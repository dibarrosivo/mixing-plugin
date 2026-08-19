#pragma once

#include <algorithm>
#include <array>

#include <dsp/Biquad.h>

/*
    Linkwitz-Riley crossovers for multiband dynamics.

    ── Why Linkwitz-Riley and not just a low-pass and a high-pass ──────────
    The one property a crossover must have is that its outputs sum back to the
    input. Otherwise the multiband processor colours the signal even with every
    band set to unity, and no amount of good compression downstream recovers
    from that.

    A pair of Butterworth filters does NOT sum flat: at the crossover both are
    -3 dB and 90 degrees apart, giving +3 dB. Linkwitz-Riley is two cascaded
    Butterworth sections, so each output is -6 dB at the crossover and the two
    are in phase there — they sum to exactly unity magnitude.

    The sum is not the identity, though: it is an all-pass. Magnitude is flat,
    phase is not. That is the accepted trade for IIR crossovers, and it is why
    the multiband splitter below has to compensate.

    ── Why the splitter needs all-passes ───────────────────────────────────
    Split three ways and the low band goes through one crossover while mid and
    high go through two. The low band therefore misses the phase shift the
    others picked up, and the three no longer sum flat — you get a dip around
    the upper crossover. Running each already-split band through an all-pass
    matched to every crossover it skipped restores the sum. Section 4 of the
    tests measures exactly this.
*/
namespace dsp
{

// A 4th-order Linkwitz-Riley two-way split: two cascaded Butterworth sections
// per output.
class LinkwitzRileySplitter
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (auto& section : lowSections)  for (auto& f : section) f.reset();
        for (auto& section : highSections) for (auto& f : section) f.reset();
    }

    void setCrossoverFrequency (float newFrequencyHz) noexcept
    {
        if (std::abs (newFrequencyHz - frequencyHz) < 1.0e-3f)
            return;

        frequencyHz = newFrequencyHz;
        updateCoefficients();
    }

    float getCrossoverFrequency() const noexcept { return frequencyHz; }

    void processSample (int channel, float input, float& low, float& high) noexcept
    {
        const auto ch = (size_t) channel;

        low  = lowSections[1][ch].processSample  (lowSections[0][ch].processSample  (input));
        high = highSections[1][ch].processSample (highSections[0][ch].processSample (input));
    }

private:
    void updateCoefficients() noexcept
    {
        const auto low  = BiquadCoefficients::makeLowPass  (sampleRate, frequencyHz);
        const auto high = BiquadCoefficients::makeHighPass (sampleRate, frequencyHz);

        for (auto& section : lowSections)  for (auto& f : section) f.setCoefficients (low);
        for (auto& section : highSections) for (auto& f : section) f.setCoefficients (high);
    }

    // [cascade stage][channel]
    std::array<std::array<Biquad, maxChannels>, 2> lowSections {};
    std::array<std::array<Biquad, maxChannels>, 2> highSections {};

    double sampleRate  { 48000.0 };
    float  frequencyHz { 1000.0f };
};

// A 2nd-order all-pass, matching the order of an LR4 pair's summed response.
class AllPassSection
{
public:
    static constexpr int maxChannels = LinkwitzRileySplitter::maxChannels;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : filters)
            f.reset();
    }

    void setFrequency (float newFrequencyHz) noexcept
    {
        if (std::abs (newFrequencyHz - frequencyHz) < 1.0e-3f)
            return;

        frequencyHz = newFrequencyHz;
        updateCoefficients();
    }

    float processSample (int channel, float input) noexcept
    {
        return filters[(size_t) channel].processSample (input);
    }

private:
    void updateCoefficients() noexcept
    {
        const auto coefficients = BiquadCoefficients::makeAllPass (sampleRate, frequencyHz);

        for (auto& f : filters)
            f.setCoefficients (coefficients);
    }

    std::array<Biquad, maxChannels> filters {};

    double sampleRate  { 48000.0 };
    float  frequencyHz { 1000.0f };
};

/*
    Splits a signal into NumBands contiguous bands whose sum is magnitude-flat.

    Crossovers cascade: the input is split at the first frequency, the high
    remainder is split at the second, and so on. Each band that finishes early
    is then run through an all-pass for every later crossover it skipped, which
    is what keeps the sum flat.
*/
template <size_t NumBands>
class MultibandSplitter
{
    static_assert (NumBands >= 2, "A splitter needs at least two bands");

public:
    static constexpr size_t numBands      = NumBands;
    static constexpr size_t numCrossovers = NumBands - 1;
    static constexpr int    maxChannels   = LinkwitzRileySplitter::maxChannels;

    void prepare (double sampleRate) noexcept
    {
        for (auto& crossover : crossovers)
            crossover.prepare (sampleRate);

        for (auto& row : compensators)
            for (auto& allPass : row)
                allPass.prepare (sampleRate);

        syncCompensators();
    }

    void reset() noexcept
    {
        for (auto& crossover : crossovers)
            crossover.reset();

        for (auto& row : compensators)
            for (auto& allPass : row)
                allPass.reset();
    }

    /*  Crossover frequencies, lowest first. They are forced into ascending
        order: a UI that lets the user drag one crossover past another would
        otherwise produce bands with negative width and a scrambled response.
    */
    void setCrossoverFrequencies (const std::array<float, numCrossovers>& frequencies) noexcept
    {
        auto previous = 20.0f;

        for (size_t i = 0; i < numCrossovers; ++i)
        {
            const auto clamped = std::max (previous * 1.01f, frequencies[i]);
            crossovers[i].setCrossoverFrequency (clamped);
            previous = clamped;
        }

        syncCompensators();
    }

    // The crossover actually in use, after ordering has been enforced.
    float getCrossoverFrequency (size_t index) const noexcept
    {
        return index < numCrossovers ? crossovers[index].getCrossoverFrequency() : 0.0f;
    }

    // Fills `bands` with one sample per band. Their sum is the input, flat in
    // magnitude.
    void processSample (int channel, float input,
                        std::array<float, NumBands>& bands) noexcept
    {
        auto remainder = input;

        for (size_t i = 0; i < numCrossovers; ++i)
        {
            float low = 0.0f, high = 0.0f;
            crossovers[i].processSample (channel, remainder, low, high);

            bands[i]  = low;
            remainder = high;
        }

        bands[NumBands - 1] = remainder;

        // Band i was split off at crossover i, so it never saw crossovers
        // i+1..end. Give it their phase shift back.
        for (size_t band = 0; band + 1 < numCrossovers; ++band)
            for (size_t crossover = band + 1; crossover < numCrossovers; ++crossover)
                bands[band] = compensators[band][crossover].processSample (channel, bands[band]);
    }

private:
    void syncCompensators() noexcept
    {
        for (size_t band = 0; band + 1 < numCrossovers; ++band)
            for (size_t crossover = band + 1; crossover < numCrossovers; ++crossover)
                compensators[band][crossover].setFrequency (
                    crossovers[crossover].getCrossoverFrequency());
    }

    std::array<LinkwitzRileySplitter, numCrossovers> crossovers {};

    // [band][crossover it skipped]. Sparse — only entries above the diagonal
    // are used — but a fixed 2D array keeps the audio path allocation-free and
    // the indexing obvious.
    std::array<std::array<AllPassSection, numCrossovers>, NumBands> compensators {};
};

} // namespace dsp

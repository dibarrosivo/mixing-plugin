#pragma once

#include <array>

#include <dsp/Compressor.h>
#include <dsp/LinkwitzRiley.h>

/*
    Client module 2: compression per band.

    Splits with a Linkwitz-Riley crossover, compresses each band independently,
    sums. The crossover is the part that had to be right first — see
    LinkwitzRiley.h — because with every band bypassed this must be
    indistinguishable from a wire, and it is the crossover that decides whether
    that holds.

    ── Why it works per sample rather than per band ────────────────────────
    The obvious structure is "split the whole buffer into N buffers, compress
    each, sum". That needs N scratch buffers sized to the block, and every band
    walks the block separately. Doing it a sample at a time needs a handful of
    floats on the stack, keeps everything in cache, and reads in the order the
    signal actually flows. It is also why Compressor exposes nextGain().

    ── Solo ────────────────────────────────────────────────────────────────
    Not a luxury. Setting up a multiband without being able to hear one band at
    a time is guesswork, and a soloed band is how you find out whether the
    crossover is where you think it is.
*/
namespace dsp
{

template <size_t NumBands>
class MultibandCompressor
{
    static_assert (NumBands >= 2, "A multiband needs at least two bands");

public:
    static constexpr size_t numBands      = NumBands;
    static constexpr size_t numCrossovers = NumBands - 1;
    static constexpr int    maxChannels   = MultibandSplitter<NumBands>::maxChannels;

    void prepare (double sampleRate)
    {
        splitter.prepare (sampleRate);

        for (auto& compressor : compressors)
            compressor.prepare (sampleRate);
    }

    void reset()
    {
        splitter.reset();

        for (auto& compressor : compressors)
            compressor.reset();
    }

    void setCrossoverFrequencies (const std::array<float, numCrossovers>& frequencies) noexcept
    {
        splitter.setCrossoverFrequencies (frequencies);
    }

    float getCrossoverFrequency (size_t index) const noexcept
    {
        return splitter.getCrossoverFrequency (index);
    }

    void setBand (size_t index, const Compressor::Settings& settings)
    {
        if (index < NumBands)
            compressors[index].setSettings (settings);
    }

    const Compressor::Settings& getBand (size_t index) const noexcept
    {
        return compressors[index].getSettings();
    }

    void setBandMuted (size_t index, bool shouldBeMuted) noexcept
    {
        if (index < NumBands)
            muted[index] = shouldBeMuted;
    }

    void setBandSoloed (size_t index, bool shouldBeSoloed) noexcept
    {
        if (index < NumBands)
            soloed[index] = shouldBeSoloed;
    }

    bool isBandMuted  (size_t index) const noexcept { return muted[index]; }
    bool isBandSoloed (size_t index) const noexcept { return soloed[index]; }

    float getBandReductionDb (size_t index) const noexcept
    {
        return index < NumBands ? compressors[index].getGainReductionDb() : 0.0f;
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;

        const auto usableChannels = std::min (numChannels, maxChannels);
        const auto scale = 1.0f / (float) usableChannels;

        // Solo is resolved once per block, not per band per sample.
        auto anySoloed = false;
        for (const auto flag : soloed)
            anySoloed = anySoloed || flag;

        for (int i = 0; i < numSamples; ++i)
        {
            std::array<std::array<float, NumBands>, (size_t) maxChannels> split {};

            for (int channel = 0; channel < usableChannels; ++channel)
                splitter.processSample (channel, channels[channel][i], split[(size_t) channel]);

            for (size_t band = 0; band < NumBands; ++band)
            {
                auto monoSum = 0.0f;

                for (int channel = 0; channel < usableChannels; ++channel)
                    monoSum += split[(size_t) channel][band];

                // The detector runs even for a muted or un-soloed band, so its
                // meter stays live and the gain does not lurch when the band
                // comes back. Muting only stops it reaching the output.
                const auto gain = compressors[band].nextGain (monoSum * scale);
                const auto audible = ! muted[band] && (! anySoloed || soloed[band]);
                const auto applied = audible ? gain : 0.0f;

                for (int channel = 0; channel < usableChannels; ++channel)
                    split[(size_t) channel][band] *= applied;
            }

            for (int channel = 0; channel < usableChannels; ++channel)
            {
                auto out = 0.0f;

                for (size_t band = 0; band < NumBands; ++band)
                    out += split[(size_t) channel][band];

                channels[channel][i] = out;
            }
        }
    }

private:
    MultibandSplitter<NumBands>          splitter;
    std::array<Compressor, NumBands>     compressors {};
    std::array<bool, NumBands>           muted {};
    std::array<bool, NumBands>           soloed {};
};

} // namespace dsp

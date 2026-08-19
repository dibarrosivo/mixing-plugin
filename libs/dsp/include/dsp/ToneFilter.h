#pragma once

#include <array>
#include <juce_dsp/juce_dsp.h>

#include <dsp/Biquad.h>

/*
    One peaking EQ band: frequency, gain, Q.

    Coefficients are recomputed only when a parameter actually moves. That check
    is not just an optimisation — recomputing every block would run four
    transcendental functions per block for no reason.
*/
namespace dsp
{

class ToneFilter
{
public:
    static constexpr size_t maxChannels = 2;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        coefficientsDirty = true;
        reset();
    }

    void reset()
    {
        for (auto& filter : filters)
            filter.reset();
    }

    void setParameters (float newFrequencyHz, float newGainDb, float newQ)
    {
        if (juce::approximatelyEqual (newFrequencyHz, frequencyHz)
            && juce::approximatelyEqual (newGainDb, gainDb)
            && juce::approximatelyEqual (newQ, q))
            return;

        frequencyHz       = newFrequencyHz;
        gainDb            = newGainDb;
        q                 = newQ;
        coefficientsDirty = true;
    }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        if (coefficientsDirty)
        {
            // Pure float maths, no allocation — safe to do on the audio thread.
            const auto c = BiquadCoefficients::makePeak (sampleRate, frequencyHz, q, gainDb);

            for (auto& filter : filters)
                filter.setCoefficients (c);

            coefficientsDirty = false;
        }

        const auto numSamples  = block.getNumSamples();
        const auto numChannels = juce::jmin (block.getNumChannels(), maxChannels);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* samples = block.getChannelPointer (ch);
            auto& filter  = filters[ch];

            for (size_t i = 0; i < numSamples; ++i)
                samples[i] = filter.processSample (samples[i]);
        }
    }

private:
    double sampleRate  { 44100.0 };
    float  frequencyHz { 1000.0f };
    float  gainDb      { 0.0f };
    float  q           { 0.707f };
    bool   coefficientsDirty { true };

    // One filter per channel: the state variables cannot be shared or the
    // channels bleed into each other.
    std::array<Biquad, maxChannels> filters;
};

} // namespace dsp

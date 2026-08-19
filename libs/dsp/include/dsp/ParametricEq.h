#pragma once

#include <algorithm>
#include <array>

#include <juce_dsp/juce_dsp.h>

#include <dsp/DynamicEqBand.h>

/*
    An N-band parametric EQ: a stack of independent bands in series, each of
    which can be static or dynamic.

    Series rather than parallel because that is what "an EQ" means — each band
    filters the output of the one before it, so their responses multiply
    (add in dB). Summing bands in parallel gives comb filtering and a response
    that looks nothing like the curve you drew.

    A consequence worth knowing: because the bands are in series, a dynamic
    band's sidechain hears the output of every band before it. Band 3 reacting
    to a resonance that band 1 has already removed is usually what you want, but
    it does mean band order affects behaviour in a way it does not for a purely
    static EQ.

    This is a thin adapter over DynamicEqBand, which holds the actual DSP and
    lives in dsp_core so it can be tested without JUCE.
*/
namespace dsp
{

template <size_t NumBands>
class ParametricEq
{
public:
    static constexpr size_t numBands = NumBands;
    static constexpr int maxChannels = DynamicEqBand::maxChannels;

    using Band = DynamicEqBand::Settings;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        for (auto& band : bands)
            band.prepare (spec.sampleRate);
    }

    void reset()
    {
        for (auto& band : bands)
            band.reset();
    }

    void setBand (size_t index, const Band& settings) noexcept
    {
        if (index < NumBands)
            bands[index].setSettings (settings);
    }

    const Band& getBand (size_t index) const noexcept
    {
        return bands[index].getSettings();
    }

    // Live gain reduction for metering. Safe to call from the message thread.
    float getBandReductionDb (size_t index) const noexcept
    {
        return index < NumBands ? bands[index].getGainReductionDb() : 0.0f;
    }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        const auto numChannels = (int) std::min (block.getNumChannels(),
                                                 (size_t) maxChannels);
        const auto numSamples  = (int) block.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0)
            return;

        // A fixed-size stack array, not a vector: this runs on the audio thread.
        float* channels[maxChannels] {};

        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel] = block.getChannelPointer ((size_t) channel);

        for (auto& band : bands)
            band.process (channels, numChannels, numSamples);
    }

private:
    std::array<DynamicEqBand, NumBands> bands {};
};

} // namespace dsp

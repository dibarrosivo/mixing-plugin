#pragma once

#include <juce_dsp/juce_dsp.h>

/*
    A gain control. Trivial on purpose — its job here is to establish the
    contract that every DSP module in this library follows:

        prepare()  configure for a sample rate / block size. Called off the
                   audio thread, before playback. Allocation is allowed here.
        process()  real-time. No allocation, no locks, no file or network I/O.
        reset()    clear internal state without reallocating (transport jumps,
                   host resets).

    Match this shape in new modules and they drop straight into any plugin's
    chain in four lines.
*/
namespace dsp
{

class GainStage
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        gain.reset (spec.sampleRate, rampSeconds);
        gain.setCurrentAndTargetValue (gain.getTargetValue());
    }

    void reset()
    {
        gain.setCurrentAndTargetValue (gain.getTargetValue());
    }

    void setGainDecibels (float decibels)
    {
        gain.setTargetValue (juce::Decibels::decibelsToGain (decibels, -96.0f));
    }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        const auto numSamples  = block.getNumSamples();
        const auto numChannels = block.getNumChannels();

        // Sample-outer / channel-inner is deliberate: the smoother must advance
        // exactly once per sample *frame*. Looping channels on the outside would
        // advance it N times per frame and make the ramp N times too fast — and
        // worse, give each channel a different gain.
        for (size_t i = 0; i < numSamples; ++i)
        {
            const auto g = gain.getNextValue();

            for (size_t ch = 0; ch < numChannels; ++ch)
                block.getChannelPointer (ch)[i] *= g;
        }
    }

private:
    // Jumping straight to a new gain value on a block boundary is an audible
    // click ("zipper noise"). Ramping over ~20 ms makes it inaudible.
    static constexpr double rampSeconds = 0.02;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 1.0f };
};

} // namespace dsp

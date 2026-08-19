#pragma once

#include <algorithm>
#include <array>
#include <memory>

#include <juce_dsp/juce_dsp.h>

#include <dsp/Biquad.h>
#include <dsp/Waveshaper.h>

/*
    Client module 4: harmonic exciter.

    ── An exciter ADDS, it does not replace ────────────────────────────────
    That is the difference between this and a distortion. The dry signal passes
    through untouched; a high-passed copy is saturated and mixed back in, so
    what reaches the output is the original plus harmonics that were not there
    before. Turn MIX to zero and you get exactly the input back.

    ── Why oversampling is not optional here ───────────────────────────────
    Saturation generates harmonics at 2x, 3x, 4x the input frequency. Feed it
    7 kHz at a 48 kHz sample rate and the fifth harmonic sits at 35 kHz — well
    above Nyquist. In a sampled system that energy does not simply vanish: it
    folds back down and reappears at 13 kHz, inharmonically, unrelated to
    anything in the music.

    That folded content is the "cheap and digital" sound, and it is precisely
    what an exciter exists to avoid. Running the shaper at 4x pushes Nyquist far
    enough out that the harmonics land where they belong, and the downsampling
    filter removes them before they can fold.

    ── Latency, and why the dry path is delayed ────────────────────────────
    Linear-phase oversampling filters cost latency. The saturated path therefore
    arrives late, and adding it to an undelayed dry path is a comb filter —
    cancellation at some frequencies, reinforcement at others, an audible
    hollowness that appears the moment MIX leaves zero.

    So the dry path is delayed by exactly the oversampler's latency, and the
    total is reported to the host through getLatencySamples(). This is the
    situation the earlier modules' bypass comments warned about; it is real now.
*/
namespace dsp
{

class HarmonicExciter
{
public:
    static constexpr int maxChannels = 2;

    struct Settings
    {
        bool  enabled { true };

        // Only content above this is saturated. Below it the signal is left
        // alone, which is what keeps an exciter from muddying the low end.
        float focusHz { 3000.0f };

        float drive      { 4.0f };
        float mixPercent { 30.0f };
        SaturationType type { SaturationType::tube };

        // Monitor only the generated harmonics. Essential for setting one of
        // these up: the effect is subtle in context and obvious in isolation.
        bool listen { false };
    };

    /*  The oversampling factor is fixed at prepare time rather than being a
        live parameter, because changing it means reallocating filters — which
        cannot happen on the audio thread. A host re-prepares on any change that
        matters, so this is the right place for it.

        factor 1 disables oversampling entirely. That exists so the aliasing it
        prevents can be measured rather than assumed.
    */
    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingFactor = 4)
    {
        sampleRate = spec.sampleRate;
        factor     = juce::jmax (1, oversamplingFactor);

        const auto channels = (size_t) juce::jmin ((int) spec.numChannels, maxChannels);

        if (factor > 1)
        {
            // log2 of the factor: JUCE counts stages, not multiples.
            auto stages = 0;
            for (auto f = factor; f > 1; f /= 2)
                ++stages;

            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                channels, (size_t) stages,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                true, true);

            oversampler->initProcessing ((size_t) spec.maximumBlockSize);
            latencySamples = oversampler->getLatencyInSamples();
        }
        else
        {
            oversampler.reset();
            latencySamples = 0.0f;
        }

        dryDelay.prepare (spec);
        dryDelay.setMaximumDelayInSamples (juce::jmax (1, (int) std::ceil (latencySamples) + 4));
        dryDelay.setDelay (latencySamples);

        scratch.setSize ((int) channels, (int) spec.maximumBlockSize, false, false, true);

        updateFilters();
        reset();
    }

    void reset()
    {
        for (auto& section : highPass)
            for (auto& filter : section)
                filter.reset();

        dryDelay.reset();

        if (oversampler != nullptr)
            oversampler->reset();
    }

    void setSettings (const Settings& newSettings) noexcept
    {
        if (! juce::approximatelyEqual (newSettings.focusHz, settings.focusHz))
        {
            settings = newSettings;
            updateFilters();
            return;
        }

        settings = newSettings;
    }

    const Settings& getSettings() const noexcept { return settings; }

    // Report this to the host. It is also what the dry path is delayed by.
    int getLatencySamples() const noexcept { return (int) std::round (latencySamples); }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        const auto numChannels = (int) juce::jmin (block.getNumChannels(), (size_t) maxChannels);
        const auto numSamples  = (int) block.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0)
            return;

        if (! settings.enabled)
            return;

        shaper.setType (settings.type);
        shaper.setDrive (settings.drive);

        // 1. High-passed copy into scratch. The saturator only ever sees the
        //    part of the spectrum the user pointed it at.
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* source = block.getChannelPointer ((size_t) channel);
            auto* destination  = scratch.getWritePointer (channel);

            auto& first  = highPass[0][(size_t) channel];
            auto& second = highPass[1][(size_t) channel];

            for (int i = 0; i < numSamples; ++i)
                destination[i] = second.processSample (first.processSample (source[i]));
        }

        juce::dsp::AudioBlock<float> wet (scratch.getArrayOfWritePointers(),
                                          (size_t) numChannels, (size_t) numSamples);

        // 2. Saturate, at whatever rate the oversampler gives us.
        if (oversampler != nullptr)
        {
            auto upsampled = oversampler->processSamplesUp (wet);

            for (size_t channel = 0; channel < upsampled.getNumChannels(); ++channel)
            {
                auto* samples = upsampled.getChannelPointer (channel);

                for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
                    samples[i] = shaper.processSample (samples[i]);
            }

            oversampler->processSamplesDown (wet);
        }
        else
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* samples = wet.getChannelPointer ((size_t) channel);

                for (int i = 0; i < numSamples; ++i)
                    samples[i] = shaper.processSample (samples[i]);
            }
        }

        // 3. Delay the dry path to match, then sum. Skipping this delay turns
        //    the parallel mix into a comb filter.
        const auto mix = settings.mixPercent * 0.01f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* dry = block.getChannelPointer ((size_t) channel);
            const auto* generated = wet.getChannelPointer ((size_t) channel);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto delayed = dryDelay.popSample (channel, latencySamples, true);
                dryDelay.pushSample (channel, dry[i]);

                dry[i] = settings.listen ? generated[i] * mix
                                         : delayed + generated[i] * mix;
            }
        }
    }

private:
    void updateFilters()
    {
        // Two cascaded Butterworth sections: 24 dB/octave, matching the
        // crossover slope used elsewhere in the chain. A gentler slope lets
        // low-mid content into the saturator and the result sounds thick
        // rather than bright.
        const auto coefficients = BiquadCoefficients::makeHighPass (sampleRate, settings.focusHz);

        for (auto& section : highPass)
            for (auto& filter : section)
                filter.setCoefficients (coefficients);
    }

    Settings   settings;
    Waveshaper shaper;

    // [cascade stage][channel]
    std::array<std::array<Biquad, maxChannels>, 2> highPass {};

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> dryDelay { 64 };

    juce::AudioBuffer<float> scratch;

    double sampleRate     { 48000.0 };
    int    factor         { 4 };
    float  latencySamples { 0.0f };
};

} // namespace dsp

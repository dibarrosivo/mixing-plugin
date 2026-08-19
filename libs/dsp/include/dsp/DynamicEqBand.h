#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

#include <dsp/Biquad.h>
#include <dsp/Decibels.h>
#include <dsp/EnvelopeFollower.h>
#include <dsp/GainComputer.h>

/*
    One band of a dynamic subtractive EQ — the client's first module.

    A dynamic EQ band is a compressor whose gain is applied by a filter instead
    of by a VCA, and whose detector listens through a band-pass tuned to the
    same frequency. That is the whole idea, and it is why the pieces already
    built compose into it:

        band-pass sidechain -> EnvelopeFollower -> GainComputer -> peak filter gain

    Subtractive falls out for free: GainComputer never returns a positive value,
    so the band can only ever pull a resonance down, never push one up. Nothing
    enforces that separately; it is a property of the curve.

    ── Why the control block ───────────────────────────────────────────────
    The detector runs every sample, because attack times of a millisecond are
    meaningless otherwise. Filter coefficients are recomputed every 16 samples
    instead — a third of a millisecond at 48 kHz, far finer than any audible
    attack, and it turns four transcendental functions per sample into four per
    sixteen. Recomputing per block instead would step the gain audibly, which is
    exactly the artefact a dynamic EQ must not have.

    Raw pointers rather than juce::dsp::AudioBlock so this stays in dsp_core and
    can be tested without JUCE. The dynamics are the part most likely to be
    subtly wrong, so they are the part most worth testing cheaply.
*/
namespace dsp
{

class DynamicEqBand
{
public:
    static constexpr int maxChannels      = 2;
    static constexpr int controlBlockSize = 16;

    struct Settings
    {
        float frequencyHz  { 1000.0f };
        float staticGainDb { 0.0f };
        float q            { 0.707f };
        bool  enabled      { false };

        bool  dynamicEnabled { false };
        float thresholdDb    { -24.0f };
        float ratio          { 4.0f };
        float kneeDb         { 6.0f };
        float attackMs       { 10.0f };
        float releaseMs      { 120.0f };
    };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

        detector.prepare (sampleRate);
        detector.setMode (EnvelopeFollower::Mode::peak);

        sidechainDirty    = true;
        coefficientsDirty = true;

        reset();
    }

    void reset()
    {
        for (auto& filter : filters)
            filter.reset();

        sidechainFilter.reset();
        detector.reset();

        currentReductionDb.store (0.0f, std::memory_order_relaxed);
        lastAppliedGainDb  = std::numeric_limits<float>::quiet_NaN();
        coefficientsDirty  = true;
    }

    void setSettings (const Settings& newSettings) noexcept
    {
        if (! approximately (newSettings.frequencyHz, settings.frequencyHz)
            || ! approximately (newSettings.q, settings.q))
        {
            sidechainDirty    = true;
            coefficientsDirty = true;
        }

        if (! approximately (newSettings.staticGainDb, settings.staticGainDb))
            coefficientsDirty = true;

        // A band switched on must not resume mid-ring with state from whenever
        // it was last active.
        if (newSettings.enabled && ! settings.enabled)
            reset();

        detector.setAttackMs (newSettings.attackMs);
        detector.setReleaseMs (newSettings.releaseMs);

        gainComputer.setThresholdDb (newSettings.thresholdDb);
        gainComputer.setRatio (newSettings.ratio);
        gainComputer.setKneeWidthDb (newSettings.kneeDb);

        settings = newSettings;
    }

    const Settings& getSettings() const noexcept { return settings; }

    /*  Current gain reduction in dB, always <= 0.

        Atomic because the audio thread writes it and the UI reads it. Relaxed
        ordering is enough: it is a meter, so a reader that is one block stale
        is fine — what is not fine is the torn read a plain float would allow.
    */
    float getGainReductionDb() const noexcept
    {
        return currentReductionDb.load (std::memory_order_relaxed);
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! settings.enabled || numChannels <= 0 || numSamples <= 0)
            return;

        const auto usableChannels = std::min (numChannels, maxChannels);

        if (sidechainDirty)
        {
            sidechainFilter.setCoefficients (
                BiquadCoefficients::makeBandPass (sampleRate, settings.frequencyHz, settings.q));
            sidechainDirty = false;
        }

        for (int start = 0; start < numSamples; start += controlBlockSize)
        {
            const auto count = std::min (controlBlockSize, numSamples - start);

            updateReduction (channels, usableChannels, start, count);
            applyCoefficientsIfChanged();

            for (int channel = 0; channel < usableChannels; ++channel)
            {
                auto* samples = channels[channel] + start;
                auto& filter  = filters[(size_t) channel];

                for (int i = 0; i < count; ++i)
                    samples[i] = filter.processSample (samples[i]);
            }
        }
    }

private:
    static bool approximately (float a, float b) noexcept
    {
        return std::abs (a - b) < 1.0e-6f;
    }

    void updateReduction (float* const* channels, int numChannels,
                          int start, int count) noexcept
    {
        if (! settings.dynamicEnabled)
        {
            currentReductionDb.store (0.0f, std::memory_order_relaxed);
            return;
        }

        // Mono sidechain, so both channels take the same reduction. Detecting
        // per channel would move the stereo image whenever one side is louder,
        // which on a vocal reads as the voice wandering.
        const auto scale = 1.0f / (float) numChannels;
        auto envelope = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            auto sum = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
                sum += channels[channel][start + i];

            // The detector must run every sample even though the coefficients
            // only change per control block, or the attack time is quantised to
            // the block and fast settings stop meaning anything.
            envelope = detector.processSample (sidechainFilter.processSample (sum * scale));
        }

        currentReductionDb.store (gainComputer.computeGainDb (gainToDecibels (envelope)),
                                  std::memory_order_relaxed);
    }

    void applyCoefficientsIfChanged() noexcept
    {
        const auto effectiveGainDb = settings.staticGainDb + getGainReductionDb();

        // The NaN initial value makes the first call always take this branch.
        if (! coefficientsDirty
            && std::abs (effectiveGainDb - lastAppliedGainDb) <= recomputeThresholdDb)
            return;

        const auto coefficients = BiquadCoefficients::makePeak (sampleRate,
                                                                settings.frequencyHz,
                                                                settings.q,
                                                                effectiveGainDb);

        for (auto& filter : filters)
            filter.setCoefficients (coefficients);

        lastAppliedGainDb = effectiveGainDb;
        coefficientsDirty = false;
    }

    // Below this the change is inaudible and not worth four transcendentals.
    static constexpr float recomputeThresholdDb = 0.01f;

    Settings settings;

    std::array<Biquad, maxChannels> filters {};
    Biquad           sidechainFilter;
    EnvelopeFollower detector;
    GainComputer     gainComputer;

    double sampleRate { 48000.0 };
    std::atomic<float> currentReductionDb { 0.0f };
    float  lastAppliedGainDb { std::numeric_limits<float>::quiet_NaN() };

    bool sidechainDirty    { true };
    bool coefficientsDirty { true };
};

} // namespace dsp

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include <dsp/Decibels.h>
#include <dsp/EnvelopeFollower.h>
#include <dsp/GainComputer.h>

/*
    A broadband compressor: detector, static curve, makeup gain.

    Third thing built from EnvelopeFollower + GainComputer, after the dynamic EQ
    band. It is deliberately a separate class rather than a mode of that one —
    a dynamic EQ band applies its gain through a filter and needs a band-pass
    sidechain, a compressor applies gain directly and listens to everything.
    Sharing the pieces is right; sharing the class would mean a constant stream
    of "if this is really an EQ band" branches.

    ── nextGain() versus process() ─────────────────────────────────────────
    process() is the convenience path for a broadband compressor. nextGain()
    exists because a multiband processor cannot use it: it has to split every
    sample, gain each band, and sum, all before advancing to the next sample.
    Exposing the per-sample gain lets the multiband drive N of these without
    N temporary buffers.

    ── Cost ────────────────────────────────────────────────────────────────
    Two transcendentals per sample — a log to get the level in dB, a pow to get
    back to linear. That is the honest cost of a correct feed-forward
    compressor, and it is what commercial ones do. At 48 kHz with four bands it
    is a low single-digit percentage of one core. If that ever matters, the fix
    is a fast log/exp pair, not a coarser control rate: quantising the gain is
    audible and quantising it per band is worse.
*/
namespace dsp
{

class Compressor
{
public:
    struct Settings
    {
        bool  enabled      { true };
        float thresholdDb  { -18.0f };
        float ratio        { 3.0f };
        float kneeDb       { 6.0f };
        float attackMs     { 10.0f };
        float releaseMs    { 120.0f };
        float makeupGainDb { 0.0f };

        // RMS tracks perceived loudness and is gentler; peak catches
        // transients. Multiband bands usually want RMS, a limiter wants peak.
        EnvelopeFollower::Mode detectorMode { EnvelopeFollower::Mode::rms };
    };

    void prepare (double sampleRate)
    {
        detector.prepare (sampleRate);
        applySettings();
        reset();
    }

    void reset()
    {
        detector.reset();
        currentReductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void setSettings (const Settings& newSettings)
    {
        settings = newSettings;
        applySettings();
    }

    const Settings& getSettings() const noexcept { return settings; }

    /*  Gain reduction in dB, always <= 0. Excludes makeup gain, because a
        meter should show what the compressor took away, not the net change.

        Atomic: the audio thread writes it and the UI reads it. Relaxed is
        enough for a meter — a stale reading is fine, a torn one is not.
    */
    float getGainReductionDb() const noexcept
    {
        return currentReductionDb.load (std::memory_order_relaxed);
    }

    /*  Advance the detector by one sample and return the linear gain to apply.

        detectorInput should already be mono — summed across channels — so that
        both sides take the same gain and the stereo image does not wander.
    */
    float nextGain (float detectorInput) noexcept
    {
        if (! settings.enabled)
        {
            currentReductionDb.store (0.0f, std::memory_order_relaxed);
            return 1.0f;
        }

        const auto envelope   = detector.processSample (detectorInput);
        const auto levelDb    = gainToDecibels (envelope);
        const auto reduction  = gainComputer.computeGainDb (levelDb);

        currentReductionDb.store (reduction, std::memory_order_relaxed);

        return decibelsToGain (reduction + settings.makeupGainDb);
    }

    // Convenience for broadband use: mono-sums for detection, applies the gain
    // to every channel.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;

        const auto scale = 1.0f / (float) numChannels;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sum = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
                sum += channels[channel][i];

            const auto gain = nextGain (sum * scale);

            for (int channel = 0; channel < numChannels; ++channel)
                channels[channel][i] *= gain;
        }
    }

private:
    void applySettings()
    {
        detector.setMode (settings.detectorMode);
        detector.setAttackMs (settings.attackMs);
        detector.setReleaseMs (settings.releaseMs);

        gainComputer.setThresholdDb (settings.thresholdDb);
        gainComputer.setRatio (settings.ratio);
        gainComputer.setKneeWidthDb (settings.kneeDb);
    }

    Settings         settings;
    EnvelopeFollower detector;
    GainComputer     gainComputer;

    std::atomic<float> currentReductionDb { 0.0f };
};

} // namespace dsp

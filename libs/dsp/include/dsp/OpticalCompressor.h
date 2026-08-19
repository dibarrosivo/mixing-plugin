#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include <dsp/Decibels.h>
#include <dsp/EnvelopeFollower.h>
#include <dsp/GainComputer.h>

/*
    A compressor with a program-dependent release — the thing that makes an
    optical compressor sound like one.

    ── What actually distinguishes an opto ─────────────────────────────────
    Not the ratio, not the knee. In an LA-2A the control signal drives a lamp
    and the gain element is a light-dependent resistor, and an LDR does not
    recover at one rate. It snaps back quickly at first, then trails off over
    seconds — and how much of that long tail you get depends on how hard and
    how long it was just driven. Compress a short transient and it recovers
    almost immediately; ride a loud passage for ten seconds and it takes an age
    to let go.

    That is modelled here as two release rates blended by a slow-moving memory
    of recent gain reduction. Set programDepth to 0 and the memory is ignored,
    leaving a conventional single-rate compressor.

    ── Where the attack and release live ───────────────────────────────────
    In Compressor, the detector smooths the LEVEL and the gain follows it
    instantly. Here the detector only rectifies, and attack/release are applied
    to the GAIN REDUCTION itself. That is the physically honest arrangement for
    an opto — the lamp responds to the control voltage, and the cell's
    sluggishness is downstream of it — and it is also the only place a
    program-dependent release can live, because the memory has to be a memory
    of gain reduction, not of signal level.

    ── Opto versus FET ─────────────────────────────────────────────────────
    The client's written brief says optical; the reference screenshot was a
    CLA-76, which is FET. Both are reachable from here, because the difference
    is mostly the release:

      opto: RMS detection, slow attack (~10 ms), wide knee, programDepth 1
      FET:  peak detection, very fast attack (<1 ms), tighter knee,
            programDepth 0 or low, much shorter release

    Exposing these as parameters rather than picking one means the decision can
    be made by ear later without touching the DSP.
*/
namespace dsp
{

class OpticalCompressor
{
public:
    struct Settings
    {
        bool  enabled      { true };
        float thresholdDb  { -20.0f };
        float ratio        { 3.0f };
        float kneeDb       { 10.0f };    // opto knees are wide and soft
        float makeupGainDb { 0.0f };

        // Rectifier window. Short — this is not the attack stage.
        float detectorMs   { 3.0f };
        EnvelopeFollower::Mode detectorMode { EnvelopeFollower::Mode::rms };

        // Applied to the gain reduction, not the level.
        float attackMs      { 10.0f };
        float releaseFastMs { 80.0f };
        float releaseSlowMs { 1500.0f };

        // 0 = single-rate release, 1 = full optical behaviour.
        float programDepth { 1.0f };
    };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        levelDetector.prepare (sampleRate);
        applySettings();
        reset();
    }

    void reset()
    {
        levelDetector.reset();
        currentReductionDb = 0.0f;
        memory = 0.0f;
        publishedReductionDb.store (0.0f, std::memory_order_relaxed);
        publishedMemory.store (0.0f, std::memory_order_relaxed);
    }

    void setSettings (const Settings& newSettings)
    {
        settings = newSettings;
        applySettings();
    }

    const Settings& getSettings() const noexcept { return settings; }

    // Gain reduction in dB, always <= 0. Excludes makeup: a meter should show
    // what was taken away, not the net change.
    float getGainReductionDb() const noexcept
    {
        return publishedReductionDb.load (std::memory_order_relaxed);
    }

    /*  How far into "slow release" the cell currently is, 0 to 1.

        Worth showing in a UI. It is the one piece of state that explains why
        the same setting behaves differently on a transient and on a sustained
        note, and without it that behaviour just looks like the compressor
        being inconsistent.
    */
    float getProgramMemory() const noexcept
    {
        return publishedMemory.load (std::memory_order_relaxed);
    }

    float nextGain (float detectorInput) noexcept
    {
        if (! settings.enabled)
        {
            currentReductionDb = 0.0f;
            memory = 0.0f;
            publishedReductionDb.store (0.0f, std::memory_order_relaxed);
            publishedMemory.store (0.0f, std::memory_order_relaxed);
            return 1.0f;
        }

        const auto level    = levelDetector.processSample (detectorInput);
        const auto targetDb = gainComputer.computeGainDb (gainToDecibels (level));

        if (targetDb < currentReductionDb)
        {
            // Clamping down.
            currentReductionDb += (targetDb - currentReductionDb) * attackRate;
        }
        else
        {
            // Letting go. Blend from the fast rate toward the slow one
            // according to how much sustained reduction the cell remembers.
            const auto blend = juceLimit (memory * settings.programDepth);
            const auto rate  = releaseFastRate + (releaseSlowRate - releaseFastRate) * blend;

            currentReductionDb += (targetDb - currentReductionDb) * rate;
        }

        // The memory itself moves slowly in both directions — that sluggishness
        // IS the effect. A fast memory would just track the reduction and the
        // two release rates would never separate.
        const auto normalised = juceLimit (-currentReductionDb / memoryScaleDb);
        memory += (normalised - memory) * memoryRate;

        publishedReductionDb.store (currentReductionDb, std::memory_order_relaxed);
        publishedMemory.store (memory, std::memory_order_relaxed);

        return decibelsToGain (currentReductionDb + settings.makeupGainDb);
    }

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
    static float juceLimit (float value) noexcept
    {
        return std::max (0.0f, std::min (1.0f, value));
    }

    // One-pole rate: the fraction of the remaining distance covered per sample.
    float rateFor (float milliseconds) const noexcept
    {
        if (milliseconds <= 0.0f)
            return 1.0f;

        return 1.0f - std::exp (-1.0f / (float) (milliseconds * 0.001 * sampleRate));
    }

    void applySettings()
    {
        levelDetector.setMode (settings.detectorMode);
        levelDetector.setAttackMs (settings.detectorMs);
        levelDetector.setReleaseMs (settings.detectorMs);

        gainComputer.setThresholdDb (settings.thresholdDb);
        gainComputer.setRatio (settings.ratio);
        gainComputer.setKneeWidthDb (settings.kneeDb);

        attackRate       = rateFor (settings.attackMs);
        releaseFastRate  = rateFor (settings.releaseFastMs);
        releaseSlowRate  = rateFor (settings.releaseSlowMs);
        memoryRate       = rateFor (memoryMs);
    }

    // Reduction at which the cell is fully in slow-release territory, and how
    // long its memory takes to build or fade.
    static constexpr float memoryScaleDb = 10.0f;
    static constexpr float memoryMs      = 600.0f;

    Settings         settings;
    EnvelopeFollower levelDetector;
    GainComputer     gainComputer;

    double sampleRate { 48000.0 };

    float attackRate      { 0.0f };
    float releaseFastRate { 0.0f };
    float releaseSlowRate { 0.0f };
    float memoryRate      { 0.0f };

    float currentReductionDb { 0.0f };
    float memory             { 0.0f };

    std::atomic<float> publishedReductionDb { 0.0f };
    std::atomic<float> publishedMemory      { 0.0f };
};

} // namespace dsp

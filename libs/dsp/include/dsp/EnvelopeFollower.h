#pragma once

#include <algorithm>
#include <cmath>

/*
    Level detector — the front half of every dynamics processor here.

    A one-pole smoother with separate attack and release coefficients, chosen
    per sample by whether the signal is rising or falling. The compressor, the
    gate, the de-esser, each band of the multiband and each band of the dynamic
    EQ all sit on top of this.

    ── On "attack time" ────────────────────────────────────────────────────
    The times below are one-pole *time constants* (tau), not 10–90% rise times.
    After tau the envelope has travelled 63.2% of the way to the target; ~90%
    takes about 2.3 tau. Plugin front panels differ on which they display, so
    if the client's reference plugin feels faster or slower at the same number,
    this convention is the first thing to check.

    ── Peak vs RMS ─────────────────────────────────────────────────────────
    Peak reacts to transients and is what you want for limiting and de-essing.
    RMS tracks perceived loudness and is gentler — closer to how an opto or a
    vari-mu behaves. Both are here because the vocal chain needs both.

    No allocation, no JUCE — lives in dsp_core so it stays cheap to test.
*/
namespace dsp
{

class EnvelopeFollower
{
public:
    enum class Mode { peak, rms };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        envelope = 0.0f;
    }

    void setMode (Mode newMode) noexcept
    {
        mode = newMode;
    }

    void setAttackMs (float milliseconds)
    {
        attackMs = std::max (0.0f, milliseconds);
        updateCoefficients();
    }

    void setReleaseMs (float milliseconds)
    {
        releaseMs = std::max (0.0f, milliseconds);
        updateCoefficients();
    }

    /*  Feed one sample, get the current envelope back.

        In peak mode the envelope is in the same units as the input. In RMS mode
        the smoothing happens on the squared signal and the square root is taken
        on the way out, so the return value is still an amplitude.
    */
    float processSample (float x) noexcept
    {
        const auto rectified = (mode == Mode::peak) ? std::abs (x) : x * x;

        // Rising uses attack, falling uses release. Comparing against the
        // stored (squared, in RMS mode) envelope keeps the two consistent.
        const auto coefficient = (rectified > envelope) ? attackCoefficient
                                                        : releaseCoefficient;

        envelope = rectified + coefficient * (envelope - rectified);

        return (mode == Mode::peak) ? envelope : std::sqrt (std::max (0.0f, envelope));
    }

    // The raw internal state, without the RMS square root. Useful when chaining
    // detectors; prefer processSample() otherwise.
    float getCurrentEnvelope() const noexcept
    {
        return (mode == Mode::peak) ? envelope : std::sqrt (std::max (0.0f, envelope));
    }

private:
    static float coefficientFor (float milliseconds, double sampleRate) noexcept
    {
        // Zero means "instant": no smoothing, the envelope jumps to the input.
        if (milliseconds <= 0.0f)
            return 0.0f;

        return std::exp (-1.0f / static_cast<float> (milliseconds * 0.001 * sampleRate));
    }

    void updateCoefficients() noexcept
    {
        attackCoefficient  = coefficientFor (attackMs, sampleRate);
        releaseCoefficient = coefficientFor (releaseMs, sampleRate);
    }

    double sampleRate { 44100.0 };
    Mode   mode       { Mode::peak };

    float attackMs  { 10.0f };
    float releaseMs { 100.0f };

    float attackCoefficient  { 0.0f };
    float releaseCoefficient { 0.0f };

    float envelope { 0.0f };
};

} // namespace dsp

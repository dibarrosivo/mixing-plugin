#pragma once

#include <algorithm>
#include <cmath>

/*
    Saturation curves for the harmonic exciter.

    ── Even versus odd harmonics ───────────────────────────────────────────
    This is the whole distinction between the voices, and it is a consequence
    of symmetry, not of taste:

      A SYMMETRIC curve — f(-x) == -f(x) — can only produce ODD harmonics.
      Third, fifth, seventh. Those sit an octave-and-a-fifth, two octaves and a
      third, and so on above the fundamental. They read as edge, grit, hardness.

      An ASYMMETRIC curve also produces EVEN harmonics. The second is exactly an
      octave up and the fourth two octaves, so they are consonant with whatever
      is already there. That is why "tube warmth" is a real perceptual thing
      rather than marketing: even harmonics reinforce the note instead of
      arguing with it.

    So `tube` is asymmetric on purpose, via a small DC bias inside the curve.
    The bias is subtracted back out afterwards, or every note would come with a
    DC offset attached.

    ── Normalisation ───────────────────────────────────────────────────────
    Each curve is scaled so that full-scale in gives roughly full-scale out at
    any drive. Without that, turning up drive mostly turns up volume, and
    everything sounds better because it is louder — which makes the control
    impossible to judge by ear.

    Pure maths, no JUCE. The harmonic content is exactly the kind of thing that
    should be measured rather than trusted, and dsp_core keeps that cheap.
*/
namespace dsp
{

enum class SaturationType
{
    tube,        // asymmetric — even harmonics, octave-consonant
    tape,        // symmetric soft — odd harmonics, gentle
    transistor   // symmetric harder — odd harmonics, more of them
};

class Waveshaper
{
public:
    void setType (SaturationType newType) noexcept { type = newType; }
    void setDrive (float newDrive) noexcept
    {
        // Below 1 the curves stop doing anything useful; above ~50 they are
        // effectively a hard clipper and more is meaningless.
        drive = std::max (1.0f, std::min (50.0f, newDrive));
    }

    SaturationType getType() const noexcept { return type; }
    float getDrive() const noexcept { return drive; }

    float processSample (float x) const noexcept
    {
        switch (type)
        {
            case SaturationType::tube:       return tube (x, drive);
            case SaturationType::tape:       return tape (x, drive);
            case SaturationType::transistor: return transistor (x, drive);
        }

        return x;
    }

    // ── The curves, exposed for testing and reuse ───────────────────────

    /*  Asymmetric soft clip.

        The bias shifts the signal onto a lopsided part of the tanh curve, so
        positive and negative halves are shaped differently and even harmonics
        appear. Subtracting tanh(drive * bias) puts the curve back through the
        origin — otherwise silence would come out at a DC offset, which sums
        badly with anything else and thumps on every bypass.
    */
    static float tube (float x, float drive) noexcept
    {
        /*  The bias is expressed in the tanh's own units and divided back out
            by drive, so `drive * bias` is a constant.

            A FIXED bias does not work: at high drive the positive half is
            already hard against the asymptote before the signal arrives, the
            curve degenerates into a negative-only clipper, and any
            normalisation based on it divides by nearly zero. Holding the
            operating point still instead keeps the same asymmetric character
            at every drive setting.
        */
        constexpr float biasInTanhUnits = 0.3f;

        const auto bias   = biasInTanhUnits / drive;
        const auto offset = std::tanh (biasInTanhUnits);

        const auto shaped = std::tanh (drive * (x + bias)) - offset;

        /*  Normalised by the LARGER extreme, not by f(+1).

            An asymmetric curve cannot have both halves reach full scale — that
            asymmetry is the entire point. Scaling by the positive peak alone
            sends the negative half well past 1.0.
        */
        const auto positive = std::abs (std::tanh (drive * ( 1.0f + bias)) - offset);
        const auto negative = std::abs (std::tanh (drive * (-1.0f + bias)) - offset);
        const auto peak     = std::max (positive, negative);

        return peak > 0.0f ? shaped / peak : shaped;
    }

    // Symmetric soft clip. Odd harmonics only, dominated by the third.
    static float tape (float x, float drive) noexcept
    {
        const auto peak = std::tanh (drive);
        return peak > 0.0f ? std::tanh (drive * x) / peak : x;
    }

    /*  Symmetric, harder knee. The classic cubic soft clipper, which reaches
        its limit at a definite point rather than asymptotically, so it
        generates more high-order odd harmonics than tanh.
    */
    static float transistor (float x, float drive) noexcept
    {
        const auto driven = drive * x;

        const auto shaped = driven <= -1.0f ? -2.0f / 3.0f
                          : driven >=  1.0f ?  2.0f / 3.0f
                                            : driven - (driven * driven * driven) / 3.0f;

        const auto peak = drive <= 1.0f ? drive - (drive * drive * drive) / 3.0f
                                        : 2.0f / 3.0f;

        return peak > 0.0f ? shaped / peak : shaped;
    }

private:
    SaturationType type  { SaturationType::tube };
    float          drive { 2.0f };
};

} // namespace dsp

#pragma once

#include <cmath>

/*
    A second-order IIR section, written out by hand.

    JUCE ships juce::dsp::IIR, and it is good — but its coefficient factories
    return a reference-counted object, i.e. they *allocate*. Allocating on the
    audio thread is the single most common real-time bug in plugin code, and it
    is invisible until a user reports crackling under load.

    Everything here is plain floats in a fixed-size struct: no allocation, no
    locks, safe to recompute inside processBlock.

    The coefficient formulas are the RBJ Audio EQ Cookbook, which is the
    reference every EQ you have ever used is ultimately based on. Shelves and
    pass filters are further formulas over this same struct — add them here
    rather than writing a second filter class.
*/
namespace dsp
{

struct BiquadCoefficients
{
    float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f };
    float a1 { 0.0f }, a2 { 0.0f };

    static BiquadCoefficients makePeak (double sampleRate,
                                        float  frequencyHz,
                                        float  q,
                                        float  gainDb)
    {
        // A is the *square root* of the linear gain for peaking and shelving
        // filters — hence /40 rather than /20.
        const auto A     = std::pow (10.0f, gainDb / 40.0f);
        const auto w0    = 2.0f * 3.14159265358979323846f
                                * frequencyHz / static_cast<float> (sampleRate);
        const auto cosW0 = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * q);

        const auto b0 =  1.0f + alpha * A;
        const auto b1 = -2.0f * cosW0;
        const auto b2 =  1.0f - alpha * A;
        const auto a0 =  1.0f + alpha / A;
        const auto a1 = -2.0f * cosW0;
        const auto a2 =  1.0f - alpha / A;

        // Normalise so a0 == 1 and it drops out of the difference equation.
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    /*  Band-pass with constant 0 dB peak gain.

        This is the sidechain filter for a dynamic EQ band: it lets the detector
        hear only the frequencies the band actually controls. Without it the
        band reacts to the whole signal and you have a compressor that happens
        to be wired to a filter, which is a different and much less useful tool.

        "Constant 0 dB peak gain" matters here: whatever the Q, a sine at the
        centre frequency comes through at unity. That means a threshold in dBFS
        keeps meaning the same thing when the user changes Q.
    */
    static BiquadCoefficients makeBandPass (double sampleRate,
                                            float  frequencyHz,
                                            float  q) noexcept
    {
        const auto w0    = 2.0f * 3.14159265358979323846f
                                * frequencyHz / static_cast<float> (sampleRate);
        const auto cosW0 = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * q);

        const auto b0 =  alpha;
        const auto b1 =  0.0f;
        const auto b2 = -alpha;
        const auto a0 =  1.0f + alpha;
        const auto a1 = -2.0f * cosW0;
        const auto a2 =  1.0f - alpha;

        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    // Q for a maximally-flat (Butterworth) second-order section. Two of these
    // cascaded give a 4th-order Linkwitz-Riley, which is the standard crossover
    // for multiband dynamics.
    static constexpr float butterworthQ = 0.70710678f;

    static BiquadCoefficients makeLowPass (double sampleRate,
                                           float  frequencyHz,
                                           float  q = butterworthQ) noexcept
    {
        const auto w0    = angularFrequency (sampleRate, frequencyHz);
        const auto cosW0 = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * q);

        const auto b1 =  1.0f - cosW0;
        const auto b0 =  b1 * 0.5f;
        const auto b2 =  b0;
        const auto a0 =  1.0f + alpha;
        const auto a1 = -2.0f * cosW0;
        const auto a2 =  1.0f - alpha;

        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    static BiquadCoefficients makeHighPass (double sampleRate,
                                            float  frequencyHz,
                                            float  q = butterworthQ) noexcept
    {
        const auto w0    = angularFrequency (sampleRate, frequencyHz);
        const auto cosW0 = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * q);

        const auto b0 =  (1.0f + cosW0) * 0.5f;
        const auto b1 = -(1.0f + cosW0);
        const auto b2 =  b0;
        const auto a0 =  1.0f + alpha;
        const auto a1 = -2.0f * cosW0;
        const auto a2 =  1.0f - alpha;

        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    /*  Second-order all-pass: unity magnitude everywhere, phase only.

        Needed to keep a multi-way crossover flat. Splitting three ways means
        the low band never passes through the second crossover, so it does not
        pick up the phase shift the other two did — and the three no longer sum
        back to the input. Running the low band through an all-pass matched to
        that crossover restores it.
    */
    static BiquadCoefficients makeAllPass (double sampleRate,
                                           float  frequencyHz,
                                           float  q = butterworthQ) noexcept
    {
        const auto w0    = angularFrequency (sampleRate, frequencyHz);
        const auto cosW0 = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * q);

        const auto b0 =  1.0f - alpha;
        const auto b1 = -2.0f * cosW0;
        const auto b2 =  1.0f + alpha;
        const auto a0 =  1.0f + alpha;
        const auto a1 = -2.0f * cosW0;
        const auto a2 =  1.0f - alpha;

        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

private:
    static float angularFrequency (double sampleRate, float frequencyHz) noexcept
    {
        return 2.0f * 3.14159265358979323846f
                    * frequencyHz / static_cast<float> (sampleRate);
    }
};

/*
    Transposed Direct Form II.

    Chosen over Direct Form I because it needs only two state variables per
    channel instead of four, and behaves better numerically at low frequencies
    in single precision.
*/
class Biquad
{
public:
    void reset() noexcept
    {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    void setCoefficients (const BiquadCoefficients& newCoefficients) noexcept
    {
        c = newCoefficients;
    }

    float processSample (float x) noexcept
    {
        const auto y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }

private:
    BiquadCoefficients c;
    float s1 { 0.0f };
    float s2 { 0.0f };
};

} // namespace dsp

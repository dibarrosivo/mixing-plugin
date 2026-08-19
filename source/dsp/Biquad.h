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
    reference every EQ you have ever used is ultimately based on.
*/
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

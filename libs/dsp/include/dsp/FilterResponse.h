#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

#include <dsp/Biquad.h>

/*
    Frequency-response evaluation and the log frequency mapping.

    This is what draws the curve. A Pro-Q style display evaluates the filter
    chain's magnitude at every horizontal pixel, so this runs a few thousand
    times per repaint — but on the message thread, never the audio thread.

    Pure maths, so it lives in dsp_core and is testable without JUCE. That
    matters more than it sounds: a wrong curve looks plausible, and "the display
    disagrees with what I hear" is a miserable bug to chase from the UI side.
*/
namespace dsp
{

// |H(e^jw)| in dB for one biquad section.
inline float magnitudeDb (const BiquadCoefficients& c,
                          double frequencyHz,
                          double sampleRate) noexcept
{
    constexpr double pi = 3.14159265358979323846;

    const auto w = 2.0 * pi * frequencyHz / sampleRate;
    const std::complex<double> z1 { std::cos (-w), std::sin (-w) };
    const auto z2 = z1 * z1;

    const auto numerator = std::complex<double> (c.b0)
                         + std::complex<double> (c.b1) * z1
                         + std::complex<double> (c.b2) * z2;

    const auto denominator = std::complex<double> (1.0)
                           + std::complex<double> (c.a1) * z1
                           + std::complex<double> (c.a2) * z2;

    const auto magnitude = std::abs (numerator) / std::abs (denominator);

    return magnitude > 0.0 ? static_cast<float> (20.0 * std::log10 (magnitude))
                           : -120.0f;
}

/*
    Log frequency mapping, normalised to 0..1 across the display width.

    Linear frequency would spend half the display above 10 kHz and squeeze
    everything musical into the left edge. Every analyser you have ever looked
    at is logarithmic; this is that mapping, factored out so the curve, the
    grid and the spectrum cannot drift apart.
*/
inline float frequencyToNormalised (float frequencyHz,
                                    float minHz = 20.0f,
                                    float maxHz = 20000.0f) noexcept
{
    const auto clamped = std::max (minHz, std::min (maxHz, frequencyHz));
    return std::log (clamped / minHz) / std::log (maxHz / minHz);
}

inline float normalisedToFrequency (float normalised,
                                    float minHz = 20.0f,
                                    float maxHz = 20000.0f) noexcept
{
    const auto clamped = std::max (0.0f, std::min (1.0f, normalised));
    return minHz * std::pow (maxHz / minHz, clamped);
}

} // namespace dsp

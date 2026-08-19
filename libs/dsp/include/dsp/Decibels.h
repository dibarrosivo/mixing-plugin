#pragma once

#include <algorithm>
#include <cmath>

/*
    dB <-> linear conversions.

    juce::Decibels does the same job, but these live in dsp_core so that pure
    modules — detectors, gain computers — stay free of JUCE and keep the
    sub-second, framework-free test path.
*/
namespace dsp
{

// Levels below minusInfinityDb are treated as silence. -100 dB is well below
// the noise floor of any real signal and avoids log10(0).
inline constexpr float defaultMinusInfinityDb = -100.0f;

inline float gainToDecibels (float gain,
                             float minusInfinityDb = defaultMinusInfinityDb) noexcept
{
    return gain > 0.0f ? std::max (minusInfinityDb, 20.0f * std::log10 (gain))
                       : minusInfinityDb;
}

inline float decibelsToGain (float decibels,
                             float minusInfinityDb = defaultMinusInfinityDb) noexcept
{
    return decibels > minusInfinityDb ? std::pow (10.0f, decibels * 0.05f)
                                      : 0.0f;
}

} // namespace dsp

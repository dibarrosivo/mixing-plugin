#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Parameters for the optical compressor.

    Deliberately fewer controls than the DSP supports. OpticalCompressor exposes
    releaseFastMs and releaseSlowMs separately, but no one thinks about an opto
    that way — the hardware has one release control and a cell that does the
    rest. RELEASE therefore drives the fast stage and the slow one is derived
    from it, with PROGRAM setting how much of the slow tail applies at all.
*/
namespace ParamID
{
    inline constexpr auto character  = "character";
    inline constexpr auto threshold  = "threshold";
    inline constexpr auto ratio      = "ratio";
    inline constexpr auto attack     = "attack";
    inline constexpr auto release    = "release";
    inline constexpr auto program    = "program";
    inline constexpr auto makeup     = "makeup";

    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto bypass     = "bypass";
}

/*  The two voices the client's brief and reference screenshot point at.

    Optical is what the written spec asks for; FET is what the CLA-76 in the
    reference actually is. Both are one enum away because they differ mostly in
    detection and release, not in structure.
*/
enum class Character { optical = 0, fet = 1 };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

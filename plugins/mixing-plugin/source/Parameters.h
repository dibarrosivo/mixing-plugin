#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Single source of truth for the plugin's parameters.

    Everything that needs to refer to a parameter — the processor, the editor,
    the state tree — goes through the IDs below. Nobody types a parameter ID as
    a bare string anywhere else, so renaming one is a compiler error rather than
    a silent runtime miss.
*/
namespace ParamID
{
    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto toneFreq   = "toneFreq";
    inline constexpr auto toneGain   = "toneGain";
    inline constexpr auto toneQ      = "toneQ";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto bypass     = "bypass";
}

/*
    The version hint (the `1` in ParameterID { id, 1 }) tells hosts which
    parameters existed in which release. Bump it for a parameter only when you
    change its meaning, so that older saved sessions keep mapping correctly.
*/
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

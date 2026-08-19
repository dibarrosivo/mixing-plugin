#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamID
{
    inline constexpr auto focus      = "focus";
    inline constexpr auto drive      = "drive";
    inline constexpr auto mix        = "mix";
    inline constexpr auto type       = "type";
    inline constexpr auto listen     = "listen";

    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto bypass     = "bypass";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Single source of truth for the multiband compressor's parameters.

    Band IDs are built rather than listed because there are eight per band. The
    format is fixed forever once released: renaming one orphans every saved
    session that used it.
*/
namespace ParamID
{
    inline constexpr int numBands      = 4;
    inline constexpr int numCrossovers = numBands - 1;

    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto bypass     = "bypass";

    inline juce::String crossover (int index) { return "xover" + juce::String (index); }

    inline juce::String bandOn        (int i) { return "band" + juce::String (i) + "On"; }
    inline juce::String bandThreshold (int i) { return "band" + juce::String (i) + "Thr"; }
    inline juce::String bandRatio     (int i) { return "band" + juce::String (i) + "Ratio"; }
    inline juce::String bandAttack    (int i) { return "band" + juce::String (i) + "Atk"; }
    inline juce::String bandRelease   (int i) { return "band" + juce::String (i) + "Rel"; }
    inline juce::String bandMakeup    (int i) { return "band" + juce::String (i) + "Makeup"; }
    inline juce::String bandMute      (int i) { return "band" + juce::String (i) + "Mute"; }
    inline juce::String bandSolo      (int i) { return "band" + juce::String (i) + "Solo"; }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

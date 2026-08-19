#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Single source of truth for the plugin's parameters.

    Everything that refers to a parameter — the processor, the editor, the state
    tree — goes through the helpers below. Nobody types a parameter ID as a bare
    string anywhere else, so a rename is a compiler error rather than a silent
    runtime miss.

    Band IDs are built rather than listed because there are four per band. The
    format is fixed forever once released: changing "band0Freq" to anything else
    orphans every saved session.
*/
namespace ParamID
{
    inline constexpr int numBands = 6;

    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto bypass     = "bypass";

    inline juce::String bandFreq (int index) { return "band" + juce::String (index) + "Freq"; }
    inline juce::String bandGain (int index) { return "band" + juce::String (index) + "Gain"; }
    inline juce::String bandQ    (int index) { return "band" + juce::String (index) + "Q"; }
    inline juce::String bandOn   (int index) { return "band" + juce::String (index) + "On"; }

    // Dynamics, per band. A dynamic band is the same filter with its gain
    // driven by a detector listening through a band-pass at the same frequency.
    inline juce::String bandDyn       (int index) { return "band" + juce::String (index) + "Dyn"; }
    inline juce::String bandThreshold (int index) { return "band" + juce::String (index) + "Thr"; }
    inline juce::String bandRatio     (int index) { return "band" + juce::String (index) + "Ratio"; }
    inline juce::String bandAttack    (int index) { return "band" + juce::String (index) + "Atk"; }
    inline juce::String bandRelease   (int index) { return "band" + juce::String (index) + "Rel"; }
}

/*
    The version hint (the `1` in ParameterID { id, 1 }) tells hosts which
    parameters existed in which release. Bump it for a parameter only when you
    change its meaning, so that older saved sessions keep mapping correctly.
*/
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

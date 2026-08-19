#include "Parameters.h"

namespace
{
    juce::NormalisableRange<float> decibelRange (float low, float high)
    {
        return { low, high, 0.01f };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Only content above this is saturated. Below ~500 Hz an exciter stops
    // being an exciter and becomes a distortion on the whole signal.
    juce::NormalisableRange<float> focusRange { 500.0f, 16000.0f, 1.0f };
    focusRange.setSkewForCentre (3000.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::focus, 1 }, "Focus", focusRange, 3000.0f));

    juce::NormalisableRange<float> driveRange { 1.0f, 50.0f, 0.01f };
    driveRange.setSkewForCentre (8.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::drive, 1 }, "Drive", driveRange, 5.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::mix, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 25.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::type, 1 }, "Type",
        juce::StringArray { "Tube", "Tape", "Transistor" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::listen, 1 }, "Listen", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::inputGain, 1 },
        "Input Gain", decibelRange (-24.0f, 24.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::outputGain, 1 },
        "Output Gain", decibelRange (-24.0f, 24.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::bypass, 1 }, "Bypass", false));

    return layout;
}

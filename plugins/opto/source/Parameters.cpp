#include "Parameters.h"

namespace
{
    juce::NormalisableRange<float> decibelRange (float low, float high)
    {
        return { low, high, 0.01f };
    }

    juce::NormalisableRange<float> ratioRange()
    {
        juce::NormalisableRange<float> range { 1.0f, 20.0f, 0.01f };
        range.setSkewForCentre (4.0f);
        return range;
    }

    juce::NormalisableRange<float> timeRange (float low, float high, float centre)
    {
        juce::NormalisableRange<float> range { low, high, 0.01f };
        range.setSkewForCentre (centre);
        return range;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::character, 1 },
        "Character",
        juce::StringArray { "Optical", "FET" },
        0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::threshold, 1 },
        "Threshold", decibelRange (-60.0f, 0.0f), -20.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::ratio, 1 },
        "Ratio", ratioRange(), 3.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::attack, 1 },
        "Attack", timeRange (0.05f, 100.0f, 10.0f), 10.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::release, 1 },
        "Release", timeRange (20.0f, 1000.0f, 150.0f), 120.0f));

    // How much of the long optical tail applies. At 0 this is an ordinary
    // single-rate compressor, which is also the FET voice.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::program, 1 },
        "Program", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 80.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::makeup, 1 },
        "Makeup", decibelRange (-12.0f, 24.0f), 0.0f));

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

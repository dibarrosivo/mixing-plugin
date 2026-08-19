#include "Parameters.h"

namespace
{
    juce::NormalisableRange<float> decibelRange (float low, float high)
    {
        return { low, high, 0.01f };
    }

    juce::NormalisableRange<float> frequencyRange (float low, float high, float centre)
    {
        juce::NormalisableRange<float> range { low, high, 1.0f };
        range.setSkewForCentre (centre);
        return range;
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

    // Low / low-mid / high-mid / high, roughly where a vocal wants them.
    constexpr float defaultCrossovers[ParamID::numCrossovers] = { 180.0f, 900.0f, 5000.0f };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < ParamID::numCrossovers; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::crossover (i), 1 },
            "Crossover " + juce::String (i + 1),
            frequencyRange (20.0f, 20000.0f, 1000.0f),
            defaultCrossovers[i]));
    }

    for (int band = 0; band < ParamID::numBands; ++band)
    {
        const auto number = juce::String (band + 1);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamID::bandOn (band), 1 },
            "Band " + number + " On", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandThreshold (band), 1 },
            "Band " + number + " Threshold",
            decibelRange (-60.0f, 0.0f), -20.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandRatio (band), 1 },
            "Band " + number + " Ratio", ratioRange(), 3.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandAttack (band), 1 },
            "Band " + number + " Attack",
            timeRange (0.1f, 200.0f, 10.0f), 15.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandRelease (band), 1 },
            "Band " + number + " Release",
            timeRange (5.0f, 2000.0f, 150.0f), 150.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandMakeup (band), 1 },
            "Band " + number + " Makeup",
            decibelRange (-12.0f, 12.0f), 0.0f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamID::bandMute (band), 1 },
            "Band " + number + " Mute", false));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamID::bandSolo (band), 1 },
            "Band " + number + " Solo", false));
    }

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

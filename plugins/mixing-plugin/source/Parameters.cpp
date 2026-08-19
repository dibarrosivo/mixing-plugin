#include "Parameters.h"

namespace
{
    juce::NormalisableRange<float> decibelRange (float low, float high)
    {
        return { low, high, 0.01f };
    }

    juce::NormalisableRange<float> frequencyRange()
    {
        juce::NormalisableRange<float> range { 20.0f, 20000.0f, 1.0f };

        // Without this, half the knob's travel is spent above 10 kHz. Skewing
        // the centre to 1 kHz makes the control feel logarithmic, which is how
        // we hear pitch.
        range.setSkewForCentre (1000.0f);
        return range;
    }

    juce::NormalisableRange<float> qRange()
    {
        juce::NormalisableRange<float> range { 0.1f, 10.0f, 0.01f };
        range.setSkewForCentre (0.707f);
        return range;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::inputGain, 1 },
        "Input Gain",
        decibelRange (-24.0f, 24.0f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::toneFreq, 1 },
        "Tone Freq",
        frequencyRange(),
        1000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::toneGain, 1 },
        "Tone Gain",
        decibelRange (-18.0f, 18.0f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::toneQ, 1 },
        "Tone Q",
        qRange(),
        0.707f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::outputGain, 1 },
        "Output Gain",
        decibelRange (-24.0f, 24.0f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::bypass, 1 },
        "Bypass",
        false));

    return layout;
}

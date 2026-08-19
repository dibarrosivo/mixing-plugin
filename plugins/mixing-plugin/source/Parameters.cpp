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

    juce::NormalisableRange<float> ratioRange()
    {
        juce::NormalisableRange<float> range { 1.0f, 20.0f, 0.01f };

        // Skewed low: everything musically useful happens between 1.5:1 and
        // 6:1, and a linear control spends most of its travel above that.
        range.setSkewForCentre (4.0f);
        return range;
    }

    juce::NormalisableRange<float> timeRange (float low, float high, float centre)
    {
        juce::NormalisableRange<float> range { low, high, 0.01f };
        range.setSkewForCentre (centre);
        return range;
    }

    juce::NormalisableRange<float> qRange()
    {
        juce::NormalisableRange<float> range { 0.1f, 10.0f, 0.01f };
        range.setSkewForCentre (0.707f);
        return range;
    }

    // Spread across the spectrum so that switching bands on in order gives
    // something musically sensible rather than six stacked at 1 kHz.
    constexpr float defaultFrequencies[ParamID::numBands] = {
        80.0f, 250.0f, 700.0f, 2000.0f, 5000.0f, 12000.0f
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int band = 0; band < ParamID::numBands; ++band)
    {
        const auto number = juce::String (band + 1);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandFreq (band), 1 },
            "Band " + number + " Freq",
            frequencyRange(),
            defaultFrequencies[band]));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandGain (band), 1 },
            "Band " + number + " Gain",
            decibelRange (-18.0f, 18.0f),
            0.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandQ (band), 1 },
            "Band " + number + " Q",
            qRange(),
            0.707f));

        // Only the first band is on by default. Six handles on a fresh instance
        // is clutter; the user adds bands by double-clicking the display.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamID::bandOn (band), 1 },
            "Band " + number + " On",
            band == 0));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamID::bandDyn (band), 1 },
            "Band " + number + " Dynamic",
            false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandThreshold (band), 1 },
            "Band " + number + " Threshold",
            decibelRange (-60.0f, 0.0f),
            -24.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandRatio (band), 1 },
            "Band " + number + " Ratio",
            ratioRange(),
            4.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandAttack (band), 1 },
            "Band " + number + " Attack",
            timeRange (0.1f, 200.0f, 10.0f),
            10.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamID::bandRelease (band), 1 },
            "Band " + number + " Release",
            timeRange (5.0f, 2000.0f, 150.0f),
            120.0f));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::inputGain, 1 },
        "Input Gain",
        decibelRange (-24.0f, 24.0f),
        0.0f));

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

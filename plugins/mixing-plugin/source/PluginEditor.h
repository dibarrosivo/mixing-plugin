#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ui/FrequencyCurveDisplay.h>
#include <ui/KnobLookAndFeel.h>
#include <ui/Theme.h>

#include "PluginProcessor.h"

class MixingPluginEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit MixingPluginEditor (MixingPluginProcessor&);
    ~MixingPluginEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // Total response of the chain at a given frequency: the EQ band plus the
    // broadband gains, so the curve shows what actually comes out rather than
    // just what the filter does.
    float responseDbAt (float frequencyHz) const;
    void  updateNodes();

    /*
        Slider + caption + attachment as one unit.

        The attachment keeps the control, the parameter and the host's
        automation lane in sync in both directions — never call
        setValue()/getValue() on the slider yourself once one exists.

        Declaration order matters: the attachment must be constructed after the
        slider it binds to, and destroyed before it.
    */
    struct AttachedKnob
    {
        AttachedKnob (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& captionText,
                      const juce::String& suffix,
                      juce::LookAndFeel& lookAndFeel,
                      juce::Component& parent);

        void setBounds (juce::Rectangle<int> area);

        juce::Slider slider;
        juce::Label  caption;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };

    MixingPluginProcessor& processorRef;

    // Three, because bipolarity is a per-control property: gain-style knobs
    // must fill outward from 12 o'clock, frequency and Q from the left stop.
    ui::KnobLookAndFeel ioLook       { ui::theme::textDim };
    ui::KnobLookAndFeel bandLook     { ui::theme::bandColour (0) };
    ui::KnobLookAndFeel bandGainLook { ui::theme::bandColour (0) };

    ui::FrequencyCurveDisplay curveDisplay;

    AttachedKnob inputGain;
    AttachedKnob toneFreq;
    AttachedKnob toneGain;
    AttachedKnob toneQ;
    AttachedKnob outputGain;

    juce::ToggleButton bypassButton { "BYPASS" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    // Repainting only when something moved keeps an idle editor at zero CPU,
    // which matters once thirty of these are open in a session.
    float lastResponseSignature { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixingPluginEditor)
};

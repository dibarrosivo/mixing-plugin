#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include <ui/FrequencyCurveDisplay.h>
#include <ui/KnobLookAndFeel.h>
#include <ui/Theme.h>

#include "PluginProcessor.h"

class ExciterEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit ExciterEditor (ExciterProcessor&);
    ~ExciterEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateRegions();
    void setParameter (juce::StringRef parameterID, float value);

    float focusHz() const;
    int   typeIndex() const;

    struct Knob
    {
        Knob (const juce::String& captionText,
              const juce::String& suffix,
              juce::LookAndFeel& lookAndFeel,
              juce::Component& parent);

        void setBounds (juce::Rectangle<int> area);

        juce::Slider slider;
        juce::Label  caption;
    };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    ExciterProcessor& processorRef;

    ui::KnobLookAndFeel ioLook   { ui::theme::textDim };
    ui::KnobLookAndFeel mainLook { ui::theme::bandColour (1) };

    ui::FrequencyCurveDisplay display;

    Knob focusKnob, driveKnob, mixKnob, inputKnob, outputKnob;

    juce::ComboBox     typeBox;
    juce::ToggleButton listenButton { "LISTEN" };
    juce::ToggleButton bypassButton { "BYPASS" };

    std::unique_ptr<SliderAttachment> focusAttachment, driveAttachment, mixAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> listenAttachment, bypassAttachment;
    std::unique_ptr<ChoiceAttachment> typeAttachment;

    float lastSignature { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExciterEditor)
};

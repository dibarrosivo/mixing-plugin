#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include <ui/GainReductionHistory.h>
#include <ui/KnobLookAndFeel.h>
#include <ui/Theme.h>

#include "PluginProcessor.h"

class OptoEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit OptoEditor (OptoProcessor&);
    ~OptoEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyCharacter();
    bool isFet() const;

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

    OptoProcessor& processorRef;

    ui::KnobLookAndFeel ioLook   { ui::theme::textDim };
    ui::KnobLookAndFeel mainLook { ui::theme::bandColour (0) };

    ui::GainReductionHistory history;

    Knob thresholdKnob, ratioKnob, attackKnob, releaseKnob, programKnob, makeupKnob;
    Knob inputKnob, outputKnob;

    juce::ComboBox     characterBox;
    juce::ToggleButton bypassButton { "BYPASS" };

    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment, programAttachment, makeupAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ChoiceAttachment> characterAttachment;

    bool lastWasFet { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoEditor)
};

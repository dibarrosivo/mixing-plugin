#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <ui/FrequencyCurveDisplay.h>
#include <ui/KnobLookAndFeel.h>
#include <ui/Theme.h>

#include "PluginProcessor.h"

class MultibandEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit MultibandEditor (MultibandProcessor&);
    ~MultibandEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int numBands      = ParamID::numBands;
    static constexpr int numCrossovers = ParamID::numCrossovers;

    void timerCallback() override;

    void selectBand (int band);
    void updateRegions();

    void setParameter (juce::StringRef parameterID, float value);
    void setCrossoverGesture (int index, bool starting);

    bool isBandOn   (int band) const;
    bool isBandMuted (int band) const;
    bool anySoloed() const;

    void paintReductionMeter (juce::Graphics& g, juce::Rectangle<int> area) const;

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

    MultibandProcessor& processorRef;

    ui::KnobLookAndFeel ioLook     { ui::theme::textDim };
    ui::KnobLookAndFeel bandLook   { ui::theme::bandColour (0) };
    ui::KnobLookAndFeel makeupLook { ui::theme::bandColour (0) };

    ui::FrequencyCurveDisplay display;

    Knob thresholdKnob, ratioKnob, attackKnob, releaseKnob, makeupKnob;
    Knob inputKnob, outputKnob;

    juce::ToggleButton bandOnButton { "ON" };
    juce::ToggleButton muteButton   { "M" };
    juce::ToggleButton soloButton   { "S" };
    juce::ToggleButton bypassButton { "BYPASS" };

    // Rebuilt whenever the selected band changes.
    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment, releaseAttachment, makeupAttachment;
    std::unique_ptr<ButtonAttachment> bandOnAttachment, muteAttachment, soloAttachment;

    // Fixed for the lifetime of the editor.
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    int selectedBand { 0 };

    float lastParameterSignature { 0.0f };
    std::array<float, (size_t) numBands> lastReductionDb {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandEditor)
};

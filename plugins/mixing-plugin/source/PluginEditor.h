#pragma once

#include <array>
#include <memory>
#include <vector>

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
    static constexpr int numBands = ParamID::numBands;

    void timerCallback() override;

    // ── Response ────────────────────────────────────────────────────────
    // includeDynamics selects the live curve (what is happening right now) or
    // the static one (what the knobs alone would do). The gap between them is
    // the gain reduction, which is what the ghost curve shows.
    float bandResponseDbAt (int band, float frequencyHz, bool includeDynamics) const;
    float responseDbAt (float frequencyHz) const;
    float staticResponseDbAt (float frequencyHz) const;

    // Broadband offset applied to the curve. Dragging a handle must subtract it
    // so the band ends up where the pointer is, not where the pointer plus the
    // I/O trim happens to land.
    float broadbandOffsetDb() const;

    // ── Band management ─────────────────────────────────────────────────
    void selectBand (int band);
    void addBandAt (float frequencyHz, float gainDb);
    void removeBand (int band);
    bool isBandOn (int band) const;
    bool isBandDynamic (int band) const;

    void updateNodes();
    void paintReductionMeter (juce::Graphics& g, juce::Rectangle<int> area) const;
    void setParameter (juce::StringRef parameterID, float value);
    void setBandGesture (int band, bool starting);

    /*  Slider plus caption. The attachment is deliberately NOT held here.

        The band controls retarget as the selection changes, so their
        attachments are owned separately and rebuilt on each selection.
        Bundling one in would mean destroying the slider too, which loses
        mouse capture mid-interaction.
    */
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

    MixingPluginProcessor& processorRef;

    // Bipolarity is a per-control property: gain-style knobs fill outward from
    // 12 o'clock, frequency and Q from the left stop.
    ui::KnobLookAndFeel ioLook       { ui::theme::textDim };
    ui::KnobLookAndFeel bandLook     { ui::theme::bandColour (0) };
    ui::KnobLookAndFeel bandGainLook { ui::theme::bandColour (0) };

    ui::FrequencyCurveDisplay curveDisplay;

    Knob freqKnob, gainKnob, qKnob, inputKnob, outputKnob;
    Knob thresholdKnob, ratioKnob, attackKnob, releaseKnob;

    juce::ToggleButton bandOnButton { "ON" };
    juce::ToggleButton bandDynButton { "DYN" };
    juce::ToggleButton bypassButton { "BYPASS" };

    // Rebuilt whenever the selected band changes.
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment, releaseAttachment;
    std::unique_ptr<ButtonAttachment> bandOnAttachment, bandDynAttachment;

    // Fixed for the lifetime of the editor.
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    int selectedBand { 0 };

    // Repainting only when something moved keeps an idle editor at zero CPU,
    // which matters once thirty of these are open in a session.
    float lastResponseSignature { 0.0f };

    // Gain reduction moves with the audio, so it needs its own change check —
    // the parameter signature never sees it.
    std::array<float, (size_t) numBands> lastReductionDb {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixingPluginEditor)
};

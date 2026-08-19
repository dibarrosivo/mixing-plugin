#include "PluginEditor.h"

namespace
{
    constexpr int knobWidth   = 96;
    constexpr int knobHeight  = 108;
    constexpr int margin      = 16;
    constexpr int headerHeight = 34;
}

MixingPluginEditor::AttachedSlider::AttachedSlider (juce::AudioProcessorValueTreeState& state,
                                                    const juce::String& parameterID,
                                                    const juce::String& labelText,
                                                    const juce::String& suffix,
                                                    juce::Component& parent)
    : attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth, 18);
    slider.setTextValueSuffix (suffix);
    parent.addAndMakeVisible (slider);

    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
    parent.addAndMakeVisible (label);
}

MixingPluginEditor::MixingPluginEditor (MixingPluginProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      inputGain  (p.apvts, ParamID::inputGain,  "Input",  " dB", *this),
      toneFreq   (p.apvts, ParamID::toneFreq,   "Freq",   " Hz", *this),
      toneGain   (p.apvts, ParamID::toneGain,   "Tone",   " dB", *this),
      toneQ      (p.apvts, ParamID::toneQ,      "Q",      "",    *this),
      outputGain (p.apvts, ParamID::outputGain, "Output", " dB", *this),
      bypassAttachment (p.apvts, ParamID::bypass, bypassButton)
{
    addAndMakeVisible (bypassButton);

    setSize (knobWidth * 5 + margin * 2,
             knobHeight + headerHeight + margin * 2 + 24);
}

void MixingPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1f26));

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (processorRef.getName(),
                margin, margin, getWidth() - margin * 2, headerHeight - margin,
                juce::Justification::centredLeft);
}

void MixingPluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    auto header = bounds.removeFromTop (headerHeight - margin);
    bypassButton.setBounds (header.removeFromRight (90));

    bounds.removeFromTop (24);   // room for the labels attached above the knobs

    for (auto* control : { &inputGain, &toneFreq, &toneGain, &toneQ, &outputGain })
        control->slider.setBounds (bounds.removeFromLeft (knobWidth).withHeight (knobHeight));
}

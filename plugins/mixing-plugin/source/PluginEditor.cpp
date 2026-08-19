#include "PluginEditor.h"

#include <dsp/Biquad.h>
#include <dsp/FilterResponse.h>

using namespace ui;

namespace
{
    constexpr int headerHeight  = 30;
    constexpr int controlHeight = 82;
    constexpr int margin        = 10;
    constexpr int knobSize      = 46;

    // The curve is drawn against the plugin's own sample rate when it is
    // running, but the editor may open before prepareToPlay. 48k keeps the
    // display honest in the meantime — the visible difference across common
    // rates is well under a tenth of a dB below 15 kHz.
    constexpr double displaySampleRate = 48000.0;
}

MixingPluginEditor::AttachedKnob::AttachedKnob (juce::AudioProcessorValueTreeState& state,
                                                const juce::String& parameterID,
                                                const juce::String& captionText,
                                                const juce::String& suffix,
                                                juce::LookAndFeel& lookAndFeel,
                                                juce::Component& parent)
    : attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    slider.setTextValueSuffix (suffix);
    slider.setLookAndFeel (&lookAndFeel);
    slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    parent.addAndMakeVisible (slider);

    caption.setText (captionText, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (theme::labelFont (10.0f));
    caption.setColour (juce::Label::textColourId, theme::textDim);
    parent.addAndMakeVisible (caption);
}

void MixingPluginEditor::AttachedKnob::setBounds (juce::Rectangle<int> area)
{
    caption.setBounds (area.removeFromTop (13));
    slider.setBounds (area);
}

MixingPluginEditor::MixingPluginEditor (MixingPluginProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      inputGain  (p.apvts, ParamID::inputGain,  "INPUT",  " dB", ioLook, *this),
      toneFreq   (p.apvts, ParamID::toneFreq,   "FREQ",   " Hz", bandLook, *this),
      toneGain   (p.apvts, ParamID::toneGain,   "GAIN",   " dB", bandGainLook, *this),
      toneQ      (p.apvts, ParamID::toneQ,      "Q",      "",    bandLook, *this),
      outputGain (p.apvts, ParamID::outputGain, "OUTPUT", " dB", ioLook, *this),
      bypassAttachment (p.apvts, ParamID::bypass, bypassButton)
{
    ioLook.setBipolar (true);        // input/output gain are ± around unity
    bandGainLook.setBipolar (true);  // band gain is a boost/cut around flat

    addAndMakeVisible (curveDisplay);
    curveDisplay.setDecibelRange (-18.0f, 18.0f);
    curveDisplay.setResponseFunction ([this] (float hz) { return responseDbAt (hz); });
    updateNodes();

    bypassButton.setLookAndFeel (&ioLook);
    addAndMakeVisible (bypassButton);

    setSize (660, 400);
    startTimerHz (30);
}

MixingPluginEditor::~MixingPluginEditor()
{
    // Every component using a custom LookAndFeel must drop it before the
    // LookAndFeel is destroyed, or JUCE asserts on the dangling pointer.
    for (auto* knob : { &inputGain, &toneFreq, &toneGain, &toneQ, &outputGain })
        knob->slider.setLookAndFeel (nullptr);

    bypassButton.setLookAndFeel (nullptr);
}

float MixingPluginEditor::responseDbAt (float frequencyHz) const
{
    auto& apvts = processorRef.apvts;

    const auto freq = apvts.getRawParameterValue (ParamID::toneFreq)->load();
    const auto gain = apvts.getRawParameterValue (ParamID::toneGain)->load();
    const auto q    = apvts.getRawParameterValue (ParamID::toneQ)->load();
    const auto in   = apvts.getRawParameterValue (ParamID::inputGain)->load();
    const auto out  = apvts.getRawParameterValue (ParamID::outputGain)->load();

    const auto coefficients = dsp::BiquadCoefficients::makePeak (displaySampleRate, freq, q, gain);

    return dsp::magnitudeDb (coefficients, frequencyHz, displaySampleRate) + in + out;
}

void MixingPluginEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;

    // Cheap change detection. Summing the parameters is enough to notice any
    // movement without keeping five separate cached values in sync.
    const auto signature =
          apvts.getRawParameterValue (ParamID::toneFreq)->load()   * 1.0f
        + apvts.getRawParameterValue (ParamID::toneGain)->load()   * 7.0f
        + apvts.getRawParameterValue (ParamID::toneQ)->load()      * 31.0f
        + apvts.getRawParameterValue (ParamID::inputGain)->load()  * 127.0f
        + apvts.getRawParameterValue (ParamID::outputGain)->load() * 511.0f;

    if (! juce::approximatelyEqual (signature, lastResponseSignature))
    {
        lastResponseSignature = signature;
        updateNodes();
        curveDisplay.repaint();
    }
}

void MixingPluginEditor::updateNodes()
{
    auto& apvts = processorRef.apvts;

    ui::FrequencyCurveDisplay::BandNode band;
    band.frequencyHz = apvts.getRawParameterValue (ParamID::toneFreq)->load();

    // Placed on the composite curve rather than at the band's own gain, so the
    // handle stays welded to the line when broadband gain shifts it. This is
    // also the behaviour that generalises: with several bands, each handle sits
    // where the summed curve actually is at that frequency.
    band.gainDb      = responseDbAt (band.frequencyHz);
    band.colour      = theme::bandColour (0);

    curveDisplay.setNodes ({ band });
}

void MixingPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);

    g.setColour (theme::text);
    g.setFont (theme::titleFont (13.0f));
    g.drawText (processorRef.getName().toUpperCase(),
                header, juce::Justification::centredLeft);

    // A hairline under the header separates chrome from content without
    // spending a whole panel on it.
    g.setColour (theme::border);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    const auto controlTop = getHeight() - controlHeight;
    g.drawHorizontalLine (controlTop, 0.0f, (float) getWidth());

    g.setColour (theme::panel);
    g.fillRect (0, controlTop + 1, getWidth(), controlHeight - 1);
}

void MixingPluginEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 5);
    bypassButton.setBounds (header.removeFromRight (66));

    auto controls = bounds.removeFromBottom (controlHeight).reduced (margin, 8);

    curveDisplay.setBounds (bounds.reduced (margin, margin - 2));

    // Band controls grouped on the left, I/O on the right — the grouping the
    // signal flow implies, rather than one undifferentiated row.
    const auto slot = knobSize + 18;

    auto bandArea = controls.removeFromLeft (slot * 3);
    toneFreq.setBounds (bandArea.removeFromLeft (slot));
    toneGain.setBounds (bandArea.removeFromLeft (slot));
    toneQ   .setBounds (bandArea.removeFromLeft (slot));

    auto ioArea = controls.removeFromRight (slot * 2);
    inputGain .setBounds (ioArea.removeFromLeft (slot));
    outputGain.setBounds (ioArea.removeFromLeft (slot));
}

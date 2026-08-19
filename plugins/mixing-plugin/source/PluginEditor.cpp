#include "PluginEditor.h"

#include <dsp/Biquad.h>
#include <dsp/FilterResponse.h>

using namespace ui;

namespace
{
    constexpr int headerHeight  = 30;
    constexpr int controlHeight = 88;
    constexpr int margin        = 10;
    constexpr int knobSlot      = 70;
    constexpr int badgeWidth    = 58;

    // The curve is drawn against the plugin's own sample rate when it is
    // running, but the editor may open before prepareToPlay. 48k keeps the
    // display honest in the meantime — the visible difference across common
    // rates is well under a tenth of a dB below 15 kHz.
    constexpr double displaySampleRate = 48000.0;
}

MixingPluginEditor::Knob::Knob (const juce::String& captionText,
                                const juce::String& suffix,
                                juce::LookAndFeel& lookAndFeel,
                                juce::Component& parent)
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

void MixingPluginEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    caption.setBounds (area.removeFromTop (13));
    slider.setBounds (area);
}

MixingPluginEditor::MixingPluginEditor (MixingPluginProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      freqKnob   ("FREQ",   " Hz", bandLook,     *this),
      gainKnob   ("GAIN",   " dB", bandGainLook, *this),
      qKnob      ("Q",      "",    bandLook,     *this),
      inputKnob  ("INPUT",  " dB", ioLook,       *this),
      outputKnob ("OUTPUT", " dB", ioLook,       *this)
{
    ioLook.setBipolar (true);        // input/output gain are ± around unity
    bandGainLook.setBipolar (true);  // band gain is a boost/cut around flat

    inputAttachment  = std::make_unique<SliderAttachment> (p.apvts, ParamID::inputGain,  inputKnob.slider);
    outputAttachment = std::make_unique<SliderAttachment> (p.apvts, ParamID::outputGain, outputKnob.slider);
    bypassAttachment = std::make_unique<ButtonAttachment> (p.apvts, ParamID::bypass,     bypassButton);

    bandOnButton.setLookAndFeel (&bandLook);
    bypassButton.setLookAndFeel (&ioLook);
    addAndMakeVisible (bandOnButton);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (curveDisplay);
    curveDisplay.setDecibelRange (-18.0f, 18.0f);
    curveDisplay.setSpectrumRange (-90.0f, 0.0f);

    curveDisplay.setResponseFunction ([this] (float hz) { return responseDbAt (hz); });

    curveDisplay.setSpectrumFunction ([this] (float lowHz, float highHz)
    {
        return processorRef.analyser.magnitudeDbForRange (lowHz, highHz);
    });

    curveDisplay.setNodeDragCallback ([this] (int band, float frequencyHz, float gainDb)
    {
        setParameter (ParamID::bandFreq (band), frequencyHz);
        setParameter (ParamID::bandGain (band), gainDb - broadbandOffsetDb());
    });

    curveDisplay.setNodeGestureCallback ([this] (int band, bool starting)
    {
        setBandGesture (band, starting);
    });

    curveDisplay.setNodeSelectCallback ([this] (int band) { selectBand (band); });
    curveDisplay.setAddBandCallback    ([this] (float hz, float db) { addBandAt (hz, db); });
    curveDisplay.setRemoveBandCallback ([this] (int band) { removeBand (band); });

    selectBand (0);

    setSize (720, 420);
    startTimerHz (30);
}

MixingPluginEditor::~MixingPluginEditor()
{
    // Attachments must die before the components they reference.
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    bandOnAttachment.reset();
    inputAttachment.reset();
    outputAttachment.reset();
    bypassAttachment.reset();

    // Every component using a custom LookAndFeel must drop it before the
    // LookAndFeel is destroyed, or JUCE asserts on the dangling pointer.
    for (auto* knob : { &freqKnob, &gainKnob, &qKnob, &inputKnob, &outputKnob })
        knob->slider.setLookAndFeel (nullptr);

    bandOnButton.setLookAndFeel (nullptr);
    bypassButton.setLookAndFeel (nullptr);
}

// ── Band management ─────────────────────────────────────────────────────

void MixingPluginEditor::selectBand (int band)
{
    selectedBand = juce::jlimit (0, numBands - 1, band);

    const auto accent = theme::bandColour (selectedBand);
    bandLook.setAccent (accent);
    bandGainLook.setAccent (accent);

    // Rebuilt rather than retargeted: a SliderAttachment binds one parameter
    // for its lifetime. Reset first, or two attachments briefly drive the same
    // slider and fight each other.
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    bandOnAttachment.reset();

    auto& apvts = processorRef.apvts;
    freqAttachment   = std::make_unique<SliderAttachment> (apvts, ParamID::bandFreq (selectedBand), freqKnob.slider);
    gainAttachment   = std::make_unique<SliderAttachment> (apvts, ParamID::bandGain (selectedBand), gainKnob.slider);
    qAttachment      = std::make_unique<SliderAttachment> (apvts, ParamID::bandQ    (selectedBand), qKnob.slider);
    bandOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamID::bandOn   (selectedBand), bandOnButton);

    updateNodes();
    repaint();
}

bool MixingPluginEditor::isBandOn (int band) const
{
    return processorRef.apvts.getRawParameterValue (ParamID::bandOn (band))->load() > 0.5f;
}

void MixingPluginEditor::addBandAt (float frequencyHz, float gainDb)
{
    // First band that is currently off. With all six in use a double-click does
    // nothing, which is preferable to silently stealing someone's band.
    for (int band = 0; band < numBands; ++band)
    {
        if (isBandOn (band))
            continue;

        setParameter (ParamID::bandFreq (band), frequencyHz);
        setParameter (ParamID::bandGain (band), gainDb - broadbandOffsetDb());
        setParameter (ParamID::bandQ    (band), 1.0f);
        setParameter (ParamID::bandOn   (band), 1.0f);

        selectBand (band);
        return;
    }
}

void MixingPluginEditor::removeBand (int band)
{
    setParameter (ParamID::bandOn (band), 0.0f);
    updateNodes();
    repaint();
}

// ── Response ────────────────────────────────────────────────────────────

float MixingPluginEditor::bandResponseDbAt (int band, float frequencyHz) const
{
    if (! isBandOn (band))
        return 0.0f;

    auto& apvts = processorRef.apvts;

    const auto frequency = apvts.getRawParameterValue (ParamID::bandFreq (band))->load();
    const auto gain      = apvts.getRawParameterValue (ParamID::bandGain (band))->load();
    const auto q         = apvts.getRawParameterValue (ParamID::bandQ    (band))->load();

    const auto coefficients = dsp::BiquadCoefficients::makePeak (displaySampleRate,
                                                                 frequency, q, gain);

    return dsp::magnitudeDb (coefficients, frequencyHz, displaySampleRate);
}

float MixingPluginEditor::broadbandOffsetDb() const
{
    auto& apvts = processorRef.apvts;

    return apvts.getRawParameterValue (ParamID::inputGain)->load()
         + apvts.getRawParameterValue (ParamID::outputGain)->load();
}

float MixingPluginEditor::responseDbAt (float frequencyHz) const
{
    // Bands are in series, so their responses multiply — a sum in dB.
    auto total = broadbandOffsetDb();

    for (int band = 0; band < numBands; ++band)
        total += bandResponseDbAt (band, frequencyHz);

    return total;
}

void MixingPluginEditor::updateNodes()
{
    auto& apvts = processorRef.apvts;

    std::vector<ui::FrequencyCurveDisplay::BandNode> nodes;
    nodes.reserve ((size_t) numBands);

    for (int band = 0; band < numBands; ++band)
    {
        ui::FrequencyCurveDisplay::BandNode node;

        node.frequencyHz = apvts.getRawParameterValue (ParamID::bandFreq (band))->load();
        node.colour      = theme::bandColour (band);
        node.active      = isBandOn (band);
        node.selected    = node.active && (band == selectedBand);
        node.draggable   = node.active;

        // Placed on the composite curve rather than at the band's own gain, so
        // the handle stays welded to the line: with several bands overlapping,
        // a band's own gain is not where the curve actually is.
        node.gainDb = responseDbAt (node.frequencyHz);

        if (node.active)
        {
            node.response = [this, band] (float hz)
            {
                return bandResponseDbAt (band, hz) + broadbandOffsetDb();
            };
        }

        nodes.push_back (std::move (node));
    }

    curveDisplay.setNodes (std::move (nodes));
}

// ── Parameter plumbing ──────────────────────────────────────────────────

void MixingPluginEditor::setParameter (juce::StringRef parameterID, float value)
{
    if (auto* parameter = processorRef.apvts.getParameter (parameterID))
    {
        const auto normalised = parameter->getNormalisableRange().convertTo0to1 (value);
        parameter->setValueNotifyingHost (normalised);
    }
}

void MixingPluginEditor::setBandGesture (int band, bool starting)
{
    // Hosts need an explicit gesture around a drag or the automation lane
    // records a smear of individual values instead of one coherent move.
    for (const auto& parameterID : { ParamID::bandFreq (band), ParamID::bandGain (band) })
    {
        if (auto* parameter = processorRef.apvts.getParameter (parameterID))
        {
            if (starting) parameter->beginChangeGesture();
            else          parameter->endChangeGesture();
        }
    }
}

// ── Frame ───────────────────────────────────────────────────────────────

void MixingPluginEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;

    // Cheap change detection. Weighting each value differently means two
    // parameters moving in opposite directions cannot cancel out.
    auto signature = apvts.getRawParameterValue (ParamID::inputGain)->load()  * 1.7f
                   + apvts.getRawParameterValue (ParamID::outputGain)->load() * 3.1f;

    for (int band = 0; band < numBands; ++band)
    {
        const auto weight = (float) (band + 2);

        signature += apvts.getRawParameterValue (ParamID::bandFreq (band))->load() * weight
                   + apvts.getRawParameterValue (ParamID::bandGain (band))->load() * weight * 13.0f
                   + apvts.getRawParameterValue (ParamID::bandQ    (band))->load() * weight * 101.0f
                   + apvts.getRawParameterValue (ParamID::bandOn   (band))->load() * weight * 997.0f;
    }

    const auto parametersMoved = ! juce::approximatelyEqual (signature, lastResponseSignature);

    if (parametersMoved)
    {
        lastResponseSignature = signature;
        updateNodes();
        repaint();          // the band badge reflects the on/off state too
    }

    // update() drains the FIFO and runs the FFT on this thread. It returns
    // false when nothing changed and everything has already decayed to the
    // floor, which is what keeps an idle editor at zero CPU.
    const auto spectrumMoved = processorRef.analyser.update();

    if (parametersMoved || spectrumMoved)
        curveDisplay.repaint();
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

    // Which band the controls are driving. Without this the knobs are
    // ambiguous the moment there is more than one band.
    const auto accent = theme::bandColour (selectedBand);
    const auto on = isBandOn (selectedBand);
    const juce::Rectangle<int> badge { margin, controlTop + 30, badgeWidth, 18 };

    g.setColour (accent.withAlpha (on ? 0.22f : 0.07f));
    g.fillRoundedRectangle (badge.toFloat(), 3.0f);

    g.setColour (on ? accent : theme::textFaint);
    g.setFont (theme::labelFont (10.0f));
    g.drawText ("BAND " + juce::String (selectedBand + 1), badge, juce::Justification::centred);

    // Discoverability: double-click is not guessable, and an EQ where you
    // cannot find how to add a band reads as broken.
    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("double-click to add or remove a band",
                juce::Rectangle<int> (margin, getHeight() - 16, getWidth() - margin * 2, 12),
                juce::Justification::centredRight);
}

void MixingPluginEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 5);
    bypassButton.setBounds (header.removeFromRight (66));

    auto controls = bounds.removeFromBottom (controlHeight).reduced (margin, 8);
    controls.removeFromBottom (10);   // room for the hint line

    curveDisplay.setBounds (bounds.reduced (margin, margin - 2));

    // Selected-band controls on the left after the band badge, I/O on the
    // right — the grouping the signal flow implies.
    controls.removeFromLeft (badgeWidth + 8);

    bandOnButton.setBounds (controls.removeFromLeft (38).withSizeKeepingCentre (34, 20));
    controls.removeFromLeft (8);

    freqKnob.setBounds (controls.removeFromLeft (knobSlot));
    gainKnob.setBounds (controls.removeFromLeft (knobSlot));
    qKnob   .setBounds (controls.removeFromLeft (knobSlot));

    auto ioArea = controls.removeFromRight (knobSlot * 2);
    inputKnob .setBounds (ioArea.removeFromLeft (knobSlot));
    outputKnob.setBounds (ioArea.removeFromLeft (knobSlot));
}

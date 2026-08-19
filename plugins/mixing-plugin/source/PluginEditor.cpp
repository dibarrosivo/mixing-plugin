#include "PluginEditor.h"

#include <dsp/Biquad.h>
#include <dsp/FilterResponse.h>

using namespace ui;

namespace
{
    constexpr int headerHeight  = 30;
    constexpr int controlHeight = 168;   // two rows: band, then dynamics
    constexpr int rowHeight     = 74;
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
      outputKnob ("OUTPUT", " dB", ioLook,       *this),
      thresholdKnob ("THRESH",  " dB", bandLook, *this),
      ratioKnob     ("RATIO",   ":1",  bandLook, *this),
      attackKnob    ("ATTACK",  " ms", bandLook, *this),
      releaseKnob   ("RELEASE", " ms", bandLook, *this)
{
    ioLook.setBipolar (true);        // input/output gain are ± around unity
    bandGainLook.setBipolar (true);  // band gain is a boost/cut around flat

    inputAttachment  = std::make_unique<SliderAttachment> (p.apvts, ParamID::inputGain,  inputKnob.slider);
    outputAttachment = std::make_unique<SliderAttachment> (p.apvts, ParamID::outputGain, outputKnob.slider);
    bypassAttachment = std::make_unique<ButtonAttachment> (p.apvts, ParamID::bypass,     bypassButton);

    bandOnButton.setLookAndFeel (&bandLook);
    bandDynButton.setLookAndFeel (&bandLook);
    bypassButton.setLookAndFeel (&ioLook);
    addAndMakeVisible (bandOnButton);
    addAndMakeVisible (bandDynButton);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (curveDisplay);
    curveDisplay.setDecibelRange (-18.0f, 18.0f);
    curveDisplay.setSpectrumRange (-90.0f, 0.0f);

    curveDisplay.setResponseFunction ([this] (float hz) { return responseDbAt (hz); });
    curveDisplay.setStaticResponseFunction ([this] (float hz) { return staticResponseDbAt (hz); });

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

    setSize (720, 500);
    startTimerHz (30);
}

MixingPluginEditor::~MixingPluginEditor()
{
    // Attachments must die before the components they reference.
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    thresholdAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    bandOnAttachment.reset();
    bandDynAttachment.reset();
    inputAttachment.reset();
    outputAttachment.reset();
    bypassAttachment.reset();

    // Every component using a custom LookAndFeel must drop it before the
    // LookAndFeel is destroyed, or JUCE asserts on the dangling pointer.
    for (auto* knob : { &freqKnob, &gainKnob, &qKnob, &inputKnob, &outputKnob,
                        &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob })
        knob->slider.setLookAndFeel (nullptr);

    bandOnButton.setLookAndFeel (nullptr);
    bandDynButton.setLookAndFeel (nullptr);
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
    thresholdAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    bandOnAttachment.reset();
    bandDynAttachment.reset();

    auto& apvts = processorRef.apvts;
    freqAttachment      = std::make_unique<SliderAttachment> (apvts, ParamID::bandFreq (selectedBand), freqKnob.slider);
    gainAttachment      = std::make_unique<SliderAttachment> (apvts, ParamID::bandGain (selectedBand), gainKnob.slider);
    qAttachment         = std::make_unique<SliderAttachment> (apvts, ParamID::bandQ    (selectedBand), qKnob.slider);
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ParamID::bandThreshold (selectedBand), thresholdKnob.slider);
    ratioAttachment     = std::make_unique<SliderAttachment> (apvts, ParamID::bandRatio (selectedBand), ratioKnob.slider);
    attackAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::bandAttack (selectedBand), attackKnob.slider);
    releaseAttachment   = std::make_unique<SliderAttachment> (apvts, ParamID::bandRelease (selectedBand), releaseKnob.slider);
    bandOnAttachment    = std::make_unique<ButtonAttachment> (apvts, ParamID::bandOn  (selectedBand), bandOnButton);
    bandDynAttachment   = std::make_unique<ButtonAttachment> (apvts, ParamID::bandDyn (selectedBand), bandDynButton);

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

bool MixingPluginEditor::isBandDynamic (int band) const
{
    return processorRef.apvts.getRawParameterValue (ParamID::bandDyn (band))->load() > 0.5f;
}

float MixingPluginEditor::bandResponseDbAt (int band, float frequencyHz,
                                            bool includeDynamics) const
{
    if (! isBandOn (band))
        return 0.0f;

    auto& apvts = processorRef.apvts;

    const auto frequency = apvts.getRawParameterValue (ParamID::bandFreq (band))->load();
    const auto q         = apvts.getRawParameterValue (ParamID::bandQ    (band))->load();
    auto       gain      = apvts.getRawParameterValue (ParamID::bandGain (band))->load();

    // The live curve adds the reduction the DSP is applying right now. Note
    // this is read from the audio thread's atomic, so it is a snapshot — the
    // curve is a picture of the last block, not a prediction.
    if (includeDynamics && isBandDynamic (band))
        gain += processorRef.getBandReductionDb (band);

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
        total += bandResponseDbAt (band, frequencyHz, true);

    return total;
}

float MixingPluginEditor::staticResponseDbAt (float frequencyHz) const
{
    auto total = broadbandOffsetDb();

    for (int band = 0; band < numBands; ++band)
        total += bandResponseDbAt (band, frequencyHz, false);

    return total;
}

void MixingPluginEditor::updateNodes()
{
    auto& apvts = processorRef.apvts;

    std::vector<ui::FrequencyCurveDisplay::BandNode> nodes;
    nodes.reserve ((size_t) numBands);

    for (int band = 0; band < numBands; ++band)
    {
        // Disabled bands contribute no handle at all. Six grey ghosts parked on
        // the zero line read as a broken display rather than an empty one.
        if (! isBandOn (band))
            continue;

        ui::FrequencyCurveDisplay::BandNode node;

        node.id          = band;
        node.frequencyHz = apvts.getRawParameterValue (ParamID::bandFreq (band))->load();
        node.colour      = theme::bandColour (band);
        node.active      = true;
        node.selected    = (band == selectedBand);
        node.draggable   = true;

        // Placed on the composite curve rather than at the band's own gain, so
        // the handle stays welded to the line: with several bands overlapping,
        // a band's own gain is not where the curve actually is.
        node.gainDb = responseDbAt (node.frequencyHz);

        node.response = [this, band] (float hz)
        {
            return bandResponseDbAt (band, hz, true) + broadbandOffsetDb();
        };

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

    // Gain reduction moves with the audio, not with the parameters, so the
    // signature above will never see it. 0.05 dB is below what is visible on
    // the curve, which keeps a static band from repainting on detector noise.
    auto reductionMoved = false;

    for (int band = 0; band < numBands; ++band)
    {
        const auto reduction = isBandDynamic (band) ? processorRef.getBandReductionDb (band)
                                                    : 0.0f;

        if (std::abs (reduction - lastReductionDb[(size_t) band]) > 0.05f)
        {
            lastReductionDb[(size_t) band] = reduction;
            reductionMoved = true;
        }
    }

    if (reductionMoved)
    {
        updateNodes();
        repaint();          // the reduction meter lives in the control strip
    }

    // update() drains the FIFO and runs the FFT on this thread. It returns
    // false when nothing changed and everything has already decayed to the
    // floor, which is what keeps an idle editor at zero CPU.
    const auto spectrumMoved = processorRef.analyser.update();

    if (parametersMoved || spectrumMoved || reductionMoved)
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
    const juce::Rectangle<int> badge { margin, controlTop + 26, badgeWidth, 18 };

    g.setColour (accent.withAlpha (on ? 0.22f : 0.07f));
    g.fillRoundedRectangle (badge.toFloat(), 3.0f);

    g.setColour (on ? accent : theme::textFaint);
    g.setFont (theme::labelFont (10.0f));
    g.drawText ("BAND " + juce::String (selectedBand + 1), badge, juce::Justification::centred);

    paintReductionMeter (g, { getWidth() - margin - 150, controlTop + rowHeight + 26, 150, 16 });

    // Discoverability: double-click is not guessable, and an EQ where you
    // cannot find how to add a band reads as broken.
    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("double-click to add or remove a band",
                juce::Rectangle<int> (margin, getHeight() - 16, getWidth() - margin * 2, 12),
                juce::Justification::centredRight);
}

void MixingPluginEditor::paintReductionMeter (juce::Graphics& g,
                                              juce::Rectangle<int> area) const
{
    constexpr float fullScaleDb = 18.0f;

    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("GR", area.removeFromLeft (18), juce::Justification::centredLeft);

    auto track = area.removeFromLeft (area.getWidth() - 40).toFloat();

    g.setColour (theme::displayBackground);
    g.fillRoundedRectangle (track, 2.0f);

    if (isBandDynamic (selectedBand) && isBandOn (selectedBand))
    {
        const auto reduction = processorRef.getBandReductionDb (selectedBand);
        const auto proportion = juce::jlimit (0.0f, 1.0f, -reduction / fullScaleDb);

        // Filling right-to-left, because reduction pulls the level DOWN. A bar
        // that grows rightward reads as "more output", which is backwards.
        auto filled = track.withTrimmedLeft (track.getWidth() * (1.0f - proportion));

        g.setColour (theme::bandColour (selectedBand).withAlpha (0.85f));
        g.fillRoundedRectangle (filled, 2.0f);

        g.setColour (theme::textDim);
        g.drawText (juce::String (reduction, 1) + " dB",
                    area.withTrimmedLeft (4), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (theme::textFaint);
        g.drawText ("--", area.withTrimmedLeft (4), juce::Justification::centredLeft);
    }
}

void MixingPluginEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 5);
    bypassButton.setBounds (header.removeFromRight (66));

    auto controls = bounds.removeFromBottom (controlHeight).reduced (margin, 8);
    controls.removeFromBottom (10);   // room for the hint line

    curveDisplay.setBounds (bounds.reduced (margin, margin - 2));

    // Row one: what the band IS. Row two: how it REACTS. Splitting them means
    // the static controls stay in the same place whether dynamics are on or
    // off, so muscle memory survives toggling DYN.
    auto bandRow = controls.removeFromTop (rowHeight);
    auto dynRow  = controls;

    bandRow.removeFromLeft (badgeWidth + 8);
    bandOnButton.setBounds (bandRow.removeFromLeft (38).withSizeKeepingCentre (34, 20));
    bandRow.removeFromLeft (8);

    freqKnob.setBounds (bandRow.removeFromLeft (knobSlot));
    gainKnob.setBounds (bandRow.removeFromLeft (knobSlot));
    qKnob   .setBounds (bandRow.removeFromLeft (knobSlot));

    auto ioArea = bandRow.removeFromRight (knobSlot * 2);
    inputKnob .setBounds (ioArea.removeFromLeft (knobSlot));
    outputKnob.setBounds (ioArea.removeFromLeft (knobSlot));

    dynRow.removeFromLeft (badgeWidth + 8);
    bandDynButton.setBounds (dynRow.removeFromLeft (38).withSizeKeepingCentre (34, 20));
    dynRow.removeFromLeft (8);

    thresholdKnob.setBounds (dynRow.removeFromLeft (knobSlot));
    ratioKnob    .setBounds (dynRow.removeFromLeft (knobSlot));
    attackKnob   .setBounds (dynRow.removeFromLeft (knobSlot));
    releaseKnob  .setBounds (dynRow.removeFromLeft (knobSlot));
}

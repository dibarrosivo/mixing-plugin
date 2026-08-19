#include "PluginEditor.h"

using namespace ui;

namespace
{
    constexpr int headerHeight  = 30;
    constexpr int controlHeight = 168;
    constexpr int rowHeight     = 74;
    constexpr int margin        = 10;
    constexpr int knobSlot      = 70;
    constexpr int badgeWidth    = 58;
}

MultibandEditor::Knob::Knob (const juce::String& captionText,
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

void MultibandEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    caption.setBounds (area.removeFromTop (13));
    slider.setBounds (area);
}

MultibandEditor::MultibandEditor (MultibandProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      thresholdKnob ("THRESH",  " dB", bandLook,   *this),
      ratioKnob     ("RATIO",   ":1",  bandLook,   *this),
      attackKnob    ("ATTACK",  " ms", bandLook,   *this),
      releaseKnob   ("RELEASE", " ms", bandLook,   *this),
      makeupKnob    ("MAKEUP",  " dB", makeupLook, *this),
      inputKnob     ("INPUT",   " dB", ioLook,     *this),
      outputKnob    ("OUTPUT",  " dB", ioLook,     *this)
{
    ioLook.setBipolar (true);
    makeupLook.setBipolar (true);

    inputAttachment  = std::make_unique<SliderAttachment> (p.apvts, ParamID::inputGain,  inputKnob.slider);
    outputAttachment = std::make_unique<SliderAttachment> (p.apvts, ParamID::outputGain, outputKnob.slider);
    bypassAttachment = std::make_unique<ButtonAttachment> (p.apvts, ParamID::bypass,     bypassButton);

    for (auto* button : { &bandOnButton, &muteButton, &soloButton })
    {
        button->setLookAndFeel (&bandLook);
        addAndMakeVisible (*button);
    }

    bypassButton.setLookAndFeel (&ioLook);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (display);
    // Asymmetric on purpose: a compressor only ever pulls down, so a symmetric
    // range wastes the whole top half on nothing. A little headroom above zero
    // is kept so makeup gain has somewhere to show.
    display.setDecibelRange (-24.0f, 6.0f);
    display.setSpectrumRange (-90.0f, 0.0f);

    display.setSpectrumFunction ([this] (float lowHz, float highHz)
    {
        return processorRef.analyser.magnitudeDbForRange (lowHz, highHz);
    });

    display.setRegionSelectCallback ([this] (int band) { selectBand (band); });

    display.setCrossoverDragCallback ([this] (int index, float frequencyHz)
    {
        setParameter (ParamID::crossover (index), frequencyHz);
    });

    display.setCrossoverGestureCallback ([this] (int index, bool starting)
    {
        setCrossoverGesture (index, starting);
    });

    selectBand (0);
    updateRegions();

    setSize (760, 520);
    startTimerHz (30);
}

MultibandEditor::~MultibandEditor()
{
    thresholdAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    makeupAttachment.reset();
    bandOnAttachment.reset();
    muteAttachment.reset();
    soloAttachment.reset();
    inputAttachment.reset();
    outputAttachment.reset();
    bypassAttachment.reset();

    for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob,
                        &makeupKnob, &inputKnob, &outputKnob })
        knob->slider.setLookAndFeel (nullptr);

    for (auto* button : { &bandOnButton, &muteButton, &soloButton, &bypassButton })
        button->setLookAndFeel (nullptr);
}

// ── Band selection ──────────────────────────────────────────────────────

void MultibandEditor::selectBand (int band)
{
    selectedBand = juce::jlimit (0, numBands - 1, band);

    const auto accent = theme::bandColour (selectedBand);
    bandLook.setAccent (accent);
    makeupLook.setAccent (accent);

    // Rebuilt rather than retargeted: an attachment binds one parameter for
    // its lifetime. Reset first or two of them briefly drive the same control.
    thresholdAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    makeupAttachment.reset();
    bandOnAttachment.reset();
    muteAttachment.reset();
    soloAttachment.reset();

    auto& apvts = processorRef.apvts;
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ParamID::bandThreshold (selectedBand), thresholdKnob.slider);
    ratioAttachment     = std::make_unique<SliderAttachment> (apvts, ParamID::bandRatio (selectedBand),     ratioKnob.slider);
    attackAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::bandAttack (selectedBand),    attackKnob.slider);
    releaseAttachment   = std::make_unique<SliderAttachment> (apvts, ParamID::bandRelease (selectedBand),   releaseKnob.slider);
    makeupAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::bandMakeup (selectedBand),    makeupKnob.slider);
    bandOnAttachment    = std::make_unique<ButtonAttachment> (apvts, ParamID::bandOn (selectedBand),        bandOnButton);
    muteAttachment      = std::make_unique<ButtonAttachment> (apvts, ParamID::bandMute (selectedBand),      muteButton);
    soloAttachment      = std::make_unique<ButtonAttachment> (apvts, ParamID::bandSolo (selectedBand),      soloButton);

    updateRegions();
    repaint();
}

bool MultibandEditor::isBandOn (int band) const
{
    return processorRef.apvts.getRawParameterValue (ParamID::bandOn (band))->load() > 0.5f;
}

bool MultibandEditor::isBandMuted (int band) const
{
    return processorRef.apvts.getRawParameterValue (ParamID::bandMute (band))->load() > 0.5f;
}

bool MultibandEditor::anySoloed() const
{
    for (int band = 0; band < numBands; ++band)
        if (processorRef.apvts.getRawParameterValue (ParamID::bandSolo (band))->load() > 0.5f)
            return true;

    return false;
}

void MultibandEditor::updateRegions()
{
    auto& apvts = processorRef.apvts;
    const auto soloActive = anySoloed();

    std::vector<ui::FrequencyCurveDisplay::BandRegion> regions;
    std::vector<float> crossovers;

    regions.reserve ((size_t) numBands);
    crossovers.reserve ((size_t) numCrossovers);

    // Drawn from what the DSP is actually using, not from the raw parameters.
    // A divider dragged past its neighbour gets clamped, and the display has to
    // show where it really ended up or the picture lies about the audio.
    for (int i = 0; i < numCrossovers; ++i)
        crossovers.push_back (processorRef.getEffectiveCrossoverHz (i));

    for (int band = 0; band < numBands; ++band)
    {
        ui::FrequencyCurveDisplay::BandRegion region;

        region.id     = band;
        region.lowHz  = (band == 0) ? 20.0f : crossovers[(size_t) band - 1];
        region.highHz = (band == numBands - 1) ? 20000.0f : crossovers[(size_t) band];
        region.colour = theme::bandColour (band);
        region.selected = (band == selectedBand);
        region.muted    = isBandMuted (band);
        region.dimmed   = soloActive
                       && apvts.getRawParameterValue (ParamID::bandSolo (band))->load() <= 0.5f;

        region.reductionDb = isBandOn (band) ? processorRef.getBandReductionDb (band) : 0.0f;

        regions.push_back (region);
    }

    display.setCrossoverFrequencies (std::move (crossovers));
    display.setBandRegions (std::move (regions));
}

// ── Parameter plumbing ──────────────────────────────────────────────────

void MultibandEditor::setParameter (juce::StringRef parameterID, float value)
{
    if (auto* parameter = processorRef.apvts.getParameter (parameterID))
        parameter->setValueNotifyingHost (
            parameter->getNormalisableRange().convertTo0to1 (value));
}

void MultibandEditor::setCrossoverGesture (int index, bool starting)
{
    if (auto* parameter = processorRef.apvts.getParameter (ParamID::crossover (index)))
    {
        if (starting) parameter->beginChangeGesture();
        else          parameter->endChangeGesture();
    }
}

// ── Frame ───────────────────────────────────────────────────────────────

void MultibandEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;

    auto signature = apvts.getRawParameterValue (ParamID::inputGain)->load()  * 1.7f
                   + apvts.getRawParameterValue (ParamID::outputGain)->load() * 3.1f;

    for (int i = 0; i < numCrossovers; ++i)
        signature += apvts.getRawParameterValue (ParamID::crossover (i))->load() * (float) (i + 2);

    for (int band = 0; band < numBands; ++band)
    {
        const auto weight = (float) (band + 2);

        signature += apvts.getRawParameterValue (ParamID::bandOn (band))->load()   * weight * 13.0f
                   + apvts.getRawParameterValue (ParamID::bandMute (band))->load() * weight * 101.0f
                   + apvts.getRawParameterValue (ParamID::bandSolo (band))->load() * weight * 997.0f;
    }

    auto changed = ! juce::approximatelyEqual (signature, lastParameterSignature);

    if (changed)
        lastParameterSignature = signature;

    // Gain reduction moves with the audio, so the parameter signature above
    // will never see it.
    for (int band = 0; band < numBands; ++band)
    {
        const auto reduction = isBandOn (band) ? processorRef.getBandReductionDb (band) : 0.0f;

        if (std::abs (reduction - lastReductionDb[(size_t) band]) > 0.05f)
        {
            lastReductionDb[(size_t) band] = reduction;
            changed = true;
        }
    }

    const auto spectrumMoved = processorRef.analyser.update();

    if (changed)
    {
        updateRegions();
        repaint();
    }
    else if (spectrumMoved)
    {
        display.repaint();
    }
}

void MultibandEditor::paintReductionMeter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    constexpr float fullScaleDb = 24.0f;

    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("GR", area.removeFromLeft (18), juce::Justification::centredLeft);

    auto track = area.removeFromLeft (area.getWidth() - 44).toFloat();

    g.setColour (theme::displayBackground);
    g.fillRoundedRectangle (track, 2.0f);

    if (isBandOn (selectedBand))
    {
        const auto reduction = processorRef.getBandReductionDb (selectedBand);
        const auto proportion = juce::jlimit (0.0f, 1.0f, -reduction / fullScaleDb);

        // Filling right-to-left: reduction pulls level DOWN, and a bar growing
        // rightward would read as "more output".
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
        g.drawText ("off", area.withTrimmedLeft (4), juce::Justification::centredLeft);
    }
}

void MultibandEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);

    g.setColour (theme::text);
    g.setFont (theme::titleFont (13.0f));
    g.drawText (processorRef.getName().toUpperCase(), header, juce::Justification::centredLeft);

    g.setColour (theme::border);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    const auto controlTop = getHeight() - controlHeight;
    g.drawHorizontalLine (controlTop, 0.0f, (float) getWidth());

    g.setColour (theme::panel);
    g.fillRect (0, controlTop + 1, getWidth(), controlHeight - 1);

    const auto accent = theme::bandColour (selectedBand);
    const auto on = isBandOn (selectedBand);
    const juce::Rectangle<int> badge { margin, controlTop + 26, badgeWidth, 18 };

    g.setColour (accent.withAlpha (on ? 0.22f : 0.07f));
    g.fillRoundedRectangle (badge.toFloat(), 3.0f);

    g.setColour (on ? accent : theme::textFaint);
    g.setFont (theme::labelFont (10.0f));
    g.drawText ("BAND " + juce::String (selectedBand + 1), badge, juce::Justification::centred);

    paintReductionMeter (g, { getWidth() - margin - 160, controlTop + rowHeight + 26, 160, 16 });

    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("click a band to select it   |   drag the dividers to move crossovers",
                juce::Rectangle<int> (margin, getHeight() - 16, getWidth() - margin * 2, 12),
                juce::Justification::centredRight);
}

void MultibandEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 5);
    bypassButton.setBounds (header.removeFromRight (66));

    auto controls = bounds.removeFromBottom (controlHeight).reduced (margin, 8);
    controls.removeFromBottom (10);

    display.setBounds (bounds.reduced (margin, margin - 2));

    // Row one: the compressor's shape. Row two: its timing. Grouping them this
    // way means threshold and ratio stay put while you tune attack and release.
    auto shapeRow  = controls.removeFromTop (rowHeight);
    auto timingRow = controls;

    shapeRow.removeFromLeft (badgeWidth + 8);
    bandOnButton.setBounds (shapeRow.removeFromLeft (38).withSizeKeepingCentre (34, 20));
    shapeRow.removeFromLeft (8);

    thresholdKnob.setBounds (shapeRow.removeFromLeft (knobSlot));
    ratioKnob    .setBounds (shapeRow.removeFromLeft (knobSlot));
    makeupKnob   .setBounds (shapeRow.removeFromLeft (knobSlot));

    auto ioArea = shapeRow.removeFromRight (knobSlot * 2);
    inputKnob .setBounds (ioArea.removeFromLeft (knobSlot));
    outputKnob.setBounds (ioArea.removeFromLeft (knobSlot));

    timingRow.removeFromLeft (badgeWidth + 8);
    muteButton.setBounds (timingRow.removeFromLeft (20).withSizeKeepingCentre (18, 20));
    timingRow.removeFromLeft (4);
    soloButton.setBounds (timingRow.removeFromLeft (20).withSizeKeepingCentre (18, 20));
    timingRow.removeFromLeft (10);

    attackKnob .setBounds (timingRow.removeFromLeft (knobSlot));
    releaseKnob.setBounds (timingRow.removeFromLeft (knobSlot));
}

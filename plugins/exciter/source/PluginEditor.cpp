#include "PluginEditor.h"

using namespace ui;

namespace
{
    constexpr int headerHeight  = 30;
    constexpr int controlHeight = 100;
    constexpr int margin        = 10;
    constexpr int knobSlot      = 76;
}

ExciterEditor::Knob::Knob (const juce::String& captionText,
                           const juce::String& suffix,
                           juce::LookAndFeel& lookAndFeel,
                           juce::Component& parent)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 15);
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

void ExciterEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    caption.setBounds (area.removeFromTop (13));
    slider.setBounds (area);
}

ExciterEditor::ExciterEditor (ExciterProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      focusKnob  ("FOCUS",  " Hz", mainLook, *this),
      driveKnob  ("DRIVE",  "",    mainLook, *this),
      mixKnob    ("MIX",    " %",  mainLook, *this),
      inputKnob  ("INPUT",  " dB", ioLook,   *this),
      outputKnob ("OUTPUT", " dB", ioLook,   *this)
{
    ioLook.setBipolar (true);

    auto& apvts = p.apvts;
    focusAttachment  = std::make_unique<SliderAttachment> (apvts, ParamID::focus,      focusKnob.slider);
    driveAttachment  = std::make_unique<SliderAttachment> (apvts, ParamID::drive,      driveKnob.slider);
    mixAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::mix,        mixKnob.slider);
    inputAttachment  = std::make_unique<SliderAttachment> (apvts, ParamID::inputGain,  inputKnob.slider);
    outputAttachment = std::make_unique<SliderAttachment> (apvts, ParamID::outputGain, outputKnob.slider);
    listenAttachment = std::make_unique<ButtonAttachment> (apvts, ParamID::listen,     listenButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, ParamID::bypass,     bypassButton);

    typeBox.addItemList ({ "Tube", "Tape", "Transistor" }, 1);
    typeBox.setColour (juce::ComboBox::backgroundColourId, theme::panelRaised);
    typeBox.setColour (juce::ComboBox::outlineColourId,    theme::border);
    typeBox.setColour (juce::ComboBox::textColourId,       theme::text);
    typeBox.setColour (juce::ComboBox::arrowColourId,      theme::textDim);
    addAndMakeVisible (typeBox);
    typeAttachment = std::make_unique<ChoiceAttachment> (apvts, ParamID::type, typeBox);

    listenButton.setLookAndFeel (&mainLook);
    bypassButton.setLookAndFeel (&ioLook);
    addAndMakeVisible (listenButton);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (display);
    display.setDecibelRange (-18.0f, 18.0f);
    display.setSpectrumRange (-90.0f, 0.0f);

    display.setSpectrumFunction ([this] (float lowHz, float highHz)
    {
        return processorRef.analyser.magnitudeDbForRange (lowHz, highHz);
    });

    /*  The focus frequency reuses the multiband's crossover handle.

        It is the same interaction — one draggable vertical divider splitting
        the spectrum into a region that is processed and one that is not — so
        it gets the same control rather than a second one that behaves almost
        the same.
    */
    display.setCrossoverDragCallback ([this] (int, float frequencyHz)
    {
        setParameter (ParamID::focus, frequencyHz);
    });

    display.setCrossoverGestureCallback ([this] (int, bool starting)
    {
        if (auto* parameter = processorRef.apvts.getParameter (ParamID::focus))
        {
            if (starting) parameter->beginChangeGesture();
            else          parameter->endChangeGesture();
        }
    });

    updateRegions();

    setSize (700, 440);
    startTimerHz (30);
}

ExciterEditor::~ExciterEditor()
{
    focusAttachment.reset();
    driveAttachment.reset();
    mixAttachment.reset();
    inputAttachment.reset();
    outputAttachment.reset();
    listenAttachment.reset();
    bypassAttachment.reset();
    typeAttachment.reset();

    for (auto* knob : { &focusKnob, &driveKnob, &mixKnob, &inputKnob, &outputKnob })
        knob->slider.setLookAndFeel (nullptr);

    listenButton.setLookAndFeel (nullptr);
    bypassButton.setLookAndFeel (nullptr);
}

float ExciterEditor::focusHz() const
{
    return processorRef.apvts.getRawParameterValue (ParamID::focus)->load();
}

int ExciterEditor::typeIndex() const
{
    return (int) processorRef.apvts.getRawParameterValue (ParamID::type)->load();
}

void ExciterEditor::updateRegions()
{
    const auto focus = focusHz();
    const auto accent = theme::bandColour (typeIndex() + 1);

    mainLook.setAccent (accent);

    // Two regions: what the saturator never sees, and what it does. The whole
    // control surface of an exciter is "where does the effect start".
    ui::FrequencyCurveDisplay::BandRegion untouched;
    untouched.id     = 0;
    untouched.lowHz  = 20.0f;
    untouched.highHz = focus;
    untouched.colour = theme::textFaint;
    untouched.dimmed = true;

    ui::FrequencyCurveDisplay::BandRegion excited;
    excited.id       = 1;
    excited.lowHz    = focus;
    excited.highHz   = 20000.0f;
    excited.colour   = accent;
    excited.selected = true;

    display.setBandRegions ({ untouched, excited });
    display.setCrossoverFrequencies ({ focus });
}

void ExciterEditor::setParameter (juce::StringRef parameterID, float value)
{
    if (auto* parameter = processorRef.apvts.getParameter (parameterID))
        parameter->setValueNotifyingHost (
            parameter->getNormalisableRange().convertTo0to1 (value));
}

void ExciterEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;

    const auto signature = apvts.getRawParameterValue (ParamID::focus)->load()
                         + apvts.getRawParameterValue (ParamID::drive)->load()  * 7.0f
                         + apvts.getRawParameterValue (ParamID::mix)->load()    * 31.0f
                         + apvts.getRawParameterValue (ParamID::type)->load()   * 997.0f
                         + apvts.getRawParameterValue (ParamID::listen)->load() * 131.0f;

    const auto moved = ! juce::approximatelyEqual (signature, lastSignature);

    if (moved)
    {
        lastSignature = signature;
        updateRegions();
        repaint();
    }

    if (processorRef.analyser.update() || moved)
        display.repaint();
}

void ExciterEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);

    g.setColour (theme::text);
    g.setFont (theme::titleFont (13.0f));
    g.drawText (processorRef.getName().toUpperCase(),
                header.removeFromLeft (120), juce::Justification::centredLeft);

    // Latency is worth stating. This is the only plugin in the set that has
    // any, and someone will want to know why the host reports a delay.
    g.setFont (theme::labelFont (10.0f));
    g.setColour (theme::textFaint);
    g.drawText (juce::String (processorRef.getLatencySamples()) + " smp latency (4x oversampling)",
                header.removeFromLeft (200), juce::Justification::centredLeft);

    g.setColour (theme::border);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    const auto controlTop = getHeight() - controlHeight;
    g.drawHorizontalLine (controlTop, 0.0f, (float) getWidth());

    g.setColour (theme::panel);
    g.fillRect (0, controlTop + 1, getWidth(), controlHeight - 1);

    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("drag the divider to move FOCUS   |   LISTEN solos the added harmonics",
                juce::Rectangle<int> (margin, getHeight() - 15, getWidth() - margin * 2, 12),
                juce::Justification::centredRight);
}

void ExciterEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 5);
    bypassButton.setBounds (header.removeFromRight (66));
    header.removeFromRight (8);
    typeBox.setBounds (header.removeFromRight (104));

    auto controls = bounds.removeFromBottom (controlHeight).reduced (margin, 8);
    controls.removeFromBottom (8);

    display.setBounds (bounds.reduced (margin, margin - 2));

    focusKnob.setBounds (controls.removeFromLeft (knobSlot));
    driveKnob.setBounds (controls.removeFromLeft (knobSlot));
    mixKnob  .setBounds (controls.removeFromLeft (knobSlot));

    controls.removeFromLeft (12);
    listenButton.setBounds (controls.removeFromLeft (62).withSizeKeepingCentre (58, 22));

    auto ioArea = controls.removeFromRight (knobSlot * 2);
    inputKnob .setBounds (ioArea.removeFromLeft (knobSlot));
    outputKnob.setBounds (ioArea.removeFromLeft (knobSlot));
}

#include "PluginEditor.h"

using namespace ui;

namespace
{
    constexpr int headerHeight  = 30;
    constexpr int controlHeight = 100;
    constexpr int margin        = 10;
    constexpr int knobSlot      = 76;

    // Six seconds at the editor's 30 Hz timer. Long enough that a slow optical
    // tail is visible end to end, which is the entire reason this display is
    // here rather than a meter.
    constexpr int   historyPoints  = 180;
    constexpr float historySeconds = 6.0f;
}

OptoEditor::Knob::Knob (const juce::String& captionText,
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

void OptoEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    caption.setBounds (area.removeFromTop (13));
    slider.setBounds (area);
}

OptoEditor::OptoEditor (OptoProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      history (historyPoints),
      thresholdKnob ("THRESHOLD", " dB", mainLook, *this),
      ratioKnob     ("RATIO",     ":1",  mainLook, *this),
      attackKnob    ("ATTACK",    " ms", mainLook, *this),
      releaseKnob   ("RELEASE",   " ms", mainLook, *this),
      programKnob   ("PROGRAM",   " %",  mainLook, *this),
      makeupKnob    ("MAKEUP",    " dB", mainLook, *this),
      inputKnob     ("INPUT",     " dB", ioLook,   *this),
      outputKnob    ("OUTPUT",    " dB", ioLook,   *this)
{
    ioLook.setBipolar (true);

    auto& apvts = p.apvts;
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ParamID::threshold,  thresholdKnob.slider);
    ratioAttachment     = std::make_unique<SliderAttachment> (apvts, ParamID::ratio,      ratioKnob.slider);
    attackAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::attack,     attackKnob.slider);
    releaseAttachment   = std::make_unique<SliderAttachment> (apvts, ParamID::release,    releaseKnob.slider);
    programAttachment   = std::make_unique<SliderAttachment> (apvts, ParamID::program,    programKnob.slider);
    makeupAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::makeup,     makeupKnob.slider);
    inputAttachment     = std::make_unique<SliderAttachment> (apvts, ParamID::inputGain,  inputKnob.slider);
    outputAttachment    = std::make_unique<SliderAttachment> (apvts, ParamID::outputGain, outputKnob.slider);
    bypassAttachment    = std::make_unique<ButtonAttachment> (apvts, ParamID::bypass,     bypassButton);

    characterBox.addItemList ({ "Optical", "FET" }, 1);
    characterBox.setColour (juce::ComboBox::backgroundColourId, theme::panelRaised);
    characterBox.setColour (juce::ComboBox::outlineColourId,    theme::border);
    characterBox.setColour (juce::ComboBox::textColourId,       theme::text);
    characterBox.setColour (juce::ComboBox::arrowColourId,      theme::textDim);
    addAndMakeVisible (characterBox);
    characterAttachment = std::make_unique<ChoiceAttachment> (apvts, ParamID::character, characterBox);

    bypassButton.setLookAndFeel (&ioLook);
    addAndMakeVisible (bypassButton);

    history.setFloorDb (-24.0f);
    history.setSpanSeconds (historySeconds);
    addAndMakeVisible (history);

    applyCharacter();

    setSize (700, 430);
    startTimerHz (30);
}

OptoEditor::~OptoEditor()
{
    thresholdAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    programAttachment.reset();
    makeupAttachment.reset();
    inputAttachment.reset();
    outputAttachment.reset();
    bypassAttachment.reset();
    characterAttachment.reset();

    for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob,
                        &programKnob, &makeupKnob, &inputKnob, &outputKnob })
        knob->slider.setLookAndFeel (nullptr);

    bypassButton.setLookAndFeel (nullptr);
}

bool OptoEditor::isFet() const
{
    return processorRef.apvts.getRawParameterValue (ParamID::character)->load() >= 0.5f;
}

void OptoEditor::applyCharacter()
{
    const auto fet = isFet();
    lastWasFet = fet;

    // Amber for FET, cyan for optical. The two voices behave differently enough
    // that the panel should not look identical in both.
    const auto accent = fet ? theme::bandColour (1) : theme::bandColour (0);
    mainLook.setAccent (accent);
    history.setAccent (accent);

    /*  PROGRAM does nothing in FET mode — the processor forces the depth to
        zero, because a 1176's release does not lengthen with sustained
        reduction the way an optical cell's does. Disabling the control says so,
        rather than leaving a knob that silently has no effect.
    */
    programKnob.slider.setEnabled (! fet);
    programKnob.caption.setColour (juce::Label::textColourId,
                                   fet ? theme::textFaint : theme::textDim);

    repaint();
}

void OptoEditor::timerCallback()
{
    history.push (processorRef.getGainReductionDb());

    // The cell's memory still accumulates in FET mode — it just does not drive
    // anything, because the processor forces programDepth to zero. Drawing it
    // anyway would imply it matters.
    history.pushSecondary (isFet() ? 0.0f : processorRef.getProgramMemory());
    history.repaint();

    if (isFet() != lastWasFet)
        applyCharacter();

    // The numeric readouts in the header move with the audio.
    repaint (0, 0, getWidth(), headerHeight);
}

void OptoEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);

    g.setColour (theme::text);
    g.setFont (theme::titleFont (13.0f));
    g.drawText (processorRef.getName().toUpperCase(),
                header.removeFromLeft (120), juce::Justification::centredLeft);

    // Current reduction as a number, next to the graph that shows its shape.
    // The graph answers "what is it doing", this answers "how much".
    const auto reduction = processorRef.getGainReductionDb();

    g.setFont (theme::valueFont (12.0f));
    g.setColour (reduction < -0.05f ? (isFet() ? theme::bandColour (1) : theme::bandColour (0))
                                    : theme::textFaint);
    g.drawText ("GR " + juce::String (reduction, 1) + " dB",
                header.removeFromLeft (110), juce::Justification::centredLeft);

    g.setColour (theme::border);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    const auto controlTop = getHeight() - controlHeight;
    g.drawHorizontalLine (controlTop, 0.0f, (float) getWidth());

    g.setColour (theme::panel);
    g.fillRect (0, controlTop + 1, getWidth(), controlHeight - 1);

    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (9.5f));
    g.drawText (isFet() ? "FET: fast peak detection, no program-dependent tail"
                        : "OPTICAL: the faint line is the cell's memory - it lengthens the release",
                juce::Rectangle<int> (margin, getHeight() - 15, getWidth() - margin * 2, 12),
                juce::Justification::centredRight);
}

void OptoEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 5);
    bypassButton.setBounds (header.removeFromRight (66));
    header.removeFromRight (8);
    characterBox.setBounds (header.removeFromRight (92));

    auto controls = bounds.removeFromBottom (controlHeight).reduced (margin, 8);
    controls.removeFromBottom (8);

    history.setBounds (bounds.reduced (margin, margin - 2));

    thresholdKnob.setBounds (controls.removeFromLeft (knobSlot));
    ratioKnob    .setBounds (controls.removeFromLeft (knobSlot));
    attackKnob   .setBounds (controls.removeFromLeft (knobSlot));
    releaseKnob  .setBounds (controls.removeFromLeft (knobSlot));
    programKnob  .setBounds (controls.removeFromLeft (knobSlot));
    makeupKnob   .setBounds (controls.removeFromLeft (knobSlot));

    auto ioArea = controls.removeFromRight (knobSlot * 2);
    inputKnob .setBounds (ioArea.removeFromLeft (knobSlot));
    outputKnob.setBounds (ioArea.removeFromLeft (knobSlot));
}

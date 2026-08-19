#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <ui/Theme.h>

/*
    A thin-arc rotary.

    JUCE's stock rotary — a filled grey pie with a line — is the single most
    recognisable sign of an unfinished plugin. The modern vocabulary is a thin
    unfilled track, a saturated arc showing the value, and a small indicator:
    less ink, more precision.

    Two details that do most of the work:

      * Bipolar controls (gain, pan) fill from the centre outward, not from the
        minimum. Seeing a boost and a cut mirror each other around 12 o'clock
        is worth more than any amount of gloss.

      * The text box is drawn flat with no border. Boxed values fragment the
        panel; the number should read as a label, not a form field.
*/
namespace ui
{

class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit KnobLookAndFeel (juce::Colour accentColour = theme::bandColour (0))
        : accent (accentColour)
    {
        setColour (juce::Slider::textBoxTextColourId,       theme::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,               theme::textDim);
    }

    void setAccent (juce::Colour newAccent) { accent = newAccent; }

    // Bipolar knobs fill outward from 12 o'clock instead of from the left stop.
    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto thickness = juce::jmax (2.0f, radius * 0.14f);
        const auto arcRadius = radius - thickness * 0.5f;

        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);

        g.setColour (theme::panelRaised);
        g.strokePath (track, juce::PathStrokeType (thickness,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Value arc
        const auto originAngle = bipolar
            ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
            : rotaryStartAngle;

        if (std::abs (angle - originAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (originAngle, angle),
                                 juce::jmax (originAngle, angle), true);

            g.setColour (slider.isEnabled() ? accent : theme::textFaint);
            g.strokePath (value, juce::PathStrokeType (thickness,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Indicator — a short spoke rather than a full radius line, so it reads
        // as a pointer instead of a clock hand.
        const auto inner = arcRadius - thickness * 0.9f;
        const auto outer = arcRadius - thickness * 2.1f;

        juce::Path pointer;
        pointer.startNewSubPath (centre.x + inner * std::sin (angle),
                                 centre.y - inner * std::cos (angle));
        pointer.lineTo         (centre.x + outer * std::sin (angle),
                                centre.y - outer * std::cos (angle));

        g.setColour (theme::text);
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.5f, thickness * 0.42f),
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (theme::valueFont (11.0f));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineWhenEditingColourId, accent);
        return label;
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawAsHighlighted, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto on = button.getToggleState();
        const auto corner = 3.0f;

        g.setColour (on ? accent.withAlpha (0.22f) : theme::panelRaised);
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (on ? accent
                        : (shouldDrawAsHighlighted ? theme::textDim : theme::border));
        g.drawRoundedRectangle (bounds, corner, 1.0f);

        g.setColour (on ? accent : theme::textDim);
        g.setFont (theme::labelFont (10.5f));
        g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
    }

private:
    juce::Colour accent;
    bool bipolar { false };
};

} // namespace ui

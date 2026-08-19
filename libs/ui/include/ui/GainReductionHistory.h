#pragma once

#include <algorithm>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <ui/Theme.h>

/*
    A scrolling history of gain reduction — the last several seconds, newest at
    the right.

    A frequency display is the wrong instrument for a broadband compressor:
    nothing about it varies with frequency. What does vary, and what is the
    entire selling point of an optical compressor, is how the reduction moves
    over TIME. A meter shows you the current value and nothing else; this shows
    the shape — the fast snap back, then the long tail — which is exactly the
    behaviour that is otherwise invisible and easy to mistake for the plugin
    being inconsistent.

    Fed from the editor's timer, so the horizontal axis is real time at whatever
    rate that runs. Not sample-accurate, and does not need to be: the gestures
    worth seeing last hundreds of milliseconds.
*/
namespace ui
{

class GainReductionHistory : public juce::Component
{
public:
    explicit GainReductionHistory (int numPoints = 300)
        : history ((size_t) juce::jmax (16, numPoints), 0.0f)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setFloorDb (float newFloorDb)
    {
        floorDb = juce::jmin (-1.0f, newFloorDb);
        repaint();
    }

    void setAccent (juce::Colour newAccent)
    {
        accent = newAccent;
        repaint();
    }

    // Seconds of history, used only to label the axis. The actual span is
    // numPoints divided by whatever rate push() is called at.
    void setSpanSeconds (float seconds) { spanSeconds = seconds; }

    void push (float reductionDb)
    {
        history[writeIndex] = juce::jlimit (floorDb, 0.0f, reductionDb);
        writeIndex = (writeIndex + 1) % history.size();
    }

    // A second trace, 0..1, drawn as a thin line. Used for the optical cell's
    // program memory: it explains WHY the release is behaving as it is.
    void pushSecondary (float normalised)
    {
        secondary[secondaryIndex] = juce::jlimit (0.0f, 1.0f, normalised);
        secondaryIndex = (secondaryIndex + 1) % secondary.size();
    }

    void clear()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        std::fill (secondary.begin(), secondary.end(), 0.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto plot = bounds.withTrimmedRight (26.0f);

        g.fillAll (theme::displayBackground);

        paintGrid (g, plot);
        paintHistory (g, plot);
        paintSecondary (g, plot);

        g.setColour (theme::border);
        g.drawRect (getLocalBounds(), 1);
    }

private:
    float valueToY (float decibels, juce::Rectangle<float> plot) const
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f, decibels / floorDb);
        return plot.getY() + proportion * plot.getHeight();
    }

    void paintGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const auto step = floorDb <= -24.0f ? 6.0f : 3.0f;

        g.setFont (theme::labelFont (10.0f));

        for (float db = 0.0f; db >= floorDb; db -= step)
        {
            const auto y = valueToY (db, plot);
            const auto isZero = std::abs (db) < 0.01f;

            g.setColour (isZero ? theme::gridZeroLine : theme::gridLine);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

            g.setColour (isZero ? theme::textDim : theme::textFaint);
            g.drawText (juce::String ((int) db),
                        juce::Rectangle<float> (plot.getRight() + 3.0f, y - 6.0f, 22.0f, 12.0f),
                        juce::Justification::centredLeft);
        }

        // Time ticks every second, so the tail has a scale to be read against.
        const auto pointsPerSecond = (float) history.size() / juce::jmax (0.1f, spanSeconds);

        for (float second = 1.0f; second * pointsPerSecond < (float) history.size(); second += 1.0f)
        {
            const auto x = plot.getRight() - second * pointsPerSecond
                                           * plot.getWidth() / (float) history.size();

            g.setColour (theme::gridLine);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        }

        g.setColour (theme::textFaint);
        g.setFont (theme::labelFont (9.5f));
        g.drawText ("-" + juce::String (spanSeconds, 0) + " s",
                    juce::Rectangle<float> (plot.getX() + 3.0f, plot.getBottom() - 13.0f, 40.0f, 12.0f),
                    juce::Justification::centredLeft);
        g.drawText ("now",
                    juce::Rectangle<float> (plot.getRight() - 34.0f, plot.getBottom() - 13.0f, 30.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    void paintHistory (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const auto count = history.size();
        const auto zeroY = valueToY (0.0f, plot);

        juce::Path shape;
        shape.startNewSubPath (plot.getX(), zeroY);

        for (size_t i = 0; i < count; ++i)
        {
            // Oldest sample first: writeIndex is where the NEXT value goes, so
            // it is also the oldest one currently stored.
            const auto value = history[(writeIndex + i) % count];
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (count - 1);

            shape.lineTo (x, valueToY (value, plot));
        }

        shape.lineTo (plot.getRight(), zeroY);
        shape.closeSubPath();

        g.setColour (accent.withAlpha (0.22f));
        g.fillPath (shape);

        // The outline carries the shape; the fill only gives it weight.
        juce::Path outline;
        bool started = false;

        for (size_t i = 0; i < count; ++i)
        {
            const auto value = history[(writeIndex + i) % count];
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (count - 1);
            const auto y = valueToY (value, plot);

            if (! started) { outline.startNewSubPath (x, y); started = true; }
            else           { outline.lineTo (x, y); }
        }

        g.setColour (accent);
        g.strokePath (outline, juce::PathStrokeType (1.4f));
    }

    void paintSecondary (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const auto count = secondary.size();
        auto anything = false;

        for (auto value : secondary)
            anything = anything || value > 0.005f;

        if (! anything)
            return;

        juce::Path path;
        bool started = false;

        for (size_t i = 0; i < count; ++i)
        {
            const auto value = secondary[(secondaryIndex + i) % count];
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (count - 1);

            // Drawn against the bottom of the plot, not the dB axis: it is a
            // 0..1 state, not a level, and overlaying it on the dB scale would
            // invite reading it as one.
            const auto y = plot.getBottom() - value * plot.getHeight() * 0.22f;

            if (! started) { path.startNewSubPath (x, y); started = true; }
            else           { path.lineTo (x, y); }
        }

        g.setColour (theme::text.withAlpha (0.45f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    std::vector<float> history;
    std::vector<float> secondary { std::vector<float> (300, 0.0f) };

    size_t writeIndex     { 0 };
    size_t secondaryIndex { 0 };

    float floorDb     { -24.0f };
    float spanSeconds { 10.0f };

    juce::Colour accent { theme::bandColour (0) };
};

} // namespace ui

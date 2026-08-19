#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <dsp/FilterResponse.h>
#include <ui/Theme.h>

/*
    The analyser display: log frequency grid, dB grid, and one response curve.

    Deliberately knows nothing about EQs, compressors or parameters. It is
    handed a function from frequency to dB and draws whatever that returns, so
    the same component serves the dynamic EQ, the multiband crossover view and
    anything else the chain grows.

    The spectrum analyser layer lands here next; the grid and axis mapping are
    already shared so the curve and the spectrum cannot drift out of alignment.
*/
namespace ui
{

class FrequencyCurveDisplay : public juce::Component
{
public:
    using ResponseFunction = std::function<float (float frequencyHz)>;

    FrequencyCurveDisplay()
    {
        setInterceptsMouseClicks (false, false);
    }

    void setResponseFunction (ResponseFunction fn)
    {
        responseFunction = std::move (fn);
        repaint();
    }

    void setDecibelRange (float minDb, float maxDb)
    {
        decibelMin = minDb;
        decibelMax = maxDb;
        repaint();
    }

    /*  A band handle drawn on the curve.

        This is the element that makes a Pro-Q style display feel like an
        instrument rather than a readout — it tells you at a glance which band
        is where, and it is what you will eventually drag.
    */
    struct BandNode
    {
        float        frequencyHz { 1000.0f };
        float        gainDb      { 0.0f };
        juce::Colour colour      { theme::bandColour (0) };
        bool         active      { true };
    };

    void setNodes (std::vector<BandNode> newNodes)
    {
        nodes = std::move (newNodes);
        repaint();
    }

    // ── Axis mapping ────────────────────────────────────────────────────
    float frequencyToX (float frequencyHz) const noexcept
    {
        return plotArea.getX()
             + dsp::frequencyToNormalised (frequencyHz) * plotArea.getWidth();
    }

    float xToFrequency (float x) const noexcept
    {
        return dsp::normalisedToFrequency ((x - plotArea.getX()) / plotArea.getWidth());
    }

    float decibelsToY (float decibels) const noexcept
    {
        const auto proportion = (decibels - decibelMin) / (decibelMax - decibelMin);
        return plotArea.getBottom() - proportion * plotArea.getHeight();
    }

    void resized() override
    {
        // Room at the bottom for frequency labels, at the right for dB labels.
        plotArea = getLocalBounds().toFloat().reduced (1.0f);
        plotArea.removeFromBottom (14.0f);
        plotArea.removeFromRight (26.0f);

        // The topmost dB label is centred on the plot's top edge, so without
        // this it renders half outside the component and gets clipped.
        plotArea.removeFromTop (8.0f);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::displayBackground);

        paintFrequencyGrid (g);
        paintDecibelGrid (g);
        paintCurve (g);
        paintNodes (g);

        g.setColour (theme::border);
        g.drawRect (getLocalBounds(), 1);
    }

private:
    void paintFrequencyGrid (juce::Graphics& g) const
    {
        // Decades get a label and a stronger line; the 2/3/5 subdivisions give
        // the eye something to interpolate against without shouting.
        struct GridLine { float hz; bool labelled; };

        static constexpr GridLine lines[] = {
            {    30.0f, false }, {    40.0f, false }, {    50.0f, false },
            {   100.0f, true  }, {   200.0f, false }, {   300.0f, false },
            {   500.0f, false }, {  1000.0f, true  }, {  2000.0f, false },
            {  3000.0f, false }, {  5000.0f, false }, { 10000.0f, true  },
            { 20000.0f, false },
        };

        g.setFont (theme::labelFont (10.0f));

        for (const auto& line : lines)
        {
            const auto x = frequencyToX (line.hz);

            g.setColour (line.labelled ? theme::gridLineStrong : theme::gridLine);
            g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());

            if (line.labelled)
            {
                const auto label = line.hz >= 1000.0f
                                 ? juce::String (line.hz / 1000.0f, 0) + " kHz"
                                 : juce::String ((int) line.hz) + " Hz";

                g.setColour (theme::textFaint);
                g.drawText (label,
                            juce::Rectangle<float> (x - 26.0f, plotArea.getBottom() + 1.0f,
                                                    52.0f, 12.0f),
                            juce::Justification::centred);
            }
        }
    }

    void paintDecibelGrid (juce::Graphics& g) const
    {
        const auto span = decibelMax - decibelMin;
        const auto step = span > 40.0f ? 12.0f : 6.0f;

        g.setFont (theme::labelFont (10.0f));

        // Anchored to multiples of the step rather than started at the range
        // minimum. Starting at the minimum can step straight over 0 dB — which
        // is the one line the eye actually needs.
        const auto first = std::ceil (decibelMin / step) * step;

        for (float db = first; db <= decibelMax + 0.01f; db += step)
        {
            const auto y = decibelsToY (db);
            const auto isZero = std::abs (db) < 0.01f;

            g.setColour (isZero ? theme::gridZeroLine : theme::gridLine);
            g.drawHorizontalLine ((int) y, plotArea.getX(), plotArea.getRight());

            g.setColour (isZero ? theme::textDim : theme::textFaint);
            g.drawText (juce::String ((int) db),
                        juce::Rectangle<float> (plotArea.getRight() + 3.0f, y - 6.0f,
                                                22.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    void paintCurve (juce::Graphics& g) const
    {
        if (! responseFunction)
            return;

        juce::Path curve;
        bool started = false;

        // One evaluation per pixel column. A few hundred complex magnitudes per
        // repaint is nothing on the message thread, and it means the curve is
        // exact rather than an interpolation between control points.
        for (float x = plotArea.getX(); x <= plotArea.getRight(); x += 1.0f)
        {
            const auto db = responseFunction (xToFrequency (x));
            const auto y  = decibelsToY (juce::jlimit (decibelMin, decibelMax, db));

            if (! started)
            {
                curve.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                curve.lineTo (x, y);
            }
        }

        if (! started)
            return;

        // Fill between the curve and the 0 dB line, so boosts and cuts read at
        // a glance without having to trace the line.
        juce::Path fill (curve);
        fill.lineTo (plotArea.getRight(), decibelsToY (0.0f));
        fill.lineTo (plotArea.getX(),     decibelsToY (0.0f));
        fill.closeSubPath();

        g.setColour (theme::curveFill);
        g.fillPath (fill);

        g.setColour (theme::curve);
        g.strokePath (curve, juce::PathStrokeType (1.6f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    void paintNodes (juce::Graphics& g) const
    {
        for (const auto& node : nodes)
        {
            const auto x = frequencyToX (node.frequencyHz);
            const auto y = decibelsToY (juce::jlimit (decibelMin, decibelMax, node.gainDb));

            const auto colour = node.active ? node.colour : theme::textFaint;

            // A vertical hairline anchors the handle to the frequency axis, so
            // the eye can read "which frequency" without tracing down.
            g.setColour (colour.withAlpha (0.25f));
            g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());

            // Halo, so the handle stays readable wherever the curve sits.
            g.setColour (theme::displayBackground.withAlpha (0.85f));
            g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);

            g.setColour (colour);
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);

            g.setColour (colour.withAlpha (0.45f));
            g.drawEllipse (x - 6.5f, y - 6.5f, 13.0f, 13.0f, 1.2f);
        }
    }

    ResponseFunction responseFunction;
    std::vector<BandNode> nodes;
    juce::Rectangle<float> plotArea;

    float decibelMin { -18.0f };
    float decibelMax {  18.0f };
};

} // namespace ui

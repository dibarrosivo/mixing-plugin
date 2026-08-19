#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <dsp/FilterResponse.h>
#include <ui/Theme.h>

/*
    The analyser display: log frequency grid, dB grid, spectrum, response curve
    and draggable band handles.

    Deliberately knows nothing about EQs, compressors or parameters. It is
    handed functions — frequency to dB for the curve, a frequency span to dBFS
    for the spectrum — and reports drags back through a callback. The same
    component therefore serves the dynamic EQ, the multiband crossover view and
    whatever the chain grows next.

    Everything shares one axis mapping, so the grid, the curve, the spectrum and
    the handles cannot drift apart by a pixel.
*/
namespace ui
{

class FrequencyCurveDisplay : public juce::Component
{
public:
    using ResponseFunction = std::function<float (float frequencyHz)>;
    using SpectrumFunction = std::function<float (float lowHz, float highHz)>;

    /*  Reports a handle being dragged. The component never changes anything
        itself — it does not own the parameters and must not guess at gesture
        semantics, which differ per host.
    */
    using NodeDragCallback    = std::function<void (int index, float frequencyHz, float gainDb)>;
    using NodeGestureCallback = std::function<void (int index, bool starting)>;
    using NodeSelectCallback  = std::function<void (int index)>;
    using AddBandCallback     = std::function<void (float frequencyHz, float gainDb)>;
    using RemoveBandCallback  = std::function<void (int index)>;

    FrequencyCurveDisplay()
    {
        setInterceptsMouseClicks (true, false);
    }

    void setResponseFunction (ResponseFunction fn)
    {
        responseFunction = std::move (fn);
        repaint();
    }

    void setSpectrumFunction (SpectrumFunction fn)
    {
        spectrumFunction = std::move (fn);
        repaint();
    }

    void setDecibelRange (float minDb, float maxDb)
    {
        decibelMin = minDb;
        decibelMax = maxDb;
        repaint();
    }

    // The spectrum has its own scale: it is an absolute level in dBFS, not a
    // gain change, so it cannot share the EQ axis.
    void setSpectrumRange (float minDbfs, float maxDbfs)
    {
        spectrumMin = minDbfs;
        spectrumMax = maxDbfs;
        repaint();
    }

    struct BandNode
    {
        float        frequencyHz { 1000.0f };
        float        gainDb      { 0.0f };
        juce::Colour colour      { theme::bandColour (0) };
        bool         active      { true };
        bool         draggable   { true };
        bool         selected    { false };

        // This band's own contribution, used to tint the region it is
        // responsible for. Optional: without it only the handle is drawn.
        std::function<float (float frequencyHz)> response;
    };

    void setNodes (std::vector<BandNode> newNodes)
    {
        nodes = std::move (newNodes);
        repaint();
    }

    void setNodeDragCallback (NodeDragCallback fn)       { onNodeDrag = std::move (fn); }
    void setNodeGestureCallback (NodeGestureCallback fn) { onNodeGesture = std::move (fn); }
    void setNodeSelectCallback (NodeSelectCallback fn)   { onNodeSelect = std::move (fn); }
    void setAddBandCallback (AddBandCallback fn)         { onAddBand = std::move (fn); }
    void setRemoveBandCallback (RemoveBandCallback fn)   { onRemoveBand = std::move (fn); }

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

    float yToDecibels (float y) const noexcept
    {
        const auto proportion = (plotArea.getBottom() - y) / plotArea.getHeight();
        return decibelMin + proportion * (decibelMax - decibelMin);
    }

    void resized() override
    {
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
        paintSpectrum (g);      // behind the curve — it is context, not content
        paintBandFills (g);     // then each band's own contribution
        paintCurve (g);         // then the composite, on top of both
        paintNodes (g);

        g.setColour (theme::border);
        g.drawRect (getLocalBounds(), 1);
    }

    // ── Interaction ─────────────────────────────────────────────────────
    void mouseDown (const juce::MouseEvent& event) override
    {
        draggedNode = findNodeNear (event.position);

        if (draggedNode < 0)
            return;

        if (onNodeSelect)
            onNodeSelect (draggedNode);

        if (onNodeGesture)
            onNodeGesture (draggedNode, true);
    }

    /*  Double-click adds a band where you clicked, or removes the one you
        clicked on. This is the interaction the reference plugins use, and it is
        worth matching: it is the difference between an EQ you place bands on
        and an EQ you configure.
    */
    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        const auto onNode = findNodeNear (event.position);

        if (onNode >= 0)
        {
            if (onRemoveBand)
                onRemoveBand (onNode);

            return;
        }

        if (! plotArea.contains (event.position) || ! onAddBand)
            return;

        onAddBand (xToFrequency (event.position.x),
                   juce::jlimit (decibelMin, decibelMax, yToDecibels (event.position.y)));
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (draggedNode < 0 || ! onNodeDrag)
            return;

        const auto frequency = xToFrequency (juce::jlimit (plotArea.getX(),
                                                           plotArea.getRight(),
                                                           event.position.x));
        const auto gain = juce::jlimit (decibelMin, decibelMax,
                                        yToDecibels (event.position.y));

        onNodeDrag (draggedNode, frequency, gain);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggedNode >= 0 && onNodeGesture)
            onNodeGesture (draggedNode, false);

        draggedNode = -1;
        hoveredNode = -1;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const auto found = findNodeNear (event.position);

        if (found != hoveredNode)
        {
            hoveredNode = found;
            setMouseCursor (found >= 0 ? juce::MouseCursor::DraggingHandCursor
                                       : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoveredNode >= 0)
        {
            hoveredNode = -1;
            repaint();
        }
    }

private:
    int findNodeNear (juce::Point<float> position) const
    {
        constexpr float grabRadius = 11.0f;

        int   best = -1;
        float bestDistance = grabRadius;

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (! nodes[i].draggable)
                continue;

            const juce::Point<float> centre {
                frequencyToX (nodes[i].frequencyHz),
                decibelsToY (juce::jlimit (decibelMin, decibelMax, nodes[i].gainDb))
            };

            const auto distance = centre.getDistanceFrom (position);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = (int) i;
            }
        }

        return best;
    }

    void paintFrequencyGrid (juce::Graphics& g) const
    {
        struct GridLine { float hz; bool labelled; };

        // Labelling the 1-2-5 series rather than only decades. Three labels
        // across ten octaves leaves too much to interpolate by eye; this is the
        // density the reference analysers use.
        static constexpr GridLine lines[] = {
            {    30.0f, true  }, {    40.0f, false }, {    50.0f, true  },
            {    70.0f, false }, {   100.0f, true  }, {   200.0f, true  },
            {   300.0f, false }, {   500.0f, true  }, {   700.0f, false },
            {  1000.0f, true  }, {  2000.0f, true  }, {  3000.0f, false },
            {  5000.0f, true  }, {  7000.0f, false }, { 10000.0f, true  },
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
                // Bare numbers below 1 kHz, "k" above. Repeating the unit on
                // every label is noise the eye has to filter out.
                const auto label = line.hz >= 1000.0f
                                 ? juce::String (line.hz / 1000.0f, 0) + "k"
                                 : juce::String ((int) line.hz);

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

    void paintSpectrum (juce::Graphics& g) const
    {
        if (! spectrumFunction)
            return;

        const auto bottom = plotArea.getBottom();

        juce::Path shape;
        shape.startNewSubPath (plotArea.getX(), bottom);

        // Each pixel column asks for the peak across the frequency span it
        // covers, not a point sample. Above a few kHz one pixel spans dozens of
        // FFT bins, and point sampling makes the top octave look sparse.
        for (float x = plotArea.getX(); x <= plotArea.getRight(); x += 1.0f)
        {
            const auto lowHz  = xToFrequency (x - 0.5f);
            const auto highHz = xToFrequency (x + 0.5f);

            const auto dbfs = spectrumFunction (lowHz, highHz);
            const auto proportion = juce::jlimit (0.0f, 1.0f,
                (dbfs - spectrumMin) / (spectrumMax - spectrumMin));

            shape.lineTo (x, bottom - proportion * plotArea.getHeight());
        }

        shape.lineTo (plotArea.getRight(), bottom);
        shape.closeSubPath();

        g.setColour (theme::spectrum);
        g.fillPath (shape);

        g.setColour (theme::spectrumPeak);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    void paintBandFills (juce::Graphics& g) const
    {
        const auto zeroY = decibelsToY (0.0f);

        for (const auto& node : nodes)
        {
            if (! node.active || ! node.response)
                continue;

            juce::Path shape;
            shape.startNewSubPath (plotArea.getX(), zeroY);

            for (float x = plotArea.getX(); x <= plotArea.getRight(); x += 1.0f)
            {
                const auto db = node.response (xToFrequency (x));
                shape.lineTo (x, decibelsToY (juce::jlimit (decibelMin, decibelMax, db)));
            }

            shape.lineTo (plotArea.getRight(), zeroY);
            shape.closeSubPath();

            // Low alpha, and the selected band a little stronger. These stack
            // where bands overlap, which is correct — the overlap is exactly
            // where two bands are both acting.
            g.setColour (node.colour.withAlpha (node.selected ? 0.26f : 0.14f));
            g.fillPath (shape);
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
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node = nodes[i];

            const auto x = frequencyToX (node.frequencyHz);
            const auto y = decibelsToY (juce::jlimit (decibelMin, decibelMax, node.gainDb));

            const auto colour = node.active ? node.colour : theme::textFaint;
            const auto engaged = ((int) i == hoveredNode) || ((int) i == draggedNode);
            const auto radius = (engaged || node.selected) ? 5.5f : 4.0f;

            // The hairline anchors a handle to the frequency axis, but only for
            // the band being touched. Drawing one per band puts six coloured
            // verticals across the display, competing with the grid and making
            // the whole thing read as busier than it is.
            if (engaged || node.selected)
            {
                g.setColour (colour.withAlpha (engaged ? 0.45f : 0.28f));
                g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
            }

            // Halo, so the handle stays readable wherever the curve sits.
            g.setColour (theme::displayBackground.withAlpha (0.85f));
            g.fillEllipse (x - radius - 2.0f, y - radius - 2.0f,
                           (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);

            g.setColour (colour);
            g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);

            g.setColour (colour.withAlpha (engaged ? 0.8f : 0.45f));
            g.drawEllipse (x - radius - 2.5f, y - radius - 2.5f,
                           (radius + 2.5f) * 2.0f, (radius + 2.5f) * 2.0f, 1.2f);
        }
    }

    ResponseFunction    responseFunction;
    SpectrumFunction    spectrumFunction;
    NodeDragCallback    onNodeDrag;
    NodeGestureCallback onNodeGesture;
    NodeSelectCallback  onNodeSelect;
    AddBandCallback     onAddBand;
    RemoveBandCallback  onRemoveBand;

    std::vector<BandNode> nodes;
    juce::Rectangle<float> plotArea;

    float decibelMin { -18.0f };
    float decibelMax {  18.0f };
    float spectrumMin { -90.0f };
    float spectrumMax {   0.0f };

    int hoveredNode { -1 };
    int draggedNode { -1 };
};

} // namespace ui

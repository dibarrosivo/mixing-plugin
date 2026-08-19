/*
    Tests for GainComputer.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/gc_test \
            libs/dsp/tests/gain_computer_test.cpp && /tmp/gc_test

    The knee is where these go wrong. A hard-knee curve is three lines of
    arithmetic anyone can eyeball; a soft knee has to be continuous in value
    AND slope at both edges, and an implementation that is off by a factor of
    two still looks plausible on a graph. Sections 4 and 5 pin that down
    numerically.
*/

#include <cmath>
#include <cstdio>
#include <string>

#include <dsp/Decibels.h>
#include <dsp/GainComputer.h>

using namespace dsp;

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
        if (! ok) ++failures;
    }

    void checkClose (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-50s expected %+8.4f dB   got %+8.4f dB\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    GainComputer make (float thresholdDb, float ratio, float kneeDb)
    {
        GainComputer gc;
        gc.setThresholdDb (thresholdDb);
        gc.setRatio (ratio);
        gc.setKneeWidthDb (kneeDb);
        return gc;
    }
}

int main()
{
    std::printf ("\n== 1. Below threshold, nothing happens ==\n");
    {
        auto gc = make (-20.0f, 4.0f, 0.0f);

        for (float level : { -100.0f, -60.0f, -30.0f, -20.001f })
            checkClose (gc.computeGainDb (level), 0.0, 1.0e-6,
                        "level " + std::to_string ((int) level) + " dB -> no gain change");
    }

    std::printf ("\n== 2. Hard knee applies the ratio exactly ==\n");
    {
        auto gc = make (-20.0f, 4.0f, 0.0f);

        // 12 dB over a -20 threshold at 4:1 -> 3 dB over -> 9 dB of reduction.
        checkClose (gc.computeGainDb (-8.0f),  -9.0, 1.0e-4, "12 dB over at 4:1");
        checkClose (gc.computeGainDb (-16.0f), -3.0, 1.0e-4, "4 dB over at 4:1");
        checkClose (gc.computeGainDb (0.0f),  -15.0, 1.0e-4, "20 dB over at 4:1");
    }

    std::printf ("\n== 3. Ratio extremes ==\n");
    {
        auto unity = make (-20.0f, 1.0f, 0.0f);
        checkClose (unity.computeGainDb (0.0f), 0.0, 1.0e-6, "1:1 never reduces");

        auto limiter = make (-20.0f, 1000.0f, 0.0f);
        // At an effectively infinite ratio the output pins to the threshold.
        checkClose (limiter.computeGainDb (0.0f), -20.0, 0.05, "1000:1 pins output at threshold");

        // A sub-unity ratio would be upward expansion. It is clamped to 1:1,
        // which means no reduction at all — not reduction at some other ratio.
        auto clamped = make (-20.0f, 0.25f, 0.0f);
        checkClose (clamped.computeGainDb (0.0f), 0.0, 1.0e-6,
                    "ratio below 1:1 clamps to 1:1, so no reduction");
        check (clamped.getRatio() >= 1.0f, "sub-unity ratio clamped, never boosts");
    }

    std::printf ("\n== 4. Soft knee is continuous in VALUE at both edges ==\n");
    {
        const float threshold = -20.0f, ratio = 4.0f, knee = 8.0f;
        auto soft = make (threshold, ratio, knee);
        auto hard = make (threshold, ratio, 0.0f);

        const auto lowerEdge = threshold - knee * 0.5f;
        const auto upperEdge = threshold + knee * 0.5f;

        checkClose (soft.computeGainDb (lowerEdge - 0.001f),
                    soft.computeGainDb (lowerEdge + 0.001f), 1.0e-3,
                    "no step at the lower knee edge");

        checkClose (soft.computeGainDb (upperEdge + 0.001f),
                    hard.computeGainDb (upperEdge + 0.001f), 1.0e-3,
                    "rejoins the hard curve above the knee");

        checkClose (soft.computeGainDb (lowerEdge), 0.0, 1.0e-4,
                    "lower knee edge is still unity");
    }

    std::printf ("\n== 5. Soft knee is continuous in SLOPE at both edges ==\n");
    {
        // Value continuity alone is easy to get right by accident. A kink in
        // the slope is what you actually hear as the compressor "grabbing".
        const float threshold = -20.0f, ratio = 4.0f, knee = 8.0f;
        auto gc = make (threshold, ratio, knee);

        auto slopeAt = [&] (float level)
        {
            constexpr float h = 0.01f;
            return (gc.computeGainDb (level + h) - gc.computeGainDb (level - h)) / (2.0f * h);
        };

        const auto lowerEdge = threshold - knee * 0.5f;
        const auto upperEdge = threshold + knee * 0.5f;

        // Straddle each edge closely. Sampling further out measures the shape
        // of the curve rather than continuity at the join, which is the thing
        // that actually has to hold.
        checkClose (slopeAt (lowerEdge - 0.02f), slopeAt (lowerEdge + 0.02f), 0.01,
                    "slope matches across the lower edge");
        checkClose (slopeAt (upperEdge - 0.02f), slopeAt (upperEdge + 0.02f), 0.01,
                    "slope matches across the upper edge");

        // Outside the knee the slopes are known exactly: 0 below, (1/R - 1) above.
        checkClose (slopeAt (threshold - knee), 0.0, 1.0e-3, "slope is 0 below the knee");
        checkClose (slopeAt (threshold + knee), 1.0f / 4.0f - 1.0f, 1.0e-3,
                    "slope is (1/R - 1) above the knee");
    }

    std::printf ("\n== 6. Knee midpoint is half the full-ratio reduction ==\n");
    {
        // At the threshold itself a soft knee should have applied exactly half
        // the slope it eventually reaches. This is the single value that most
        // cleanly separates a correct knee from a plausible-looking one.
        const float threshold = -20.0f, ratio = 4.0f, knee = 8.0f;
        auto gc = make (threshold, ratio, knee);

        const auto expected = (1.0f / ratio - 1.0f) * knee / 8.0f;
        checkClose (gc.computeGainDb (threshold), expected, 1.0e-4,
                    "gain at threshold with an 8 dB knee");
    }

    std::printf ("\n== 7. Gain is never positive ==\n");
    {
        // These are subtractive processors. A positive value here would mean a
        // dynamic EQ band boosting a resonance instead of taming it.
        auto gc = make (-20.0f, 4.0f, 8.0f);
        bool everPositive = false;

        for (float level = -120.0f; level <= 20.0f; level += 0.05f)
            if (gc.computeGainDb (level) > 1.0e-6f)
                everPositive = true;

        check (! everPositive, "no positive gain anywhere from -120 to +20 dB");
    }

    std::printf ("\n== 8. Monotonic: louder input never means less reduction ==\n");
    {
        auto gc = make (-20.0f, 4.0f, 8.0f);
        bool monotonic = true;
        float previous = 0.0f;

        for (float level = -120.0f; level <= 20.0f; level += 0.05f)
        {
            const auto g = gc.computeGainDb (level);
            if (g > previous + 1.0e-5f)
                monotonic = false;
            previous = g;
        }

        check (monotonic, "reduction increases monotonically with level");
    }

    std::printf ("\n== 9. Decibels round-trip ==\n");
    {
        for (float db : { -60.0f, -20.0f, -6.0f, 0.0f, 6.0f })
            checkClose (gainToDecibels (decibelsToGain (db)), db, 1.0e-3,
                        "round-trip " + std::to_string ((int) db) + " dB");

        checkClose (decibelsToGain (0.0f), 1.0, 1.0e-6, "0 dB is unity gain");
        checkClose (gainToDecibels (0.0f), defaultMinusInfinityDb, 1.0e-6,
                    "silence clamps to -inf, no log10(0)");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

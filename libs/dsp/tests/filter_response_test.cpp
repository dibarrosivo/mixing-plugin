/*
    Tests for FilterResponse.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/fr_test \
            libs/dsp/tests/filter_response_test.cpp && /tmp/fr_test

    The curve on screen has to agree with what the filter actually does. A
    display that is subtly wrong is worse than no display: it sends you tuning
    the wrong thing.
*/

#include <cmath>
#include <cstdio>
#include <string>

#include <dsp/Biquad.h>
#include <dsp/FilterResponse.h>

using namespace dsp;

namespace
{
    int failures = 0;
    constexpr double sampleRate = 48000.0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
        if (! ok) ++failures;
    }

    void checkClose (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-46s expected %+9.4f   got %+9.4f\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }
}

int main()
{
    std::printf ("\n== 1. Magnitude matches the requested peak gain ==\n");
    {
        for (double gainDb : { 12.0, 6.0, -6.0, -15.0 })
        {
            const auto c = BiquadCoefficients::makePeak (sampleRate, 1000.0f, 1.0f, (float) gainDb);
            checkClose (magnitudeDb (c, 1000.0, sampleRate), gainDb, 1.0e-3,
                        "peak " + std::to_string ((int) gainDb) + " dB at f0");
        }
    }

    std::printf ("\n== 2. Flat where the filter does nothing ==\n");
    {
        const auto c = BiquadCoefficients::makePeak (sampleRate, 1000.0f, 8.0f, 12.0f);

        checkClose (magnitudeDb (c, 0.0,      sampleRate), 0.0, 1.0e-3, "DC");
        checkClose (magnitudeDb (c, 24000.0,  sampleRate), 0.0, 1.0e-3, "Nyquist");
        checkClose (magnitudeDb (c, 50.0,     sampleRate), 0.0, 0.05,   "far below a narrow band");
    }

    std::printf ("\n== 3. A flat filter is flat everywhere ==\n");
    {
        const auto c = BiquadCoefficients::makePeak (sampleRate, 1000.0f, 0.707f, 0.0f);
        double worst = 0.0;

        for (double f = 20.0; f < 20000.0; f *= 1.05)
            worst = std::max (worst, (double) std::abs (magnitudeDb (c, f, sampleRate)));

        checkClose (worst, 0.0, 1.0e-4, "worst deviation across the spectrum");
    }

    std::printf ("\n== 4. Log frequency mapping round-trips ==\n");
    {
        for (float f : { 20.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 20000.0f })
        {
            const auto back = normalisedToFrequency (frequencyToNormalised (f));
            checkClose (back, f, f * 0.0001, std::to_string ((int) f) + " Hz round-trip");
        }
    }

    std::printf ("\n== 5. Mapping endpoints and midpoint ==\n");
    {
        checkClose (frequencyToNormalised (20.0f),    0.0, 1.0e-6, "20 Hz is the left edge");
        checkClose (frequencyToNormalised (20000.0f), 1.0, 1.0e-6, "20 kHz is the right edge");

        // Geometric mean of the range sits dead centre on a log axis. If this
        // is wrong the grid and the curve will disagree with each other.
        const auto geometricMean = std::sqrt (20.0f * 20000.0f);
        checkClose (frequencyToNormalised (geometricMean), 0.5, 1.0e-5,
                    "geometric mean is the centre");
    }

    std::printf ("\n== 6. Mapping is monotonic and clamped ==\n");
    {
        bool monotonic = true;
        float previous = -1.0f;

        for (float f = 10.0f; f <= 30000.0f; f *= 1.02f)
        {
            const auto n = frequencyToNormalised (f);
            if (n < previous - 1.0e-7f) monotonic = false;
            previous = n;
        }

        check (monotonic, "normalised position never decreases with frequency");
        checkClose (frequencyToNormalised (5.0f),     0.0, 1.0e-6, "below range clamps to 0");
        checkClose (frequencyToNormalised (40000.0f), 1.0, 1.0e-6, "above range clamps to 1");
    }

    std::printf ("\n== 7. Octaves are evenly spaced ==\n");
    {
        // The defining property of a log axis: equal ratios take equal space.
        // If this drifts, the grid lines will not line up with the decades.
        const auto a = frequencyToNormalised (200.0f)  - frequencyToNormalised (100.0f);
        const auto b = frequencyToNormalised (2000.0f) - frequencyToNormalised (1000.0f);
        const auto c = frequencyToNormalised (8000.0f) - frequencyToNormalised (4000.0f);

        checkClose (b, a, 1.0e-6, "100->200 Hz and 1k->2k occupy equal width");
        checkClose (c, a, 1.0e-6, "100->200 Hz and 4k->8k occupy equal width");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

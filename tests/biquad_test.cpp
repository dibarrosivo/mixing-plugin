/*
    Standalone tests for Biquad — no JUCE, no test framework, no CMake.

        g++ -std=c++20 -O2 -Isource -o /tmp/biquad_test tests/biquad_test.cpp && /tmp/biquad_test

    The strategy is to measure the filter two independent ways and require them
    to agree:

      1. Drive it with a real sine and measure the settled output level. This
         runs processSample(), so it catches difference-equation bugs.
      2. Evaluate |H(e^jw)| directly from the coefficients. This is pure maths
         and never touches the processing loop.

    Either one alone can be confidently wrong. Both agreeing, and both matching
    the gain we asked for, is hard to fake.
*/

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "dsp/Biquad.h"

namespace
{
    int failures = 0;

    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    void check (bool ok, const std::string& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
        if (! ok) ++failures;
    }

    void checkClose (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-46s expected %+7.3f dB   got %+7.3f dB\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    // Steady-state sine, RMS in vs RMS out, after the transient has died.
    double measuredGainDb (Biquad filter, double frequencyHz)
    {
        constexpr int warmup  = 4800;
        constexpr int measure = 48000;

        double sumIn = 0.0, sumOut = 0.0;

        for (int n = 0; n < warmup + measure; ++n)
        {
            const auto x = std::sin (2.0 * pi * frequencyHz * n / sampleRate);
            const auto y = static_cast<double> (filter.processSample (static_cast<float> (x)));

            if (n >= warmup)
            {
                sumIn  += x * x;
                sumOut += y * y;
            }
        }

        return 20.0 * std::log10 (std::sqrt (sumOut) / std::sqrt (sumIn));
    }

    // |H(e^jw)| evaluated straight from the coefficients.
    double analyticGainDb (const BiquadCoefficients& c, double frequencyHz)
    {
        const auto w = 2.0 * pi * frequencyHz / sampleRate;
        const std::complex<double> z1 { std::cos (-w), std::sin (-w) };
        const auto z2 = z1 * z1;

        const auto num = std::complex<double> (c.b0)
                       + std::complex<double> (c.b1) * z1
                       + std::complex<double> (c.b2) * z2;
        const auto den = std::complex<double> (1.0)
                       + std::complex<double> (c.a1) * z1
                       + std::complex<double> (c.a2) * z2;

        return 20.0 * std::log10 (std::abs (num) / std::abs (den));
    }

    void checkRelative (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto error = std::abs (actual - expected) / std::abs (expected);
        const auto ok = error <= tolerance;
        std::printf ("  [%s] %-46s expected %8.2f Hz   got %8.2f Hz  (%.2f%%)\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual, error * 100.0);
        if (! ok) ++failures;
    }

    BiquadCoefficients peakCoeffs (double f0, double q, double gainDb)
    {
        return BiquadCoefficients::makePeak (sampleRate, (float) f0, (float) q, (float) gainDb);
    }

    Biquad makePeak (double f0, double q, double gainDb)
    {
        Biquad b;
        b.setCoefficients (peakCoeffs (f0, q, gainDb));
        return b;
    }

    // Geometric bisection for the frequency where the response crosses targetDb.
    // Geometric rather than arithmetic because the response is symmetric on a
    // log frequency axis, not a linear one.
    double findCrossing (const BiquadCoefficients& c,
                         double lo, double hi, double targetDb,
                         bool risingWithFrequency)
    {
        for (int i = 0; i < 200; ++i)
        {
            const auto mid = std::sqrt (lo * hi);
            const auto g   = analyticGainDb (c, mid);

            if (risingWithFrequency ? (g < targetDb) : (g > targetDb))
                lo = mid;
            else
                hi = mid;
        }

        return std::sqrt (lo * hi);
    }
}

int main()
{
    std::printf ("\n== 1. Zero gain is genuinely transparent ==\n");
    {
        auto filter = makePeak (1000.0, 0.707, 0.0);

        for (double f : { 50.0, 500.0, 1000.0, 5000.0, 15000.0 })
            checkClose (measuredGainDb (filter, f), 0.0, 0.01,
                        "flat at " + std::to_string ((int) f) + " Hz");
    }

    std::printf ("\n== 2. Peak gain lands exactly on the centre frequency ==\n");
    {
        for (double gainDb : { 12.0, 6.0, -6.0, -12.0 })
        {
            auto filter = makePeak (1000.0, 1.0, gainDb);
            checkClose (measuredGainDb (filter, 1000.0), gainDb, 0.02,
                        (gainDb > 0 ? std::string ("boost ") : std::string ("cut   "))
                            + std::to_string ((int) gainDb) + " dB at f0");
        }
    }

    std::printf ("\n== 3. A narrow band leaves the rest of the spectrum alone ==\n");
    {
        auto filter = makePeak (1000.0, 8.0, 12.0);

        for (double f : { 100.0, 250.0, 4000.0, 10000.0 })
            checkClose (measuredGainDb (filter, f), 0.0, 0.5,
                        "Q=8 skirt at " + std::to_string ((int) f) + " Hz");
    }

    std::printf ("\n== 4. Processing loop agrees with the transfer function ==\n");
    {
        const auto c = peakCoeffs (800.0, 2.5, 9.0);
        auto filter = makePeak (800.0, 2.5, 9.0);

        double worst = 0.0;
        for (double f : { 40.0, 200.0, 800.0, 1500.0, 6000.0, 16000.0 })
        {
            const auto measured = measuredGainDb (filter, f);
            const auto analytic = analyticGainDb (c, f);
            worst = std::max (worst, std::abs (measured - analytic));

            std::printf ("      %6.0f Hz   measured %+7.3f   analytic %+7.3f   delta %.4f\n",
                         f, measured, analytic, std::abs (measured - analytic));
        }
        check (worst < 0.01, "worst measured-vs-analytic delta < 0.01 dB");
    }

    std::printf ("\n== 5. Unity at DC and Nyquist (peaking filters must not shift them) ==\n");
    {
        const auto c = peakCoeffs (1000.0, 1.0, 15.0);
        checkClose (analyticGainDb (c, 0.0),               0.0, 0.001, "DC");
        checkClose (analyticGainDb (c, sampleRate / 2.0),  0.0, 0.001, "Nyquist");
    }

    std::printf ("\n== 6. Stable under hostile settings ==\n");
    {
        // High Q at a very low frequency is where a marginally stable
        // implementation blows up or gets stuck ringing.
        auto filter = makePeak (25.0, 10.0, 18.0);

        double last = 0.0;
        bool allFinite = true;

        for (int n = 0; n < 480000; ++n)
        {
            const auto y = filter.processSample (n == 0 ? 1.0f : 0.0f);
            if (! std::isfinite (y)) allFinite = false;
            last = std::abs (y);
        }

        check (allFinite, "impulse response stays finite over 10 s");
        check (last < 1.0e-6, "impulse response has decayed to silence");
    }

    std::printf ("\n== 7. reset() actually clears state ==\n");
    {
        // Compare whole impulse responses, not just the first sample. s2 does
        // not reach the output until sample two, so a one-sample check passes
        // happily while half the state is still dirty.
        auto fresh  = makePeak (1000.0, 4.0, 12.0);
        auto reused = makePeak (1000.0, 4.0, 12.0);

        for (int n = 0; n < 1000; ++n)
            reused.processSample (0.5f);

        reused.reset();

        bool identical = true;
        for (int n = 0; n < 256; ++n)
        {
            const auto impulse = (n == 0 ? 1.0f : 0.0f);
            if (fresh.processSample (impulse) != reused.processSample (impulse))
                identical = false;
        }

        check (identical, "impulse response after reset matches a fresh filter exactly");
    }

    std::printf ("\n== 8. Q actually controls bandwidth ==\n");
    {
        // Peak gain at f0 is gainDb whatever Q is, and the skirt tests are too
        // loose to notice a doubled Q. Without this section the whole suite
        // passes with the wrong alpha, and every EQ band is the wrong width.
        //
        // RBJ defines Q so that the gap between the half-gain points is f0/Q.
        for (auto [f0, q] : { std::pair { 1000.0, 2.0 },
                              std::pair { 1000.0, 4.0 },
                              std::pair {  500.0, 1.0 } })
        {
            const auto gainDb = 12.0;
            const auto c      = peakCoeffs (f0, q, gainDb);
            const auto halfDb = gainDb / 2.0;

            const auto lo = findCrossing (c, f0 / 32.0, f0, halfDb, true);
            const auto hi = findCrossing (c, f0, f0 * 32.0, halfDb, false);

            const auto measured = hi - lo;
            const auto expected = f0 / q;

            std::printf ("      f0=%-6.0f Q=%-4.1f  half-gain points %7.2f .. %7.2f Hz\n",
                         f0, q, lo, hi);
            checkRelative (measured, expected, 0.02,
                           "bandwidth == f0/Q");
        }
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

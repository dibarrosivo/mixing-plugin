/*
    Tests for the Linkwitz-Riley crossover and the multiband splitter.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/lr_test \
            libs/dsp/tests/linkwitz_riley_test.cpp && /tmp/lr_test

    Sections 3 and 4 are the whole point. A crossover whose outputs do not sum
    back to the input colours the signal before any compression happens, and it
    does so by a dB or two in a narrow region — quiet enough to survive casual
    listening and loud enough to make the finished product sound wrong in a way
    nobody can name.

    Everything is measured by pushing real sines through and reading levels, so
    the difference equations are exercised rather than just the coefficients.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/LinkwitzRiley.h>

using namespace dsp;

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

    // Frequencies, not levels — a separate helper so the output does not label
    // 200 Hz as "200 dB".
    void checkCloseHz (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-46s expected %8.1f Hz   got %8.1f Hz\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    void checkClose (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-46s expected %+8.3f dB   got %+8.3f dB\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    // Steady-state level of one output of a two-way split, in dB relative to
    // the input.
    struct SplitLevels { double lowDb, highDb, sumDb; };

    SplitLevels measureSplit (float crossoverHz, double testHz)
    {
        LinkwitzRileySplitter splitter;
        splitter.setCrossoverFrequency (crossoverHz);
        splitter.prepare (sampleRate);

        constexpr int warmup  = 24000;
        constexpr int measure = 48000;

        double inSq = 0.0, lowSq = 0.0, highSq = 0.0, sumSq = 0.0;

        for (int n = 0; n < warmup + measure; ++n)
        {
            const auto x = (float) std::sin (2.0 * pi * testHz * n / sampleRate);

            float low = 0.0f, high = 0.0f;
            splitter.processSample (0, x, low, high);

            if (n >= warmup)
            {
                inSq   += (double) x * x;
                lowSq  += (double) low * low;
                highSq += (double) high * high;
                sumSq  += (double) (low + high) * (low + high);
            }
        }

        const auto toDb = [inSq] (double sq) { return 10.0 * std::log10 (sq / inSq); };
        return { toDb (lowSq), toDb (highSq), toDb (sumSq) };
    }

    // Level of the summed bands of an N-way splitter, in dB relative to input.
    template <size_t N>
    double measureMultibandSumDb (const std::array<float, N - 1>& crossoverHz, double testHz)
    {
        MultibandSplitter<N> splitter;
        splitter.prepare (sampleRate);
        splitter.setCrossoverFrequencies (crossoverHz);

        constexpr int warmup  = 24000;
        constexpr int measure = 48000;

        double inSq = 0.0, sumSq = 0.0;
        std::array<float, N> bands {};

        for (int n = 0; n < warmup + measure; ++n)
        {
            const auto x = (float) std::sin (2.0 * pi * testHz * n / sampleRate);

            splitter.processSample (0, x, bands);

            auto total = 0.0f;
            for (auto band : bands)
                total += band;

            if (n >= warmup)
            {
                inSq  += (double) x * x;
                sumSq += (double) total * total;
            }
        }

        return 10.0 * std::log10 (sumSq / inSq);
    }
}

int main()
{
    std::printf ("\n== 1. Each output is -6 dB at the crossover ==\n");
    {
        // The defining number for Linkwitz-Riley. A Butterworth pair would read
        // -3 dB here and sum to +3 dB.
        const auto levels = measureSplit (1000.0f, 1000.0);

        checkClose (levels.lowDb,  -6.0, 0.1, "low band at the crossover");
        checkClose (levels.highDb, -6.0, 0.1, "high band at the crossover");
    }

    std::printf ("\n== 2. Bands pass their own range and reject the other ==\n");
    {
        const auto low  = measureSplit (1000.0f, 100.0);
        const auto high = measureSplit (1000.0f, 10000.0);

        checkClose (low.lowDb, 0.0, 0.05, "low band passes 100 Hz");
        check (low.highDb < -60.0, "high band rejects 100 Hz by more than 60 dB");

        checkClose (high.highDb, 0.0, 0.05, "high band passes 10 kHz");
        check (high.lowDb < -60.0, "low band rejects 10 kHz by more than 60 dB");
    }

    std::printf ("\n== 3. Two-way sum is flat everywhere ==\n");
    {
        // THE property. If this drifts, a multiband processor with every band
        // at unity is already colouring the signal.
        double worst = 0.0;

        for (double f = 30.0; f < 18000.0; f *= 1.3)
        {
            const auto levels = measureSplit (1000.0f, f);
            worst = std::max (worst, std::abs (levels.sumDb));

            if (std::abs (levels.sumDb) > 0.05)
                std::printf ("        %7.0f Hz  sum %+7.4f dB\n", f, levels.sumDb);
        }

        checkClose (worst, 0.0, 0.05, "worst deviation of low+high from unity");
    }

    std::printf ("\n== 4. Three- and four-way sums are flat too ==\n");
    {
        // This is where a naive cascade fails: the low band skips the upper
        // crossovers and arrives with the wrong phase, digging a hole in the
        // summed response near them.
        {
            const std::array<float, 2> crossovers { 300.0f, 3000.0f };
            double worst = 0.0;

            for (double f = 40.0; f < 16000.0; f *= 1.25)
            {
                const auto sum = measureMultibandSumDb<3> (crossovers, f);
                worst = std::max (worst, std::abs (sum));

                if (std::abs (sum) > 0.1)
                    std::printf ("        3-way  %7.0f Hz  sum %+7.4f dB\n", f, sum);
            }

            checkClose (worst, 0.0, 0.1, "3-way: worst deviation from unity");
        }

        {
            const std::array<float, 3> crossovers { 200.0f, 1200.0f, 6000.0f };
            double worst = 0.0;

            for (double f = 40.0; f < 16000.0; f *= 1.25)
            {
                const auto sum = measureMultibandSumDb<4> (crossovers, f);
                worst = std::max (worst, std::abs (sum));

                if (std::abs (sum) > 0.1)
                    std::printf ("        4-way  %7.0f Hz  sum %+7.4f dB\n", f, sum);
            }

            checkClose (worst, 0.0, 0.1, "4-way: worst deviation from unity");
        }
    }

    std::printf ("\n== 5. Bands land in the right place ==\n");
    {
        MultibandSplitter<3> splitter;
        splitter.prepare (sampleRate);
        splitter.setCrossoverFrequencies ({ 300.0f, 3000.0f });

        auto energyPerBand = [&] (double testHz)
        {
            std::array<double, 3> energy { 0.0, 0.0, 0.0 };
            std::array<float, 3> bands {};

            for (int n = 0; n < 72000; ++n)
            {
                const auto x = (float) std::sin (2.0 * pi * testHz * n / sampleRate);
                splitter.processSample (0, x, bands);

                if (n >= 24000)
                    for (size_t i = 0; i < 3; ++i)
                        energy[i] += (double) bands[i] * bands[i];
            }

            return energy;
        };

        const auto lowTone = energyPerBand (80.0);
        check (lowTone[0] > lowTone[1] * 1000.0 && lowTone[0] > lowTone[2] * 1000.0,
               "80 Hz lands overwhelmingly in the low band");

        splitter.reset();
        const auto midTone = energyPerBand (900.0);
        check (midTone[1] > midTone[0] * 1000.0 && midTone[1] > midTone[2] * 1000.0,
               "900 Hz lands overwhelmingly in the mid band");

        splitter.reset();
        const auto highTone = energyPerBand (9000.0);
        check (highTone[2] > highTone[0] * 1000.0 && highTone[2] > highTone[1] * 1000.0,
               "9 kHz lands overwhelmingly in the high band");
    }

    std::printf ("\n== 6. Crossover frequencies are forced into order ==\n");
    {
        // Checking the SUM here would prove nothing: an LR split is
        // complementary whatever order the crossovers are in, so the sum stays
        // flat even when the bands are scrambled. What ordering protects is
        // that each band holds its intended range, so assert the ordering
        // itself.
        MultibandSplitter<4> splitter;
        splitter.prepare (sampleRate);
        splitter.setCrossoverFrequencies ({ 6000.0f, 300.0f, 1500.0f });   // scrambled

        const auto first  = splitter.getCrossoverFrequency (0);
        const auto second = splitter.getCrossoverFrequency (1);
        const auto third  = splitter.getCrossoverFrequency (2);

        std::printf ("        requested 6000/300/1500 -> using %.0f/%.0f/%.0f Hz\n",
                     (double) first, (double) second, (double) third);

        check (second > first,  "second crossover ends up above the first");
        check (third  > second, "third crossover ends up above the second");

        // And sensible input must pass through untouched.
        MultibandSplitter<4> ordered;
        ordered.prepare (sampleRate);
        ordered.setCrossoverFrequencies ({ 200.0f, 1200.0f, 6000.0f });

        checkCloseHz (ordered.getCrossoverFrequency (0), 200.0,  0.01, "ordered input kept as-is (1)");
        checkCloseHz (ordered.getCrossoverFrequency (1), 1200.0, 0.01, "ordered input kept as-is (2)");
        checkCloseHz (ordered.getCrossoverFrequency (2), 6000.0, 0.01, "ordered input kept as-is (3)");
    }

    std::printf ("\n== 7. All-pass is unity magnitude everywhere ==\n");
    {
        AllPassSection allPass;
        allPass.setFrequency (1000.0f);
        allPass.prepare (sampleRate);

        double worst = 0.0;

        for (double f = 30.0; f < 18000.0; f *= 1.4)
        {
            allPass.reset();

            double inSq = 0.0, outSq = 0.0;

            for (int n = 0; n < 72000; ++n)
            {
                const auto x = (float) std::sin (2.0 * pi * f * n / sampleRate);
                const auto y = allPass.processSample (0, x);

                if (n >= 24000)
                {
                    inSq  += (double) x * x;
                    outSq += (double) y * y;
                }
            }

            worst = std::max (worst, std::abs (10.0 * std::log10 (outSq / inSq)));
        }

        checkClose (worst, 0.0, 0.02, "worst magnitude deviation across the spectrum");
    }

    std::printf ("\n== 8. reset() clears the filters ==\n");
    {
        MultibandSplitter<3> fresh, reused;
        fresh.prepare (sampleRate);
        reused.prepare (sampleRate);
        fresh.setCrossoverFrequencies ({ 300.0f, 3000.0f });
        reused.setCrossoverFrequencies ({ 300.0f, 3000.0f });

        std::array<float, 3> bands {};

        for (int n = 0; n < 5000; ++n)
            reused.processSample (0, (float) std::sin (0.1 * n), bands);

        reused.reset();

        bool identical = true;
        std::array<float, 3> a {}, b {};

        for (int n = 0; n < 2000; ++n)
        {
            const auto x = (n == 0) ? 1.0f : 0.0f;
            fresh.processSample (0, x, a);
            reused.processSample (0, x, b);

            for (size_t i = 0; i < 3; ++i)
                if (a[i] != b[i]) identical = false;
        }

        check (identical, "impulse response after reset matches a fresh splitter");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

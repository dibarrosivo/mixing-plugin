/*
    Tests for OpticalCompressor.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/opto_test \
            libs/dsp/tests/optical_compressor_test.cpp && /tmp/opto_test

    Sections 4 and 5 are the reason this class exists. Everything else here is
    ordinary compressor behaviour that Compressor already provides; what makes
    an opto an opto is that recovery depends on what just happened. A short
    transient must let go quickly and a sustained passage must not, and the two
    have to differ by a lot — if they differ by 20% nobody will ever hear it and
    the whole model is decoration.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/OpticalCompressor.h>

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

    void checkClose (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-48s expected %+8.3f   got %+8.3f\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    OpticalCompressor::Settings optoSettings (float thresholdDb, float ratio)
    {
        OpticalCompressor::Settings s;
        s.enabled       = true;
        s.thresholdDb   = thresholdDb;
        s.ratio         = ratio;
        s.kneeDb        = 0.0f;
        s.makeupGainDb  = 0.0f;
        s.detectorMs    = 3.0f;
        s.detectorMode  = EnvelopeFollower::Mode::rms;
        s.attackMs      = 10.0f;
        s.releaseFastMs = 80.0f;
        s.releaseSlowMs = 1500.0f;
        s.programDepth  = 1.0f;
        return s;
    }

    // Drive with a tone for `driveSeconds`, then feed silence and report how
    // many milliseconds it takes for the reduction to recover to `targetDb`.
    double recoveryMs (OpticalCompressor& compressor, float amplitude,
                       double driveSeconds, float targetDb)
    {
        constexpr int blockSize = 32;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        int phase = 0;

        for (int n = 0; n < (int) (driveSeconds * sampleRate); n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * 200.0 * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;
            }

            compressor.process (channels, 2, blockSize);
        }

        // Now silence. Count until the reduction has climbed back to target.
        for (int n = 0; n < (int) (20.0 * sampleRate); n += blockSize)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            compressor.process (channels, 2, blockSize);

            if (compressor.getGainReductionDb() > targetDb)
                return 1000.0 * (double) n / sampleRate;
        }

        return 20000.0;   // never recovered
    }

    // Reduction reached `afterMs` into a tone starting from silence.
    double reductionAfterOnset (float attackMs, float amplitude, double afterMs)
    {
        OpticalCompressor compressor;
        auto settings = optoSettings (-30.0f, 6.0f);
        settings.attackMs = attackMs;
        compressor.setSettings (settings);
        compressor.prepare (sampleRate);

        constexpr int blockSize = 16;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        int phase = 0;

        for (int n = 0; n < (int) (afterMs * 0.001 * sampleRate); n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * 200.0 * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;
            }

            compressor.process (channels, 2, blockSize);
        }

        return compressor.getGainReductionDb();
    }

    double settledReductionDb (OpticalCompressor& compressor, float amplitude,
                               double seconds = 2.0)
    {
        constexpr int blockSize = 64;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        int phase = 0;

        for (int n = 0; n < (int) (seconds * sampleRate); n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * 200.0 * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;
            }

            compressor.process (channels, 2, blockSize);
        }

        return compressor.getGainReductionDb();
    }
}

int main()
{
    std::printf ("\n== 1. Disabled is a wire ==\n");
    {
        OpticalCompressor compressor;
        auto settings = optoSettings (-40.0f, 8.0f);
        settings.enabled = false;
        compressor.setSettings (settings);
        compressor.prepare (sampleRate);

        std::vector<float> left { 0.5f, -0.25f, 0.75f };
        std::vector<float> right = left;
        const auto original = left;
        float* channels[2] = { left.data(), right.data() };

        compressor.process (channels, 2, (int) left.size());

        check (left == original, "samples pass through untouched");
        checkClose (compressor.getGainReductionDb(), 0.0, 1.0e-9, "no reduction reported");
    }

    std::printf ("\n== 2. Below threshold, nothing happens ==\n");
    {
        OpticalCompressor compressor;
        compressor.setSettings (optoSettings (-12.0f, 4.0f));
        compressor.prepare (sampleRate);

        checkClose (settledReductionDb (compressor, 0.005f), 0.0, 0.05,
                    "quiet signal produces no reduction");
    }

    std::printf ("\n== 3. Cut only, never boost ==\n");
    {
        OpticalCompressor compressor;
        compressor.setSettings (optoSettings (-30.0f, 6.0f));
        compressor.prepare (sampleRate);

        constexpr int blockSize = 32;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        bool everPositive = false;
        int phase = 0;

        for (int block = 0; block < 4000; ++block)
        {
            const auto amplitude = (float) std::abs (std::sin (block * 0.0015));

            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * 200.0 * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;
            }

            compressor.process (channels, 2, blockSize);

            if (compressor.getGainReductionDb() > 1.0e-6f)
                everPositive = true;
        }

        check (! everPositive, "reduction never goes positive across a full sweep");
    }

    std::printf ("\n== 4. Release is program-dependent ==\n");
    {
        // THE defining property. Same settings, same tone, same level — only
        // the duration differs, and recovery must differ a lot as a result.
        OpticalCompressor brief, sustained;
        brief.setSettings (optoSettings (-30.0f, 6.0f));
        sustained.setSettings (optoSettings (-30.0f, 6.0f));
        brief.prepare (sampleRate);
        sustained.prepare (sampleRate);

        const auto briefMs     = recoveryMs (brief,     0.5f, 0.06, -1.0f);
        const auto sustainedMs = recoveryMs (sustained, 0.5f, 6.00, -1.0f);

        std::printf ("        60 ms of tone -> recovers in %6.0f ms\n", briefMs);
        std::printf ("         6 s  of tone -> recovers in %6.0f ms\n", sustainedMs);

        check (briefMs < 400.0, "a short burst lets go quickly");
        check (sustainedMs > briefMs * 3.0,
               "a sustained passage takes at least 3x as long to let go");
    }

    std::printf ("\n== 4b. Attack time controls how fast it clamps down ==\n");
    {
        /*  Everything else here measures steady state or recovery, and none of
            it changes if the attack smoothing is removed entirely — the
            compressor still ends up in the same place, it just gets there
            instantly. Mutation testing found exactly that hole.
        */
        const auto fastEarly = reductionAfterOnset (1.0f,   0.5f, 10.0);
        const auto slowEarly = reductionAfterOnset (100.0f, 0.5f, 10.0);

        OpticalCompressor reference;
        auto settings = optoSettings (-30.0f, 6.0f);
        settings.attackMs = 100.0f;
        reference.setSettings (settings);
        reference.prepare (sampleRate);
        const auto steadyState = settledReductionDb (reference, 0.5f, 3.0);

        std::printf ("        after 10 ms:  1 ms attack %+7.2f dB,  100 ms attack %+7.2f dB"
                     "  (steady state %+7.2f dB)\n", fastEarly, slowEarly, steadyState);

        check (fastEarly < slowEarly - 5.0,
               "a 1 ms attack has clamped far harder at 10 ms than a 100 ms one");
        check (slowEarly > steadyState * 0.5,
               "a 100 ms attack is still well short of steady state at 10 ms");
    }

    std::printf ("\n== 5. programDepth = 0 removes the effect ==\n");
    {
        // With the memory ignored the two cases must converge — that is what
        // makes programDepth a real control rather than a decorative one.
        auto measure = [] (double driveSeconds)
        {
            OpticalCompressor compressor;
            auto settings = optoSettings (-30.0f, 6.0f);
            settings.programDepth = 0.0f;
            compressor.setSettings (settings);
            compressor.prepare (sampleRate);

            return recoveryMs (compressor, 0.5f, driveSeconds, -1.0f);
        };

        const auto briefMs     = measure (0.06);
        const auto sustainedMs = measure (6.00);

        std::printf ("        depth 0:  60 ms -> %6.0f ms,   6 s -> %6.0f ms\n",
                     briefMs, sustainedMs);

        check (std::abs (sustainedMs - briefMs) < briefMs * 0.5 + 20.0,
               "with depth 0, duration barely changes recovery");
    }

    std::printf ("\n== 6. Steady-state reduction follows the static curve ==\n");
    {
        // Held long enough that the attack has finished, so what is left is
        // just threshold and ratio.
        for (float amplitude : { 0.5f, 0.2f })
        {
            OpticalCompressor compressor;
            auto settings = optoSettings (-24.0f, 4.0f);
            settings.detectorMs = 200.0f;   // symmetric and long: a true average
            compressor.setSettings (settings);
            compressor.prepare (sampleRate);

            const auto reduction = settledReductionDb (compressor, amplitude, 4.0);

            GainComputer reference;
            reference.setThresholdDb (-24.0f);
            reference.setRatio (4.0f);
            reference.setKneeWidthDb (0.0f);

            const auto expected = reference.computeGainDb (
                gainToDecibels (amplitude / std::sqrt (2.0f)));

            char label[72];
            std::snprintf (label, sizeof (label), "amplitude %.2f", (double) amplitude);
            checkClose (reduction, expected, 0.2, label);
        }
    }

    std::printf ("\n== 7. The AUDIO is reduced, not just the meter ==\n");
    {
        OpticalCompressor compressor;
        auto settings = optoSettings (-24.0f, 4.0f);
        settings.detectorMs = 200.0f;
        compressor.setSettings (settings);
        compressor.prepare (sampleRate);

        constexpr int blockSize = 64;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        double inSq = 0.0, outSq = 0.0;
        int phase = 0;

        for (int n = 0; n < (int) (5.0 * sampleRate); n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = 0.5f * (float) std::sin (2.0 * pi * 200.0 * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;

                if (n >= (int) (4.0 * sampleRate))
                    inSq += (double) x * x;
            }

            compressor.process (channels, 2, blockSize);

            if (n >= (int) (4.0 * sampleRate))
                for (int i = 0; i < blockSize; ++i)
                    outSq += (double) left[(size_t) i] * left[(size_t) i];
        }

        const auto measuredDb = 10.0 * std::log10 (outSq / inSq);

        check (measuredDb < -1.0, "output is quieter than input");
        checkClose (measuredDb, compressor.getGainReductionDb(), 0.3,
                    "measured change equals the reported reduction");
    }

    std::printf ("\n== 8. Makeup gain is applied but not metered ==\n");
    {
        auto measure = [] (float makeupDb)
        {
            OpticalCompressor compressor;
            auto settings = optoSettings (-24.0f, 4.0f);
            settings.detectorMs = 200.0f;
            settings.makeupGainDb = makeupDb;
            compressor.setSettings (settings);
            compressor.prepare (sampleRate);

            constexpr int blockSize = 64;
            std::vector<float> left (blockSize), right (blockSize);
            float* channels[2] = { left.data(), right.data() };

            double inSq = 0.0, outSq = 0.0;
            int phase = 0;

            for (int n = 0; n < (int) (5.0 * sampleRate); n += blockSize)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const auto x = 0.5f * (float) std::sin (2.0 * pi * 200.0 * phase / sampleRate);
                    left[(size_t) i] = x;
                    right[(size_t) i] = x;

                    if (n >= (int) (4.0 * sampleRate)) inSq += (double) x * x;
                }

                compressor.process (channels, 2, blockSize);

                if (n >= (int) (4.0 * sampleRate))
                    for (int i = 0; i < blockSize; ++i)
                        outSq += (double) left[(size_t) i] * left[(size_t) i];
            }

            struct R { double changeDb, reductionDb; };
            return R { 10.0 * std::log10 (outSq / inSq), compressor.getGainReductionDb() };
        };

        const auto plain  = measure (0.0f);
        const auto boosted = measure (6.0f);

        checkClose (boosted.changeDb - plain.changeDb, 6.0, 0.1, "output is 6 dB louder");
        checkClose (boosted.reductionDb, plain.reductionDb, 0.05, "meter is unaffected");
    }

    std::printf ("\n== 9. Program memory is reported and bounded ==\n");
    {
        OpticalCompressor compressor;
        compressor.setSettings (optoSettings (-40.0f, 10.0f));
        compressor.prepare (sampleRate);

        checkClose (compressor.getProgramMemory(), 0.0, 1.0e-9, "starts at zero");

        settledReductionDb (compressor, 0.7f, 5.0);
        const auto loaded = compressor.getProgramMemory();

        std::printf ("        after 5 s of heavy compression: memory = %.3f\n", loaded);
        check (loaded > 0.5f, "builds under sustained compression");
        check (loaded <= 1.0f, "never exceeds 1");

        compressor.reset();
        checkClose (compressor.getProgramMemory(), 0.0, 1.0e-9, "cleared by reset");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

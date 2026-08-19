/*
    Tests for DynamicEqBand.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/dyn_test \
            libs/dsp/tests/dynamic_eq_band_test.cpp && /tmp/dyn_test

    The thing that makes this a dynamic EQ rather than a compressor with a
    filter bolted on is section 6: the detector must hear only the band it
    controls. A sidechain that is accidentally broadband produces something that
    ducks the whole spectrum whenever a kick lands, sounds wrong, and looks
    completely correct in the code.

    Section 2 is the other load-bearing one — with dynamics off, the band must
    be bit-identical to a plain peaking filter. Any drift there means the static
    and dynamic paths have diverged.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/DynamicEqBand.h>
#include <dsp/GainComputer.h>

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

    // Drive a band with a stereo sine and report the reduction it settles on.
    float settledReductionDb (DynamicEqBand& band, float frequencyHz, float amplitude,
                              double seconds = 1.0)
    {
        constexpr int blockSize = 64;
        const auto totalSamples = (int) (seconds * sampleRate);

        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        int phase = 0;

        for (int n = 0; n < totalSamples; n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * frequencyHz * phase / sampleRate);
                left[(size_t) i]  = x;
                right[(size_t) i] = x;
            }

            band.process (channels, 2, blockSize);
        }

        return band.getGainReductionDb();
    }

    DynamicEqBand::Settings dynamicSettings (float frequencyHz, float q,
                                             float thresholdDb, float ratio)
    {
        DynamicEqBand::Settings s;
        s.frequencyHz    = frequencyHz;
        s.q              = q;
        s.staticGainDb   = 0.0f;
        s.enabled        = true;
        s.dynamicEnabled = true;
        s.thresholdDb    = thresholdDb;
        s.ratio          = ratio;
        s.kneeDb         = 0.0f;
        s.attackMs       = 5.0f;
        s.releaseMs      = 80.0f;
        return s;
    }
}

int main()
{
    std::printf ("\n== 1. A disabled band is a wire ==\n");
    {
        DynamicEqBand band;
        band.prepare (sampleRate);

        auto settings = dynamicSettings (1000.0f, 2.0f, -30.0f, 4.0f);
        settings.enabled = false;
        band.setSettings (settings);

        std::vector<float> left { 0.5f, -0.25f, 0.75f, 1.0f };
        std::vector<float> right = left;
        const auto original = left;
        float* channels[2] = { left.data(), right.data() };

        band.process (channels, 2, (int) left.size());

        check (left == original, "samples pass through untouched");
    }

    std::printf ("\n== 2. Dynamics off == a plain peaking filter ==\n");
    {
        // If these ever diverge, the static and dynamic paths have drifted
        // apart and every static preset changes meaning.
        DynamicEqBand band;
        band.prepare (sampleRate);

        DynamicEqBand::Settings settings;
        settings.frequencyHz    = 800.0f;
        settings.q              = 1.5f;
        settings.staticGainDb   = 6.0f;
        settings.enabled        = true;
        settings.dynamicEnabled = false;
        band.setSettings (settings);

        Biquad reference;
        reference.setCoefficients (
            BiquadCoefficients::makePeak (sampleRate, 800.0f, 1.5f, 6.0f));

        constexpr int numSamples = 4096;
        std::vector<float> left ((size_t) numSamples), right ((size_t) numSamples);
        std::vector<float> expected ((size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto x = (float) std::sin (2.0 * pi * 800.0 * i / sampleRate);
            left[(size_t) i]  = x;
            right[(size_t) i] = x;
            expected[(size_t) i] = reference.processSample (x);
        }

        float* channels[2] = { left.data(), right.data() };
        band.process (channels, 2, numSamples);

        auto worst = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            worst = std::max (worst, std::abs (left[(size_t) i] - expected[(size_t) i]));

        checkClose (worst, 0.0, 1.0e-6, "worst sample difference vs a plain Biquad");
        checkClose (band.getGainReductionDb(), 0.0, 1.0e-9, "no reduction reported");
    }

    std::printf ("\n== 3. Below threshold, the band does nothing ==\n");
    {
        DynamicEqBand band;
        band.prepare (sampleRate);
        band.setSettings (dynamicSettings (1000.0f, 2.0f, -12.0f, 4.0f));

        // -40 dBFS against a -12 dB threshold.
        const auto reduction = settledReductionDb (band, 1000.0f, 0.01f);
        checkClose (reduction, 0.0, 1.0e-4, "quiet signal produces no reduction");
    }

    std::printf ("\n== 4. Above threshold, reduction matches the static curve ==\n");
    {
        // The band-pass has unity gain at its centre, so a sine at f0 reaches
        // the detector at its own amplitude, and the expected reduction is
        // exactly what GainComputer returns for that level.
        //
        // The attack is deliberately far shorter than the signal period. A
        // "peak" detector with a 5 ms attack cannot reach the peak of a 1 kHz
        // sine — the rectified peaks arrive every 0.5 ms, so the envelope
        // settles somewhere between the peak and the mean, biasing the
        // reduction by well over half a dB. That is correct behaviour, but it
        // means only a genuinely fast attack lets this assertion be exact.
        for (float amplitude : { 0.5f, 0.25f, 0.125f })
        {
            DynamicEqBand band;
            band.prepare (sampleRate);

            auto settings = dynamicSettings (1000.0f, 2.0f, -24.0f, 4.0f);
            settings.attackMs = 0.05f;
            band.setSettings (settings);

            const auto reduction = settledReductionDb (band, 1000.0f, amplitude);

            GainComputer reference;
            reference.setThresholdDb (-24.0f);
            reference.setRatio (4.0f);
            reference.setKneeWidthDb (0.0f);

            const auto expected = reference.computeGainDb (gainToDecibels (amplitude));

            char label[64];
            std::snprintf (label, sizeof (label), "amplitude %.3f (%.1f dBFS)",
                           (double) amplitude, (double) gainToDecibels (amplitude));

            checkClose (reduction, expected, 0.05, label);
        }
    }

    std::printf ("\n== 4b. Soft knee, through the whole chain ==\n");
    {
        // Every other dynamic section uses a hard knee, which never enters the
        // knee branch at all. A soft knee is what anyone actually uses on a
        // vocal, so it needs exercising end to end and not only in the
        // GainComputer unit test.
        constexpr float kneeDb = 12.0f;

        for (float amplitude : { 0.35f, 0.06f })
        {
            DynamicEqBand band;
            band.prepare (sampleRate);

            auto settings = dynamicSettings (1000.0f, 2.0f, -24.0f, 4.0f);
            settings.kneeDb   = kneeDb;
            settings.attackMs = 0.05f;
            band.setSettings (settings);

            const auto reduction = settledReductionDb (band, 1000.0f, amplitude);

            GainComputer reference;
            reference.setThresholdDb (-24.0f);
            reference.setRatio (4.0f);
            reference.setKneeWidthDb (kneeDb);

            const auto expected = reference.computeGainDb (gainToDecibels (amplitude));

            char label[72];
            std::snprintf (label, sizeof (label), "knee %.0f dB at %.1f dBFS",
                           (double) kneeDb, (double) gainToDecibels (amplitude));

            checkClose (reduction, expected, 0.05, label);
            check (reduction <= 0.0f, "still a cut inside the knee");
        }
    }

    std::printf ("\n== 5. Cut only, never boost ==\n");
    {
        // The client asked for a subtractive EQ. A positive value here would be
        // the band amplifying the resonance it is supposed to tame.
        DynamicEqBand band;
        band.prepare (sampleRate);
        band.setSettings (dynamicSettings (1000.0f, 2.0f, -30.0f, 6.0f));

        constexpr int blockSize = 64;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        bool everPositive = false;
        int phase = 0;

        // Sweep amplitude from silence to full scale and back.
        for (int block = 0; block < 3000; ++block)
        {
            const auto amplitude = (float) std::abs (std::sin (block * 0.002));

            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * 1000.0 * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;
            }

            band.process (channels, 2, blockSize);

            if (band.getGainReductionDb() > 1.0e-6f)
                everPositive = true;
        }

        check (! everPositive, "reduction never goes positive across a full sweep");
    }

    std::printf ("\n== 6. The sidechain only hears its own band ==\n");
    {
        // THE defining property. A broadband sidechain gives you a compressor
        // wired to a filter, which ducks the whole spectrum on every kick.
        DynamicEqBand inBand, outOfBand;
        inBand.prepare (sampleRate);
        outOfBand.prepare (sampleRate);

        const auto settings = dynamicSettings (200.0f, 4.0f, -24.0f, 6.0f);
        inBand.setSettings (settings);
        outOfBand.setSettings (settings);

        const auto atCentre  = settledReductionDb (inBand,    200.0f, 0.5f);
        const auto farAway   = settledReductionDb (outOfBand, 8000.0f, 0.5f);

        check (atCentre < -8.0f, "a loud tone inside the band is reduced");
        checkClose (farAway, 0.0, 0.5, "the same tone two decades away is ignored");
    }

    std::printf ("\n== 7. Attack time changes how fast it clamps down ==\n");
    {
        auto reductionAfter = [] (float attackMs, double seconds)
        {
            DynamicEqBand band;
            band.prepare (sampleRate);

            auto settings = dynamicSettings (1000.0f, 2.0f, -30.0f, 6.0f);
            settings.attackMs = attackMs;
            band.setSettings (settings);

            return settledReductionDb (band, 1000.0f, 0.5f, seconds);
        };

        const auto fastEarly = reductionAfter (1.0f,   0.010);
        const auto slowEarly = reductionAfter (100.0f, 0.010);

        check (fastEarly < slowEarly - 3.0f,
               "after 10 ms, a 1 ms attack has clamped far harder than a 100 ms one");

        // Each setting is compared against its OWN steady state, not against
        // each other. Attack time changes where a peak detector settles on a
        // periodic signal — a slow attack never fully climbs to the peak
        // between cycles — so the two do not converge, and asserting that they
        // do encodes a wrong model of what a detector does.
        const auto fastLate = reductionAfter (1.0f,   1.5);
        const auto slowLate = reductionAfter (100.0f, 1.5);

        checkClose (fastEarly, fastLate, 0.5,
                    "a 1 ms attack is already at its steady state by 10 ms");

        check (std::abs (slowEarly - slowLate) > 3.0,
               "a 100 ms attack is nowhere near its steady state at 10 ms");

        check (slowLate < -5.0f, "the slow attack does eventually clamp");
    }

    std::printf ("\n== 8. Release lets the band back up ==\n");
    {
        DynamicEqBand band;
        band.prepare (sampleRate);

        auto settings = dynamicSettings (1000.0f, 2.0f, -30.0f, 6.0f);
        settings.releaseMs = 30.0f;
        band.setSettings (settings);

        const auto whileLoud = settledReductionDb (band, 1000.0f, 0.5f, 0.5);
        check (whileLoud < -8.0f, "clamped while the tone plays");

        // Now feed silence and let it recover.
        constexpr int blockSize = 64;
        std::vector<float> left (blockSize, 0.0f), right (blockSize, 0.0f);
        float* channels[2] = { left.data(), right.data() };

        for (int n = 0; n < (int) (0.5 * sampleRate); n += blockSize)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            band.process (channels, 2, blockSize);
        }

        checkClose (band.getGainReductionDb(), 0.0, 0.01, "recovered to unity after silence");
    }

    std::printf ("\n== 9. The AUDIO is reduced, not just the meter ==\n");
    {
        // Everything above this point reads getGainReductionDb(). That is the
        // meter, not the signal — a sign error in how the reduction reaches the
        // filter would boost the resonance while still reporting a clean cut.
        // Mutation testing found exactly that hole, so this section measures
        // what actually comes out.
        DynamicEqBand band;
        band.prepare (sampleRate);
        band.setSettings (dynamicSettings (1000.0f, 2.0f, -24.0f, 4.0f));

        constexpr int blockSize = 64;
        constexpr float amplitude = 0.5f;

        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        double inputSumOfSquares = 0.0, outputSumOfSquares = 0.0;
        long long measuredSamples = 0;

        const auto settleSamples  = (int) (1.0 * sampleRate);
        const auto measureSamples = (int) (0.3 * sampleRate);
        int phase = 0;

        for (int n = 0; n < settleSamples + measureSamples; n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * 1000.0 * phase / sampleRate);
                left[(size_t) i]  = x;
                right[(size_t) i] = x;

                if (n >= settleSamples)
                    inputSumOfSquares += (double) x * x;
            }

            band.process (channels, 2, blockSize);

            if (n >= settleSamples)
            {
                for (int i = 0; i < blockSize; ++i)
                    outputSumOfSquares += (double) left[(size_t) i] * left[(size_t) i];

                measuredSamples += blockSize;
            }
        }

        const auto measuredDb = 20.0 * std::log10 (std::sqrt (outputSumOfSquares)
                                                 / std::sqrt (inputSumOfSquares));

        check (measuredSamples > 0, "measured a non-empty window");
        check (measuredDb < -1.0, "output is quieter than input — an actual cut");

        // At the centre frequency the peaking filter's gain IS the effective
        // gain, so the measured change must equal the reported reduction.
        checkClose (measuredDb, band.getGainReductionDb(), 0.5,
                    "measured output change equals the reported reduction");
    }

    std::printf ("\n== 10. reset() clears the detector ==\n");
    {
        DynamicEqBand band;
        band.prepare (sampleRate);
        band.setSettings (dynamicSettings (1000.0f, 2.0f, -30.0f, 6.0f));

        settledReductionDb (band, 1000.0f, 0.5f, 0.3);
        check (band.getGainReductionDb() < -5.0f, "clamped before reset");

        band.reset();
        checkClose (band.getGainReductionDb(), 0.0, 1.0e-9, "reduction cleared by reset");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

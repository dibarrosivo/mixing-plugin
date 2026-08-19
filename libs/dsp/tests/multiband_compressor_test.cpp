/*
    Tests for Compressor and MultibandCompressor.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/mb_test \
            libs/dsp/tests/multiband_compressor_test.cpp && /tmp/mb_test

    Section 5 is the one that matters most: with every band bypassed, a
    multiband must be indistinguishable from a wire. If it is not, the processor
    colours everything it touches before any compression happens, and that
    colour is baked into every preset anyone ever makes with it.

    Sections 1-4 pin the compressor itself, and they measure the AUDIO, not just
    the meter. Mutation testing on the dynamic EQ band showed that asserting on
    the reported gain reduction lets a sign error through — the meter says "cut"
    while the signal gets louder.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/MultibandCompressor.h>

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
        std::printf ("  [%s] %-46s expected %+8.3f   got %+8.3f\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    struct Measurement { double changeDb; double reductionDb; };

    // Push a stereo sine through a broadband compressor and measure what
    // actually comes out, in dB relative to what went in.
    Measurement measureCompressor (Compressor& compressor, double frequencyHz,
                                   float amplitude, double seconds = 1.2,
                                   double measureFrom = 0.9)
    {
        constexpr int blockSize = 64;
        const auto total = (int) (seconds * sampleRate);
        const auto from  = (int) (measureFrom * sampleRate);

        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        double inSq = 0.0, outSq = 0.0;
        int phase = 0;

        for (int n = 0; n < total; n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * frequencyHz * phase / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;

                if (n >= from)
                    inSq += (double) x * x;
            }

            compressor.process (channels, 2, blockSize);

            if (n >= from)
                for (int i = 0; i < blockSize; ++i)
                    outSq += (double) left[(size_t) i] * left[(size_t) i];
        }

        return { 10.0 * std::log10 (outSq / inSq), compressor.getGainReductionDb() };
    }

    /*  Amplitude of one frequency component, by quadrature detection.

        Needed because a two-tone test cannot be judged by total energy: the
        quiet tone being ducked barely moves the sum when a loud one dominates.
    */
    double toneAmplitude (const std::vector<float>& samples, double frequencyHz, int from)
    {
        double real = 0.0, imag = 0.0;
        int count = 0;

        for (size_t n = (size_t) from; n < samples.size(); ++n, ++count)
        {
            const auto phase = 2.0 * pi * frequencyHz * (double) n / sampleRate;
            real += samples[n] * std::cos (phase);
            imag += samples[n] * std::sin (phase);
        }

        if (count == 0)
            return 0.0;

        return 2.0 * std::sqrt (real * real + imag * imag) / count;
    }

    Compressor::Settings compressorSettings (float thresholdDb, float ratio)
    {
        Compressor::Settings s;
        s.enabled      = true;
        s.thresholdDb  = thresholdDb;
        s.ratio        = ratio;
        s.kneeDb       = 0.0f;
        s.attackMs     = 5.0f;
        s.releaseMs    = 100.0f;
        s.makeupGainDb = 0.0f;
        s.detectorMode = EnvelopeFollower::Mode::rms;
        return s;
    }
}

int main()
{
    std::printf ("\n== 1. A disabled compressor is a wire ==\n");
    {
        Compressor compressor;
        auto settings = compressorSettings (-40.0f, 8.0f);
        settings.enabled = false;
        compressor.setSettings (settings);
        compressor.prepare (sampleRate);

        const auto result = measureCompressor (compressor, 1000.0, 0.5f);

        checkClose (result.changeDb, 0.0, 1.0e-4, "output level unchanged");
        checkClose (result.reductionDb, 0.0, 1.0e-9, "no reduction reported");
    }

    std::printf ("\n== 2. Below threshold, nothing happens ==\n");
    {
        Compressor compressor;
        compressor.setSettings (compressorSettings (-12.0f, 4.0f));
        compressor.prepare (sampleRate);

        const auto result = measureCompressor (compressor, 1000.0, 0.005f);

        checkClose (result.changeDb, 0.0, 0.01, "quiet signal passes untouched");
        checkClose (result.reductionDb, 0.0, 0.01, "no reduction reported");
    }

    std::printf ("\n== 3. Above threshold: the AUDIO is reduced by the reported amount ==\n");
    {
        /*  Symmetric, long time constants on purpose.

            RMS mode smooths the squared signal with the same asymmetric attack
            and release as peak mode. With a slow release the envelope creeps
            toward the PEAK of the squared signal rather than its mean, and a
            sine then reads nearly 3 dB hotter than amplitude/sqrt(2) — which
            at 4:1 is almost 2 dB of unexplained extra reduction.

            Matching attack to release makes it a genuine average, so the level
            the detector sees is exactly amplitude/sqrt(2) and the expected
            reduction can be computed rather than fudged. See EnvelopeFollower.h.
        */
        for (float amplitude : { 0.5f, 0.2f })
        {
            Compressor compressor;
            auto settings = compressorSettings (-24.0f, 4.0f);
            settings.attackMs  = 200.0f;
            settings.releaseMs = 200.0f;
            compressor.setSettings (settings);
            compressor.prepare (sampleRate);

            const auto result = measureCompressor (compressor, 1000.0, amplitude, 3.0, 2.4);

            GainComputer reference;
            reference.setThresholdDb (-24.0f);
            reference.setRatio (4.0f);
            reference.setKneeWidthDb (0.0f);

            const auto rmsLevelDb = gainToDecibels (amplitude / std::sqrt (2.0f));
            const auto expected   = reference.computeGainDb (rmsLevelDb);

            char label[72];
            std::snprintf (label, sizeof (label), "amplitude %.2f, measured output change",
                           (double) amplitude);

            checkClose (result.changeDb, expected, 0.2, label);
            checkClose (result.reductionDb, expected, 0.2, "  and the meter agrees");
        }
    }

    std::printf ("\n== 4. Makeup gain is applied but not metered ==\n");
    {
        // The meter should show what the compressor took away, not the net
        // change — otherwise 6 dB of reduction with 6 dB of makeup reads as
        // "doing nothing".
        Compressor compressor;
        auto settings = compressorSettings (-24.0f, 4.0f);
        settings.makeupGainDb = 6.0f;
        compressor.setSettings (settings);
        compressor.prepare (sampleRate);

        const auto withMakeup = measureCompressor (compressor, 1000.0, 0.5f);

        Compressor plain;
        plain.setSettings (compressorSettings (-24.0f, 4.0f));
        plain.prepare (sampleRate);

        const auto without = measureCompressor (plain, 1000.0, 0.5f);

        checkClose (withMakeup.changeDb - without.changeDb, 6.0, 0.05,
                    "output is 6 dB louder with makeup");
        checkClose (withMakeup.reductionDb, without.reductionDb, 0.01,
                    "meter is unaffected by makeup");
    }

    std::printf ("\n== 5. Bypassed multiband is indistinguishable from a wire ==\n");
    {
        // THE test. Every band off, so the only thing acting is the crossover.
        MultibandCompressor<4> multiband;
        multiband.prepare (sampleRate);
        multiband.setCrossoverFrequencies ({ 200.0f, 1200.0f, 6000.0f });

        for (size_t band = 0; band < 4; ++band)
        {
            Compressor::Settings settings;
            settings.enabled = false;
            multiband.setBand (band, settings);
        }

        double worst = 0.0;

        for (double f = 40.0; f < 16000.0; f *= 1.3)
        {
            constexpr int blockSize = 64;
            std::vector<float> left (blockSize), right (blockSize);
            float* channels[2] = { left.data(), right.data() };

            multiband.reset();

            double inSq = 0.0, outSq = 0.0;
            int phase = 0;

            for (int n = 0; n < 72000; n += blockSize)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const auto x = 0.3f * (float) std::sin (2.0 * pi * f * phase / sampleRate);
                    left[(size_t) i] = x;
                    right[(size_t) i] = x;

                    if (n >= 24000)
                        inSq += (double) x * x;
                }

                multiband.process (channels, 2, blockSize);

                if (n >= 24000)
                    for (int i = 0; i < blockSize; ++i)
                        outSq += (double) left[(size_t) i] * left[(size_t) i];
            }

            const auto deviation = 10.0 * std::log10 (outSq / inSq);
            worst = std::max (worst, std::abs (deviation));

            if (std::abs (deviation) > 0.1)
                std::printf ("        %7.0f Hz  %+7.4f dB\n", f, deviation);
        }

        checkClose (worst, 0.0, 0.1, "worst deviation across the spectrum");

        /*  Energy is blind to polarity, so everything above would also pass if
            the bands were summed with the wrong sign and the whole output came
            out inverted.

            The correlation will NOT be 1.0: an LR crossover sums to an
            all-pass, so even well below the lowest crossover there is real
            phase shift — around 39 degrees at 40 Hz against a 200 Hz
            crossover, giving cos(39) = 0.78. That is correct behaviour and the
            documented trade for IIR crossovers.

            What must hold is the SIGN. An inverted output would read about
            -0.78, so anything comfortably positive proves polarity is intact.
        */
        {
            constexpr int blockSize = 64;
            std::vector<float> left (blockSize), right (blockSize);
            float* channels[2] = { left.data(), right.data() };

            multiband.reset();

            double correlation = 0.0, inputEnergy = 0.0;
            int phase = 0;

            for (int n = 0; n < 96000; n += blockSize)
            {
                std::vector<float> original ((size_t) blockSize);

                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const auto x = 0.3f * (float) std::sin (2.0 * pi * 40.0 * phase / sampleRate);
                    left[(size_t) i] = x;
                    right[(size_t) i] = x;
                    original[(size_t) i] = x;
                }

                multiband.process (channels, 2, blockSize);

                if (n >= 48000)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        correlation += (double) original[(size_t) i] * left[(size_t) i];
                        inputEnergy += (double) original[(size_t) i] * original[(size_t) i];
                    }
            }

            const auto normalised = correlation / inputEnergy;
            std::printf ("        40 Hz input/output correlation: %+.3f "
                         "(< 1 from all-pass phase shift, must be positive)\n", normalised);

            check (normalised > 0.5, "output polarity matches the input");
        }
    }

    std::printf ("\n== 6. Compression stays in its own band ==\n");
    {
        // Squash the low band hard and check the high band is untouched. If the
        // detectors were fed the full-range signal instead of their own band,
        // a loud bass note would duck the cymbals.
        auto measureAt = [] (double toneHz, bool compressLowBand)
        {
            MultibandCompressor<3> multiband;
            multiband.prepare (sampleRate);
            multiband.setCrossoverFrequencies ({ 300.0f, 3000.0f });

            for (size_t band = 0; band < 3; ++band)
            {
                Compressor::Settings settings;
                settings.enabled = (band == 0) && compressLowBand;
                settings.thresholdDb = -40.0f;
                settings.ratio = 10.0f;
                settings.kneeDb = 0.0f;
                settings.attackMs = 5.0f;
                settings.releaseMs = 100.0f;
                multiband.setBand (band, settings);
            }

            constexpr int blockSize = 64;
            std::vector<float> left (blockSize), right (blockSize);
            float* channels[2] = { left.data(), right.data() };

            double inSq = 0.0, outSq = 0.0;
            int phase = 0;

            for (int n = 0; n < 96000; n += blockSize)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const auto x = 0.5f * (float) std::sin (2.0 * pi * toneHz * phase / sampleRate);
                    left[(size_t) i] = x;
                    right[(size_t) i] = x;

                    if (n >= 48000) inSq += (double) x * x;
                }

                multiband.process (channels, 2, blockSize);

                if (n >= 48000)
                    for (int i = 0; i < blockSize; ++i)
                        outSq += (double) left[(size_t) i] * left[(size_t) i];
            }

            return 10.0 * std::log10 (outSq / inSq);
        };

        const auto lowCompressed  = measureAt (100.0,  true);
        const auto lowUntouched   = measureAt (100.0,  false);
        const auto highCompressed = measureAt (8000.0, true);
        const auto highUntouched  = measureAt (8000.0, false);

        check (lowCompressed < lowUntouched - 8.0,
               "100 Hz is squashed when the low band compresses");
        checkClose (highCompressed, highUntouched, 0.05,
                    "8 kHz is untouched by the low band's compressor");
    }

    std::printf ("\n== 6b. A band\'s detector hears only its own band ==\n");
    {
        /*  Single tones cannot test this: with one tone in the signal, the
            band's content and the full-range content are the same, so feeding
            the detector the wrong one changes nothing.

            Two tones can. A loud bass note plus a quiet high one, with only the
            HIGH band compressing and its threshold set above the high tone. If
            its detector hears the full signal, the bass triggers it and ducks
            the high tone — the classic "kick drum pumps the cymbals" fault.
        */
        constexpr double lowHz = 80.0, highHz = 8000.0;
        constexpr float lowAmp = 0.5f, highAmp = 0.05f;
        constexpr int blockSize = 64;
        constexpr int totalSamples = 96000;

        MultibandCompressor<3> multiband;
        multiband.prepare (sampleRate);
        multiband.setCrossoverFrequencies ({ 300.0f, 3000.0f });

        for (size_t band = 0; band < 3; ++band)
        {
            Compressor::Settings settings;
            settings.enabled     = (band == 2);
            settings.thresholdDb = -18.0f;   // the 8 kHz tone alone sits well below this
            settings.ratio       = 10.0f;
            settings.kneeDb      = 0.0f;
            settings.attackMs    = 5.0f;
            settings.releaseMs   = 100.0f;
            multiband.setBand (band, settings);
        }

        std::vector<float> input, output;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        for (int n = 0; n < totalSamples; n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = (double) (n + i);
                const auto x = lowAmp  * (float) std::sin (2.0 * pi * lowHz  * t / sampleRate)
                             + highAmp * (float) std::sin (2.0 * pi * highHz * t / sampleRate);
                left[(size_t) i] = x;
                right[(size_t) i] = x;
                input.push_back (x);
            }

            multiband.process (channels, 2, blockSize);

            for (int i = 0; i < blockSize; ++i)
                output.push_back (left[(size_t) i]);
        }

        const auto inHigh  = toneAmplitude (input,  highHz, 48000);
        const auto outHigh = toneAmplitude (output, highHz, 48000);
        const auto changeDb = 20.0 * std::log10 (outHigh / inHigh);

        std::printf ("        8 kHz component: in %.5f  out %.5f  (%+.3f dB)\n",
                     inHigh, outHigh, changeDb);

        checkClose (changeDb, 0.0, 0.3, "quiet 8 kHz tone survives a loud 80 Hz one");
        checkClose (multiband.getBandReductionDb (2), 0.0, 0.5,
                    "high band reports no reduction");
    }

    std::printf ("\n== 7. Solo and mute ==\n");
    {
        auto energyAt = [] (double toneHz, int soloBand, int muteBand)
        {
            MultibandCompressor<3> multiband;
            multiband.prepare (sampleRate);
            multiband.setCrossoverFrequencies ({ 300.0f, 3000.0f });

            for (size_t band = 0; band < 3; ++band)
            {
                Compressor::Settings settings;
                settings.enabled = false;
                multiband.setBand (band, settings);
            }

            if (soloBand >= 0) multiband.setBandSoloed ((size_t) soloBand, true);
            if (muteBand >= 0) multiband.setBandMuted  ((size_t) muteBand, true);

            constexpr int blockSize = 64;
            std::vector<float> left (blockSize), right (blockSize);
            float* channels[2] = { left.data(), right.data() };

            double outSq = 0.0;
            int phase = 0;

            for (int n = 0; n < 72000; n += blockSize)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const auto x = 0.5f * (float) std::sin (2.0 * pi * toneHz * phase / sampleRate);
                    left[(size_t) i] = x;
                    right[(size_t) i] = x;
                }

                multiband.process (channels, 2, blockSize);

                if (n >= 24000)
                    for (int i = 0; i < blockSize; ++i)
                        outSq += (double) left[(size_t) i] * left[(size_t) i];
            }

            return outSq;
        };

        const auto reference = energyAt (100.0, -1, -1);

        check (energyAt (100.0, 0, -1) > reference * 0.9,
               "soloing the low band keeps a 100 Hz tone");
        check (energyAt (100.0, 2, -1) < reference * 0.001,
               "soloing the high band silences a 100 Hz tone");
        check (energyAt (100.0, -1, 0) < reference * 0.001,
               "muting the low band silences a 100 Hz tone");
        check (energyAt (100.0, -1, 2) > reference * 0.9,
               "muting the high band leaves a 100 Hz tone alone");
    }

    std::printf ("\n== 8. reset() clears the detectors ==\n");
    {
        MultibandCompressor<3> multiband;
        multiband.prepare (sampleRate);
        multiband.setCrossoverFrequencies ({ 300.0f, 3000.0f });

        Compressor::Settings settings;
        settings.enabled = true;
        settings.thresholdDb = -40.0f;
        settings.ratio = 8.0f;
        multiband.setBand (0, settings);

        constexpr int blockSize = 64;
        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        for (int n = 0; n < 48000; n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                left[(size_t) i] = 0.5f;
                right[(size_t) i] = 0.5f;
            }
            multiband.process (channels, 2, blockSize);
        }

        check (multiband.getBandReductionDb (0) < -5.0f, "clamped before reset");

        multiband.reset();
        checkClose (multiband.getBandReductionDb (0), 0.0, 1.0e-9, "cleared by reset");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

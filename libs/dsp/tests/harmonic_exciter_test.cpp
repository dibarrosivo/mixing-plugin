/*
    Tests for HarmonicExciter.

    This one links JUCE, unlike the rest of libs/dsp/tests — juce::dsp supplies
    the oversampler. Run it through CTest:

        ctest --test-dir build -R harmonic_exciter --output-on-failure

    Section 4 is the reason the module exists in this form. Saturation makes
    harmonics above Nyquist, those fold back as inharmonic content, and that
    folded content is the "cheap and digital" sound an exciter is supposed to
    avoid. The test measures the folded component directly and compares
    oversampled against not, so the benefit is a number rather than a belief.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/HarmonicExciter.h>

using namespace dsp;

namespace
{
    int failures = 0;

    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr int    blockSize = 512;

    void check (bool ok, const std::string& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
        if (! ok) ++failures;
    }

    void checkClose (double actual, double expected, double tolerance, const std::string& what)
    {
        const auto ok = std::abs (actual - expected) <= tolerance;
        std::printf ("  [%s] %-46s expected %+9.5f   got %+9.5f\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    // Run a sine through an exciter and return the output, discarding a warmup
    // so filter and oversampler transients are not measured.
    std::vector<float> runSine (HarmonicExciter& exciter, double frequencyHz,
                                float amplitude, int numBlocks = 40)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<float> output;
        int phase = 0;

        for (int block = 0; block < numBlocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const auto x = amplitude * (float) std::sin (2.0 * pi * frequencyHz * phase / sampleRate);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            juce::dsp::AudioBlock<float> b (buffer);
            exciter.process (b);

            if (block >= numBlocks / 2)
                for (int i = 0; i < blockSize; ++i)
                    output.push_back (buffer.getSample (0, i));
        }

        return output;
    }

    /*  Amplitude of one frequency component by quadrature detection.

        The window is truncated to a whole multiple of 480 samples — 10 ms at
        48 kHz — which is an integer number of cycles for any frequency that is
        a multiple of 100 Hz. Every test tone here is.

        This matters more than it looks. With an arbitrary window length, a
        strong fundamental leaks across the whole spectrum, and a measurement
        taken 1 kHz away reads that leakage rather than what is actually there.
        It made an aliasing test report the same value with oversampling on and
        off, because both readings were leakage from the 7 kHz tone.
    */
    double toneAmplitude (const std::vector<float>& samples, double frequencyHz)
    {
        constexpr int cycleAlignment = 480;

        double real = 0.0, imag = 0.0;
        const auto count = ((int) samples.size() / cycleAlignment) * cycleAlignment;

        if (count == 0)
            return 0.0;

        for (int n = 0; n < count; ++n)
        {
            const auto phase = 2.0 * pi * frequencyHz * n / sampleRate;
            real += samples[(size_t) n] * std::cos (phase);
            imag += samples[(size_t) n] * std::sin (phase);
        }

        return 2.0 * std::sqrt (real * real + imag * imag) / count;
    }

    HarmonicExciter::Settings excite (float focusHz, float drive, float mixPercent)
    {
        HarmonicExciter::Settings s;
        s.enabled    = true;
        s.focusHz    = focusHz;
        s.drive      = drive;
        s.mixPercent = mixPercent;
        s.type       = SaturationType::tape;   // symmetric: a clean harmonic series
        s.listen     = false;
        return s;
    }

    juce::dsp::ProcessSpec spec()
    {
        return { sampleRate, (juce::uint32) blockSize, 2 };
    }
}

int main()
{
    std::printf ("\n== 1. Mix at zero returns the input exactly ==\n");
    {
        // An exciter adds. At zero it must add nothing — including no phase
        // shift, which is why the dry path is delayed rather than left alone.
        HarmonicExciter exciter;
        exciter.prepare (spec());
        exciter.setSettings (excite (3000.0f, 8.0f, 0.0f));

        const auto output = runSine (exciter, 1000.0, 0.5f);
        const auto level  = toneAmplitude (output, 1000.0);

        checkClose (level, 0.5, 0.005, "1 kHz passes at unity");

        // And nothing was generated.
        const auto third = toneAmplitude (output, 3000.0);
        check (third < 1.0e-4, "no harmonics added at mix 0");
    }

    std::printf ("\n== 2. Disabled is a wire ==\n");
    {
        HarmonicExciter exciter;
        exciter.prepare (spec());

        auto settings = excite (3000.0f, 8.0f, 100.0f);
        settings.enabled = false;
        exciter.setSettings (settings);

        juce::AudioBuffer<float> buffer (2, 8);
        for (int i = 0; i < 8; ++i)
        {
            buffer.setSample (0, i, 0.1f * (float) i);
            buffer.setSample (1, i, 0.1f * (float) i);
        }

        juce::dsp::AudioBlock<float> b (buffer);
        exciter.process (b);

        auto identical = true;
        for (int i = 0; i < 8; ++i)
            if (! juce::approximatelyEqual (buffer.getSample (0, i), 0.1f * (float) i))
                identical = false;

        check (identical, "samples pass through untouched");
    }

    std::printf ("\n== 3. Content below the focus frequency is left alone ==\n");
    {
        // That is what separates an exciter from a distortion box: the low end
        // never reaches the saturator, so it does not get thickened.
        HarmonicExciter exciter;
        exciter.prepare (spec());
        exciter.setSettings (excite (5000.0f, 12.0f, 100.0f));

        const auto output = runSine (exciter, 200.0, 0.5f);

        const auto fundamental = toneAmplitude (output, 200.0);
        const auto third       = toneAmplitude (output, 600.0);

        checkClose (fundamental, 0.5, 0.01, "200 Hz passes at unity");
        check (third < fundamental * 0.005, "almost no harmonics generated from it");
    }

    std::printf ("\n== 4. Oversampling removes the aliasing ==\n");
    {
        /*  A 7 kHz tone at 48 kHz, shaped by `tape`.

            Which harmonics exist is decided by symmetry, and tape is symmetric,
            so only ODD ones are produced — 3rd, 5th, 7th. See the waveshaper
            tests, where the even harmonics of a symmetric curve measure exactly
            zero. Folding the odd series against a 24 kHz Nyquist:

                3rd = 21 kHz   below Nyquist, legitimate
                5th = 35 kHz   folds to 48 - 35 = 13 kHz
                7th = 49 kHz   folds to 49 - 48 =  1 kHz

            An earlier version of this test looked for a product at 6 kHz, which
            is where the SIXTH harmonic would fold — and a symmetric curve has
            no sixth. It measured zero with oversampling on and off and proved
            nothing.

            1 kHz is the clearest evidence: nothing in the input is near it, no
            legitimate harmonic lands there, and it is far from the fundamental,
            so it cannot be leakage either.
        */
        constexpr double fundamental = 7000.0;

        auto measure = [] (int factor, double atHz)
        {
            HarmonicExciter exciter;
            exciter.prepare (spec(), factor);
            exciter.setSettings (excite (3000.0f, 20.0f, 100.0f));

            return toneAmplitude (runSine (exciter, fundamental, 0.6f), atHz);
        };

        const auto without13k = measure (1, 13000.0);
        const auto with13k    = measure (4, 13000.0);
        const auto without1k  = measure (1, 1000.0);
        const auto with1k     = measure (4, 1000.0);

        const auto improvement13k = 20.0 * std::log10 (without13k / std::max (with13k, 1.0e-12));
        const auto improvement1k  = 20.0 * std::log10 (without1k  / std::max (with1k,  1.0e-12));

        std::printf ("        5th folded to 13 kHz:  1x %.6f   4x %.6f   (%.1f dB better)\n",
                     without13k, with13k, improvement13k);
        std::printf ("        7th folded to  1 kHz:  1x %.6f   4x %.6f   (%.1f dB better)\n",
                     without1k, with1k, improvement1k);

        check (without13k > 1.0e-3, "without oversampling the 5th folds back audibly");
        check (improvement13k > 20.0, "4x suppresses it by over 20 dB");
        check (improvement1k  > 20.0, "and suppresses the 7th's fold too");
    }

    std::printf ("\n== 5. Harmonics are actually generated ==\n");
    {
        // The flip side of section 4: suppressing aliasing is worthless if the
        // wanted harmonics went with it.
        HarmonicExciter exciter;
        exciter.prepare (spec());
        exciter.setSettings (excite (3000.0f, 12.0f, 100.0f));

        const auto output = runSine (exciter, 4000.0, 0.6f);

        const auto fundamental = toneAmplitude (output, 4000.0);
        const auto third       = toneAmplitude (output, 12000.0);

        std::printf ("        4 kHz in:  f0 %.4f   3rd %.5f\n", fundamental, third);

        check (third > fundamental * 0.01, "a real third harmonic is produced");
    }

    std::printf ("\n== 6. Latency is reported and compensated ==\n");
    {
        HarmonicExciter oversampled, plain;
        oversampled.prepare (spec(), 4);
        plain.prepare (spec(), 1);

        std::printf ("        latency: 4x = %d samples, 1x = %d samples\n",
                     oversampled.getLatencySamples(), plain.getLatencySamples());

        check (oversampled.getLatencySamples() > 0, "oversampling reports non-zero latency");
        checkClose (plain.getLatencySamples(), 0.0, 0.5, "no oversampling, no latency");

        /*  Verified with an impulse rather than by looking for comb ripple.

            A comb only appears where the wet path contributes, and there it is
            indistinguishable from the exciter doing its job — an earlier
            version of this test read the exciter legitimately adding content at
            1.5 kHz as a 1.4 dB comb and failed.

            With mix at 0 the output is purely the delayed dry path, so the
            position of the impulse IS the compensation delay. That is exact.
        */
        HarmonicExciter exciter;
        exciter.prepare (spec(), 4);
        exciter.setSettings (excite (3000.0f, 10.0f, 0.0f));

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);

        juce::dsp::AudioBlock<float> b (buffer);
        exciter.process (b);

        auto peakIndex = 0;
        auto peakValue = 0.0f;

        for (int i = 0; i < blockSize; ++i)
        {
            const auto magnitude = std::abs (buffer.getSample (0, i));

            if (magnitude > peakValue)
            {
                peakValue = magnitude;
                peakIndex = i;
            }
        }

        std::printf ("        impulse emerges at sample %d, reported latency %d\n",
                     peakIndex, exciter.getLatencySamples());

        check (std::abs (peakIndex - exciter.getLatencySamples()) <= 1,
               "dry path is delayed by exactly the reported latency");
        check (peakValue > 0.9f, "and arrives at full amplitude");
    }

    std::printf ("\n== 7. Listen mode isolates the generated content ==\n");
    {
        HarmonicExciter exciter;
        exciter.prepare (spec());

        auto settings = excite (3000.0f, 12.0f, 100.0f);
        settings.listen = true;
        exciter.setSettings (settings);

        const auto output = runSine (exciter, 300.0, 0.5f);
        const auto fundamental = toneAmplitude (output, 300.0);

        check (fundamental < 0.005, "a 300 Hz tone is absent when listening to harmonics");
    }

    std::printf ("\n== 8. Drive and mix both do something ==\n");
    {
        auto thirdHarmonic = [] (float drive, float mixPercent)
        {
            HarmonicExciter exciter;
            exciter.prepare (spec());
            exciter.setSettings (excite (3000.0f, drive, mixPercent));

            return toneAmplitude (runSine (exciter, 4000.0, 0.6f), 12000.0);
        };

        const auto gentle = thirdHarmonic (2.0f,  100.0f);
        const auto hard   = thirdHarmonic (16.0f, 100.0f);
        const auto quiet  = thirdHarmonic (16.0f, 25.0f);

        check (hard > gentle * 2.0, "more drive means more harmonics");
        checkClose (quiet / hard, 0.25, 0.02, "mix scales the added harmonics linearly");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

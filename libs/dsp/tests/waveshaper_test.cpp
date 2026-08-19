/*
    Tests for Waveshaper.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/ws_test \
            libs/dsp/tests/waveshaper_test.cpp && /tmp/ws_test

    A saturator cannot be judged by looking at it. Every curve here is a
    plausible-looking S shape, and the thing that separates them — which
    harmonics they generate — is invisible in the source and inaudible in
    isolation. So the tests measure the harmonic series directly.

    Section 3 is the load-bearing one: a symmetric curve must produce NO even
    harmonics, and an asymmetric one must produce them. That is the entire
    difference between "tube warmth" and "grit", and it follows from symmetry
    rather than from anything subjective.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/Waveshaper.h>

using namespace dsp;

namespace
{
    int failures = 0;

    constexpr double sampleRate = 96000.0;   // high, so harmonics stay below Nyquist
    constexpr double pi = 3.14159265358979323846;
    constexpr double fundamental = 1000.0;

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

    // Shape a sine and return the amplitude of each harmonic, 1-based:
    // harmonics[0] is the fundamental, [1] the second, and so on.
    std::vector<double> harmonicsOf (SaturationType type, float drive,
                                     float amplitude, int howMany = 6)
    {
        Waveshaper shaper;
        shaper.setType (type);
        shaper.setDrive (drive);

        // A whole number of cycles, so the quadrature detection below has no
        // leakage to contend with.
        constexpr int cycles = 200;
        const auto samplesPerCycle = sampleRate / fundamental;
        const auto count = (int) (cycles * samplesPerCycle);

        std::vector<float> output ((size_t) count);

        for (int n = 0; n < count; ++n)
        {
            const auto x = amplitude * (float) std::sin (2.0 * pi * fundamental * n / sampleRate);
            output[(size_t) n] = shaper.processSample (x);
        }

        std::vector<double> result;

        for (int h = 1; h <= howMany; ++h)
        {
            double real = 0.0, imag = 0.0;

            for (int n = 0; n < count; ++n)
            {
                const auto phase = 2.0 * pi * fundamental * h * n / sampleRate;
                real += output[(size_t) n] * std::cos (phase);
                imag += output[(size_t) n] * std::sin (phase);
            }

            result.push_back (2.0 * std::sqrt (real * real + imag * imag) / count);
        }

        return result;
    }
}

int main()
{
    std::printf ("\n== 1. Every curve passes through the origin ==\n");
    {
        // A curve that does not is a DC offset generator: silence comes out at
        // some non-zero level, which thumps on bypass and sums badly.
        for (auto type : { SaturationType::tube, SaturationType::tape,
                           SaturationType::transistor })
        {
            for (float drive : { 1.0f, 5.0f, 25.0f })
            {
                Waveshaper shaper;
                shaper.setType (type);
                shaper.setDrive (drive);

                const auto name = type == SaturationType::tube ? "tube"
                                : type == SaturationType::tape ? "tape" : "transistor";

                char label[64];
                std::snprintf (label, sizeof (label), "%s at drive %.0f", name, (double) drive);
                checkClose (shaper.processSample (0.0f), 0.0, 1.0e-6, label);
            }
        }
    }

    std::printf ("\n== 2. Full scale in stays near full scale out ==\n");
    {
        /*  Measured as the larger of the two extremes, not as f(+1).

            An asymmetric curve deliberately does not treat the two halves
            alike, so its positive peak is legitimately below full scale. What
            must hold is that the loudest excursion lands near 1.0, which is
            what keeps drive from doubling as a volume control.
        */
        for (auto type : { SaturationType::tube, SaturationType::tape,
                           SaturationType::transistor })
        {
            for (float drive : { 1.5f, 8.0f, 30.0f })
            {
                Waveshaper shaper;
                shaper.setType (type);
                shaper.setDrive (drive);

                const auto name = type == SaturationType::tube ? "tube"
                                : type == SaturationType::tape ? "tape" : "transistor";

                const auto peak = std::max (std::abs (shaper.processSample ( 1.0f)),
                                            std::abs (shaper.processSample (-1.0f)));

                char label[64];
                std::snprintf (label, sizeof (label), "%s at drive %.0f", name, (double) drive);
                checkClose (peak, 1.0, 0.02, label);
            }
        }
    }

    std::printf ("\n== 3. Symmetry decides which harmonics appear ==\n");
    {
        constexpr float drive = 6.0f;
        constexpr float amplitude = 0.7f;

        const auto tube       = harmonicsOf (SaturationType::tube,       drive, amplitude);
        const auto tape       = harmonicsOf (SaturationType::tape,       drive, amplitude);
        const auto transistor = harmonicsOf (SaturationType::transistor, drive, amplitude);

        auto report = [] (const char* name, const std::vector<double>& h)
        {
            std::printf ("        %-11s f0 %.4f   2nd %.5f   3rd %.5f   4th %.5f   5th %.5f\n",
                         name, h[0], h[1], h[2], h[3], h[4]);
        };

        report ("tube",       tube);
        report ("tape",       tape);
        report ("transistor", transistor);

        // Symmetric curves: even harmonics must be absent, not merely small.
        check (tape[1] < tape[0] * 1.0e-4,       "tape produces no 2nd harmonic");
        check (tape[3] < tape[0] * 1.0e-4,       "tape produces no 4th harmonic");
        check (transistor[1] < transistor[0] * 1.0e-4, "transistor produces no 2nd harmonic");
        check (transistor[3] < transistor[0] * 1.0e-4, "transistor produces no 4th harmonic");

        // ...but they must produce odd ones, or they are not saturating at all.
        check (tape[2] > tape[0] * 0.02,             "tape produces a 3rd harmonic");
        check (transistor[2] > transistor[0] * 0.02, "transistor produces a 3rd harmonic");

        // The asymmetric curve must produce even harmonics. This is the
        // difference the client is buying.
        check (tube[1] > tube[0] * 0.02, "tube produces a substantial 2nd harmonic");
        check (tube[1] > tape[1] * 100.0, "tube's 2nd harmonic dwarfs tape's");
    }

    std::printf ("\n== 4. Transistor is harder than tape ==\n");
    {
        // Both are symmetric, so they differ in how much high-order content
        // they make. A cubic clipper reaches its limit at a definite point;
        // tanh only approaches one.
        constexpr float drive = 6.0f;
        constexpr float amplitude = 0.7f;

        const auto tape       = harmonicsOf (SaturationType::tape,       drive, amplitude, 8);
        const auto transistor = harmonicsOf (SaturationType::transistor, drive, amplitude, 8);

        const auto tapeHigh       = (tape[4] + tape[6]) / tape[0];
        const auto transistorHigh = (transistor[4] + transistor[6]) / transistor[0];

        std::printf ("        5th+7th relative to f0:  tape %.5f   transistor %.5f\n",
                     tapeHigh, transistorHigh);

        check (transistorHigh > tapeHigh,
               "transistor generates more high-order odd content than tape");
    }

    std::printf ("\n== 4b. The knee is soft, not a corner ==\n");
    {
        /*  Removing the cubic term turns `transistor` into a hard clipper:
            perfectly linear, then a corner. It still saturates and still makes
            odd harmonics, so every other section passes — mutation testing
            found exactly that.

            What separates a soft clipper is that it is ALREADY bending well
            before it reaches its limit. Measured here by extrapolating the
            small-signal slope and checking the real curve falls short of it.
        */
        constexpr float drive = 4.0f;

        for (auto type : { SaturationType::tube, SaturationType::tape,
                           SaturationType::transistor })
        {
            Waveshaper shaper;
            shaper.setType (type);
            shaper.setDrive (drive);

            constexpr float tiny = 1.0e-4f;
            const auto slope = shaper.processSample (tiny) / tiny;

            // Well inside the limit: 70% of the way to where a hard clipper
            // would corner.
            const auto x = 0.7f / drive;
            const auto extrapolated = slope * x;
            const auto actual = shaper.processSample (x);
            const auto ratio = actual / extrapolated;

            const auto name = type == SaturationType::tube ? "tube"
                            : type == SaturationType::tape ? "tape" : "transistor";

            char label[96];
            std::snprintf (label, sizeof (label),
                           "%s is already bending at 70%% of limit (ratio %.4f)", name, ratio);

            check (ratio < 0.95, label);
        }
    }

    std::printf ("\n== 5. More drive means more harmonics ==\n");
    {
        // If this does not hold the control does nothing audible.
        for (auto type : { SaturationType::tube, SaturationType::tape,
                           SaturationType::transistor })
        {
            const auto gentle = harmonicsOf (type, 1.5f, 0.7f);
            const auto hard   = harmonicsOf (type, 12.0f, 0.7f);

            const auto gentleThird = gentle[2] / gentle[0];
            const auto hardThird   = hard[2] / hard[0];

            const auto name = type == SaturationType::tube ? "tube"
                            : type == SaturationType::tape ? "tape" : "transistor";

            char label[80];
            std::snprintf (label, sizeof (label),
                           "%s: 3rd grows with drive (%.5f -> %.5f)",
                           name, gentleThird, hardThird);

            check (hardThird > gentleThird * 2.0, label);
        }
    }

    std::printf ("\n== 6. Quiet signals are left nearly alone ==\n");
    {
        // A saturator that mangles -40 dBFS is unusable on anything dynamic.
        for (auto type : { SaturationType::tube, SaturationType::tape,
                           SaturationType::transistor })
        {
            const auto h = harmonicsOf (type, 3.0f, 0.01f);
            const auto distortion = (h[1] + h[2]) / h[0];

            const auto name = type == SaturationType::tube ? "tube"
                            : type == SaturationType::tape ? "tape" : "transistor";

            char label[80];
            std::snprintf (label, sizeof (label), "%s at -40 dBFS: distortion %.5f", name,
                           distortion);

            check (distortion < 0.05, label);
        }
    }

    std::printf ("\n== 7. Output stays bounded and finite ==\n");
    {
        for (auto type : { SaturationType::tube, SaturationType::tape,
                           SaturationType::transistor })
        {
            Waveshaper shaper;
            shaper.setType (type);
            shaper.setDrive (50.0f);

            auto ok = true;

            for (float x = -4.0f; x <= 4.0f; x += 0.001f)
            {
                const auto y = shaper.processSample (x);

                // Every curve is normalised, so nothing should exceed unity by
                // more than rounding. 1.5 was loose enough to hide a curve that
                // had lost its normalisation entirely.
                if (! std::isfinite (y) || std::abs (y) > 1.01f)
                    ok = false;
            }

            const auto name = type == SaturationType::tube ? "tube"
                            : type == SaturationType::tape ? "tape" : "transistor";

            check (ok, std::string (name) + " stays finite and bounded well past full scale");
        }
    }

    std::printf ("\n== 8. Drive is clamped to a usable range ==\n");
    {
        Waveshaper shaper;

        shaper.setDrive (-5.0f);
        checkClose (shaper.getDrive(), 1.0, 1.0e-6, "negative drive clamps to 1");

        shaper.setDrive (1000.0f);
        checkClose (shaper.getDrive(), 50.0, 1.0e-6, "excessive drive clamps to 50");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

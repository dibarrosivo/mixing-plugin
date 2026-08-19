/*
    Tests for EnvelopeFollower.

        g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/env_test \
            libs/dsp/tests/envelope_follower_test.cpp && /tmp/env_test

    The interesting property is that the time constants mean what they claim.
    A detector whose "10 ms attack" is really 23 ms still sounds like a
    compressor — it just never matches the reference plugin, and you burn days
    tuning around it. So the timing is pinned numerically rather than by ear.
*/

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <dsp/EnvelopeFollower.h>

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
        std::printf ("  [%s] %-50s expected %8.5f   got %8.5f\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }

    EnvelopeFollower make (float attackMs, float releaseMs,
                           EnvelopeFollower::Mode mode = EnvelopeFollower::Mode::peak)
    {
        EnvelopeFollower e;
        e.setMode (mode);
        e.setAttackMs (attackMs);
        e.setReleaseMs (releaseMs);
        e.prepare (sampleRate);
        return e;
    }

    int samplesFor (double milliseconds)
    {
        return static_cast<int> (milliseconds * 0.001 * sampleRate);
    }
}

int main()
{
    std::printf ("\n== 1. One time constant reaches 63.2%% of a step ==\n");
    {
        // The defining property of a one-pole. If this is wrong, every
        // attack/release number on the front panel is a lie.
        for (double tauMs : { 1.0, 10.0, 50.0 })
        {
            auto env = make ((float) tauMs, 1000.0f);
            float y = 0.0f;

            for (int n = 0; n < samplesFor (tauMs); ++n)
                y = env.processSample (1.0f);

            checkClose (y, 1.0 - std::exp (-1.0), 0.001,
                        "attack tau=" + std::to_string ((int) tauMs) + "ms -> 1-1/e");
        }
    }

    std::printf ("\n== 2. Release uses the release constant, not the attack one ==\n");
    {
        // Asymmetry is the whole point of a detector. A single shared
        // coefficient is a classic copy-paste bug and sounds almost right.
        auto env = make (1.0f, 100.0f);

        for (int n = 0; n < samplesFor (50.0); ++n)   // settle at 1.0
            env.processSample (1.0f);

        float y = 0.0f;
        for (int n = 0; n < samplesFor (100.0); ++n)  // one release tau of silence
            y = env.processSample (0.0f);

        checkClose (y, std::exp (-1.0), 0.002, "after one release tau -> 1/e");
    }

    std::printf ("\n== 3. Attack and release are genuinely independent ==\n");
    {
        auto fast = make (1.0f,   500.0f);
        auto slow = make (100.0f, 500.0f);

        float yFast = 0.0f, ySlow = 0.0f;
        for (int n = 0; n < samplesFor (10.0); ++n)
        {
            yFast = fast.processSample (1.0f);
            ySlow = slow.processSample (1.0f);
        }

        check (yFast > 0.99f, "1ms attack is nearly settled after 10ms");
        check (ySlow < 0.15f, "100ms attack is barely moving after 10ms");
        check (yFast > ySlow * 5.0f, "the two differ by a wide margin");
    }

    std::printf ("\n== 4. RMS mode reports true RMS, not peak ==\n");
    {
        // A full-scale sine has peak 1.0 but RMS 0.7071. Confusing the two is
        // a 3 dB error in every threshold the client sets.
        auto env = make (200.0f, 200.0f, EnvelopeFollower::Mode::rms);

        constexpr double freq = 1000.0;
        double sum = 0.0;
        int count = 0;

        for (int n = 0; n < samplesFor (3000.0); ++n)
        {
            const auto x = std::sin (2.0 * 3.14159265358979323846 * freq * n / sampleRate);
            const auto y = env.processSample ((float) x);

            if (n > samplesFor (2000.0))   // measure once settled
            {
                sum += y;
                ++count;
            }
        }

        checkClose (sum / count, 1.0 / std::sqrt (2.0), 0.005, "sine amplitude 1.0 -> RMS");
    }

    std::printf ("\n== 5. Peak mode reports peak, not RMS ==\n");
    {
        auto env = make (0.1f, 5000.0f);   // fast attack, very slow release

        constexpr double freq = 1000.0;
        float y = 0.0f;
        for (int n = 0; n < samplesFor (500.0); ++n)
        {
            const auto x = std::sin (2.0 * 3.14159265358979323846 * freq * n / sampleRate);
            y = env.processSample ((float) x);
        }

        checkClose (y, 1.0, 0.02, "sine amplitude 1.0 -> peak");
    }

    std::printf ("\n== 6. Rectification: sign is discarded ==\n");
    {
        auto positive = make (5.0f, 50.0f);
        auto negative = make (5.0f, 50.0f);

        bool identical = true;
        for (int n = 0; n < samplesFor (100.0); ++n)
            if (positive.processSample (0.7f) != negative.processSample (-0.7f))
                identical = false;

        check (identical, "+0.7 and -0.7 produce identical envelopes");
    }

    std::printf ("\n== 7. Envelope is never negative and never NaN ==\n");
    {
        auto env = make (2.0f, 20.0f);
        bool valid = true;

        for (int n = 0; n < 480000; ++n)
        {
            // Alternating hostile input: full scale, silence, sign flips.
            const auto x = (n % 3 == 0) ? 1.0f : (n % 3 == 1 ? -1.0f : 0.0f);
            const auto y = env.processSample (x);

            if (y < 0.0f || ! std::isfinite (y))
                valid = false;
        }

        check (valid, "10 s of hostile input stays finite and non-negative");
    }

    std::printf ("\n== 8. Zero attack/release means instant ==\n");
    {
        auto env = make (0.0f, 0.0f);

        const auto a = env.processSample (0.5f);
        const auto b = env.processSample (0.0f);

        checkClose (a, 0.5, 1.0e-6, "instant attack tracks immediately");
        checkClose (b, 0.0, 1.0e-6, "instant release drops immediately");
    }

    std::printf ("\n== 9. reset() clears state ==\n");
    {
        auto fresh  = make (10.0f, 100.0f);
        auto reused = make (10.0f, 100.0f);

        for (int n = 0; n < samplesFor (200.0); ++n)
            reused.processSample (1.0f);

        reused.reset();

        bool identical = true;
        for (int n = 0; n < samplesFor (100.0); ++n)
            if (fresh.processSample (0.3f) != reused.processSample (0.3f))
                identical = false;

        check (identical, "after reset, response matches a fresh detector");
    }

    std::printf ("\n== 10. Sample rate independence ==\n");
    {
        // The same attack time must behave the same at 44.1k and 96k, or every
        // preset the client makes breaks when they change session rate.
        auto measureAt = [] (double fs)
        {
            EnvelopeFollower e;
            e.setAttackMs (10.0f);
            e.setReleaseMs (100.0f);
            e.prepare (fs);

            float y = 0.0f;
            for (int n = 0; n < (int) (0.010 * fs); ++n)
                y = e.processSample (1.0f);
            return y;
        };

        const auto at44 = measureAt (44100.0);
        const auto at96 = measureAt (96000.0);

        checkClose (at44, 1.0 - std::exp (-1.0), 0.002, "10ms attack at 44.1 kHz");
        checkClose (at96, 1.0 - std::exp (-1.0), 0.002, "10ms attack at 96 kHz");
        check (std::abs (at44 - at96) < 0.002, "44.1k and 96k agree");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

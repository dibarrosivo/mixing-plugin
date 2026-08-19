#pragma once

#include <algorithm>

/*
    The static curve of a dynamics processor: given an input level in dB, how
    much gain should be applied?

    Deliberately separate from EnvelopeFollower. The detector decides *when*,
    this decides *how much*, and keeping them apart is what lets the same two
    pieces build a compressor, a multiband band, a de-esser and a dynamic EQ
    band without any of them knowing about each other.

    Everything is in dB, and the returned gain is always <= 0 — these are
    downward, subtractive processors. That matches the client's "EQ dinámica
    sustractiva" directly: a dynamic EQ band is this curve driving a filter's
    gain instead of a broadband VCA.

    ── The knee ────────────────────────────────────────────────────────────
    A hard knee switches from 1:1 to full ratio at a single point, which is
    audible as a "grab" on material that hovers around the threshold — exactly
    what a vocal does. The soft knee interpolates quadratically across a window
    centred on the threshold, and is continuous in both value and slope at each
    edge. Vocals almost always want some knee.
*/
namespace dsp
{

class GainComputer
{
public:
    void setThresholdDb (float newThresholdDb) noexcept
    {
        thresholdDb = newThresholdDb;
    }

    // Ratios below 1:1 would be upward expansion, which these processors do not
    // do. Clamped rather than asserted so a bad parameter cannot produce a
    // boost on the audio thread.
    void setRatio (float newRatio) noexcept
    {
        ratio = std::max (1.0f, newRatio);
    }

    void setKneeWidthDb (float newKneeDb) noexcept
    {
        kneeDb = std::max (0.0f, newKneeDb);
    }

    float getThresholdDb() const noexcept { return thresholdDb; }
    float getRatio()       const noexcept { return ratio; }
    float getKneeWidthDb() const noexcept { return kneeDb; }

    /*  Input: signal level in dB. Output: gain to apply, in dB (<= 0).

        Zero means "leave it alone", -6 means "turn this down by 6 dB".
    */
    float computeGainDb (float levelDb) const noexcept
    {
        const auto overshoot = levelDb - thresholdDb;

        // Below the knee — untouched.
        if (2.0f * overshoot <= -kneeDb)
            return 0.0f;

        // Above the knee — full ratio.
        if (2.0f * overshoot >= kneeDb)
            return (thresholdDb + overshoot / ratio) - levelDb;

        // Inside the knee. Quadratic interpolation, continuous in value and
        // slope at both edges; see the tests, which pin that down.
        const auto x = overshoot + kneeDb * 0.5f;
        return (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
    }

private:
    float thresholdDb { -18.0f };
    float ratio       { 4.0f };
    float kneeDb      { 6.0f };
};

} // namespace dsp

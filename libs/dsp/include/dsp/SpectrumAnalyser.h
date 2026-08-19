#pragma once

#include <array>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

#include <dsp/Decibels.h>
#include <dsp/SpscRingBuffer.h>

/*
    Real-time spectrum analyser: the translucent grey shape behind the curve.

    Split cleanly across two threads, because it has to be:

      pushSamples()   audio thread. Writes into a lock-free FIFO. That is all
                      it does — no FFT, no allocation, no maths.
      update()        message thread. Drains the FIFO, windows, transforms,
                      converts to dB and applies temporal smoothing.

    Three decisions that separate an analyser that looks professional from one
    that looks like a school project:

      1. A Hann window. Transforming a raw block treats it as if it looped, and
         the discontinuity at the seam smears energy across every bin. The
         result is a noise floor that rises and falls with the music.

      2. Asymmetric smoothing — instant attack, slow decay. Without it the
         display strobes at the frame rate and is unreadable. With it, peaks
         register instantly and fall away smoothly, which is what the eye
         expects from a level display.

      3. Peak-over-range when drawing, not point sampling. Above a few kHz one
         pixel spans dozens of bins; sampling one of them makes the top octave
         look sparse and jittery instead of dense.
*/
namespace dsp
{

template <int Order = 12>
class SpectrumAnalyser
{
public:
    // size_t rather than int: these are array extents, and mixing the two here
    // produces a sign-conversion warning at every declaration below.
    static constexpr size_t fftSize  = size_t { 1 } << Order;
    static constexpr size_t numBins  = fftSize / 2;
    static constexpr size_t fifoSize = size_t { 1 } << (Order + 2);

    SpectrumAnalyser()
        : fft (Order),
          window (fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        smoothedDb.fill (floorDb);
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
    }

    void reset()
    {
        fifo.reset();
        rollingBuffer.fill (0.0f);
        rollingIndex = 0;
        smoothedDb.fill (floorDb);
    }

    // ── Audio thread ────────────────────────────────────────────────────
    void pushSamples (const float* samples, int numSamples) noexcept
    {
        fifo.write (samples, (size_t) numSamples);
    }

    // ── Message thread ──────────────────────────────────────────────────
    // Returns true when the spectrum changed and the display should repaint.
    bool update()
    {
        // If the UI has been starved — window hidden, host busy — do not grind
        // through a backlog of stale audio. Skip to the newest.
        fifo.discardAllButNewest (fifoSize / 2);

        std::array<float, 512> scratch;
        size_t totalRead = 0;

        while (const auto got = fifo.read (scratch.data(), scratch.size()))
        {
            for (size_t i = 0; i < got; ++i)
            {
                rollingBuffer[rollingIndex] = scratch[i];
                rollingIndex = (rollingIndex + 1) % fftSize;
            }
            totalRead += got;
        }

        if (totalRead == 0)
            return decayOnly();

        // Unwrap the rolling window into transform order, oldest first.
        for (size_t i = 0; i < fftSize; ++i)
            fftData[i] = rollingBuffer[(rollingIndex + i) % fftSize];

        std::fill (fftData.begin() + (ptrdiff_t) fftSize, fftData.end(), 0.0f);

        window.multiplyWithWindowingTable (fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // Hann halves the coherent gain, so undo it to keep a full-scale sine
        // reading 0 dBFS rather than -6.
        constexpr float windowCorrection = 2.0f;
        const auto normalise = windowCorrection / (float) fftSize;

        for (size_t bin = 0; bin < numBins; ++bin)
        {
            const auto magnitude = fftData[bin] * normalise;
            const auto db = gainToDecibels (magnitude, floorDb);

            auto& target = smoothedDb[bin];
            target = db > target ? db                                  // instant attack
                                 : target + (db - target) * decayRate; // slow decay
        }

        return true;
    }

    /*  Peak level in dBFS across a frequency span — one display pixel's worth.

        Ranges rather than points, so the top of the spectrum stays dense
        instead of aliasing into spikes.
    */
    float magnitudeDbForRange (float lowHz, float highHz) const noexcept
    {
        const auto binWidth = (float) sampleRate / (float) fftSize;

        // Clamped as signed first: a low frequency can floor to a negative bin,
        // and converting that to size_t would wrap to an enormous index.
        const auto highestBin = (int) numBins - 1;

        const auto lowBin  = lowHz  / binWidth;
        const auto highBin = highHz / binWidth;

        // Below roughly 500 Hz a display pixel is narrower than one FFT bin, so
        // peak-over-range keeps returning the same bin and the spectrum draws as
        // visible stair-steps. Interpolate between neighbours instead.
        if (highBin - lowBin < 1.0f)
        {
            const auto centre = 0.5f * (lowBin + highBin);
            const auto lower  = juce::jlimit (0, highestBin, (int) std::floor (centre));
            const auto upper  = juce::jlimit (0, highestBin, lower + 1);
            const auto t      = juce::jlimit (0.0f, 1.0f, centre - std::floor (centre));

            const auto a = smoothedDb[(size_t) lower];
            const auto b = smoothedDb[(size_t) upper];

            return a + (b - a) * t;
        }

        // Wider than a bin — one pixel covers many, so show the loudest. Point
        // sampling here would make the top octave look sparse and jittery.
        const auto firstBin = juce::jlimit (0, highestBin, (int) std::floor (lowBin));
        const auto lastBin  = juce::jlimit (firstBin, highestBin, (int) std::ceil (highBin));

        auto peak = floorDb;

        for (int bin = firstBin; bin <= lastBin; ++bin)
            peak = std::max (peak, smoothedDb[(size_t) bin]);

        return peak;
    }

    void setDecayRate (float newRate) noexcept
    {
        decayRate = juce::jlimit (0.005f, 1.0f, newRate);
    }

private:
    // No new audio: keep decaying so a stopped transport falls away instead of
    // freezing mid-display.
    bool decayOnly()
    {
        bool changed = false;

        for (auto& value : smoothedDb)
        {
            if (value > floorDb + 0.01f)
            {
                value += (floorDb - value) * decayRate;
                changed = true;
            }
        }

        return changed;
    }

    static constexpr float floorDb = -100.0f;

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    SpscRingBuffer<fifoSize> fifo;

    std::array<float, fftSize>     rollingBuffer {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, numBins>     smoothedDb {};

    size_t rollingIndex { 0 };
    double sampleRate { 48000.0 };
    float decayRate { 0.12f };
};

} // namespace dsp

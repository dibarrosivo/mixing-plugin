#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>

/*
    Single-producer / single-consumer lock-free ring buffer.

    This is the bridge from the audio thread to the UI. It exists because the
    obvious solutions are all wrong:

      * A mutex round the shared buffer will, sooner or later, make the audio
        thread wait on the message thread. That is a dropout.
      * Painting directly from the audio buffer races with the host overwriting
        it, and produces an analyser that flickers for reasons you cannot see.

    Exactly one thread may push and exactly one may read. That constraint is
    what lets both sides run without ever blocking each other.

    Capacity must be a power of two so the wrap is a mask rather than a modulo,
    and one slot is left unused so that "full" and "empty" stay distinguishable
    without a separate count.

    Overflow drops the newest samples rather than blocking or overwriting. For
    an analyser that is the right trade: a UI that stalls costs you a frame,
    while an audio thread that waits costs you a click.
*/
namespace dsp
{

template <size_t Capacity>
class SpscRingBuffer
{
    static_assert (Capacity >= 2, "Capacity must be at least 2");
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    static constexpr size_t capacity = Capacity;

    // Usable slots. One is sacrificed to tell full from empty.
    static constexpr size_t maxSize = Capacity - 1;

    void reset() noexcept
    {
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store  (0, std::memory_order_relaxed);
    }

    size_t size() const noexcept
    {
        const auto w = writeIndex.load (std::memory_order_acquire);
        const auto r = readIndex.load  (std::memory_order_acquire);
        return (w - r) & mask;
    }

    size_t freeSpace() const noexcept { return maxSize - size(); }
    bool   isEmpty()   const noexcept { return size() == 0; }
    bool   isFull()    const noexcept { return size() == maxSize; }

    // ── Producer side (audio thread) ────────────────────────────────────
    bool push (float value) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & mask;

        if (next == readIndex.load (std::memory_order_acquire))
            return false;                       // full — drop

        buffer[w] = value;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    // Returns how many were actually written; short writes mean the consumer
    // has fallen behind.
    size_t write (const float* source, size_t count) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load  (std::memory_order_acquire);

        const auto available = (r - w - 1) & mask;
        const auto toWrite   = std::min (count, available);

        // At most two contiguous runs: up to the end of the array, then the
        // wrapped remainder. Two memcpy-shaped loops beat a per-sample modulo.
        const auto firstRun = std::min (toWrite, Capacity - w);
        std::copy_n (source, firstRun, buffer.begin() + static_cast<ptrdiff_t> (w));

        if (toWrite > firstRun)
            std::copy_n (source + firstRun, toWrite - firstRun, buffer.begin());

        writeIndex.store ((w + toWrite) & mask, std::memory_order_release);
        return toWrite;
    }

    // ── Consumer side (UI thread) ───────────────────────────────────────
    size_t read (float* destination, size_t count) noexcept
    {
        const auto r = readIndex.load  (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);

        const auto available = (w - r) & mask;
        const auto toRead    = std::min (count, available);

        const auto firstRun = std::min (toRead, Capacity - r);
        std::copy_n (buffer.begin() + static_cast<ptrdiff_t> (r), firstRun, destination);

        if (toRead > firstRun)
            std::copy_n (buffer.begin(), toRead - firstRun, destination + firstRun);

        readIndex.store ((r + toRead) & mask, std::memory_order_release);
        return toRead;
    }

    // Throw away all but the newest `keep` samples. An analyser that has been
    // starved of repaints should show current audio, not work through a backlog.
    void discardAllButNewest (size_t keep) noexcept
    {
        const auto current = size();

        if (current <= keep)
            return;

        const auto r = readIndex.load (std::memory_order_relaxed);
        readIndex.store ((r + (current - keep)) & mask, std::memory_order_release);
    }

private:
    static constexpr size_t mask = Capacity - 1;

    std::array<float, Capacity> buffer {};
    std::atomic<size_t> writeIndex { 0 };
    std::atomic<size_t> readIndex  { 0 };
};

} // namespace dsp

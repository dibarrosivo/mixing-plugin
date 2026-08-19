/*
    Tests for SpscRingBuffer.

        g++ -std=c++20 -O2 -pthread -Ilibs/dsp/include -o /tmp/fifo_test \
            libs/dsp/tests/spsc_ring_buffer_test.cpp && /tmp/fifo_test

    Two classes of bug live here and they fail very differently:

      * Index arithmetic — off-by-one at the wrap, full/empty confusion. These
        are deterministic and the single-threaded sections below pin them down.

      * Memory ordering — invisible on x86, which has strong ordering, and then
        it corrupts on ARM. Section 8 runs a real two-thread soak so the
        producer/consumer contract is exercised rather than assumed.
*/

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <dsp/SpscRingBuffer.h>

using namespace dsp;

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
        if (! ok) ++failures;
    }

    void checkEqual (long long actual, long long expected, const std::string& what)
    {
        const auto ok = actual == expected;
        std::printf ("  [%s] %-50s expected %6lld   got %6lld\n",
                     ok ? " ok " : "FAIL", what.c_str(), expected, actual);
        if (! ok) ++failures;
    }
}

int main()
{
    std::printf ("\n== 1. Empty on construction ==\n");
    {
        SpscRingBuffer<16> fifo;
        check (fifo.isEmpty(), "starts empty");
        check (! fifo.isFull(), "starts not full");
        checkEqual ((long long) fifo.size(), 0, "size is 0");
        checkEqual ((long long) fifo.freeSpace(), 15, "free space is capacity-1");

        float sink[4];
        checkEqual ((long long) fifo.read (sink, 4), 0, "reading an empty fifo yields nothing");
    }

    std::printf ("\n== 2. Round-trip preserves values and order ==\n");
    {
        SpscRingBuffer<16> fifo;
        const float source[5] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };

        checkEqual ((long long) fifo.write (source, 5), 5, "wrote 5");
        checkEqual ((long long) fifo.size(), 5, "size is 5");

        float sink[5] {};
        checkEqual ((long long) fifo.read (sink, 5), 5, "read 5");

        bool identical = true;
        for (int i = 0; i < 5; ++i)
            if (sink[i] != source[i]) identical = false;

        check (identical, "values and order preserved");
        check (fifo.isEmpty(), "empty again after draining");
    }

    std::printf ("\n== 3. Full means capacity-1, and overflow drops ==\n");
    {
        SpscRingBuffer<8> fifo;
        std::vector<float> source (20, 1.0f);

        checkEqual ((long long) fifo.write (source.data(), 20), 7,
                    "write of 20 into an 8-slot fifo accepts 7");
        check (fifo.isFull(), "reports full");
        check (! fifo.push (99.0f), "push onto a full fifo fails");
        checkEqual ((long long) fifo.size(), 7, "size unchanged after a rejected push");
    }

    std::printf ("\n== 4. Wraparound is correct ==\n");
    {
        // Drive the indices past the end of the array several times. An
        // off-by-one in the two-run copy shows up here and nowhere else.
        SpscRingBuffer<8> fifo;
        int nextValue = 0;
        bool correct = true;

        for (int round = 0; round < 50; ++round)
        {
            float out[5] {};
            float in[5];
            for (int i = 0; i < 5; ++i) in[i] = (float) (nextValue + i);

            if (fifo.write (in, 5) != 5) { correct = false; break; }
            if (fifo.read (out, 5) != 5) { correct = false; break; }

            for (int i = 0; i < 5; ++i)
                if (out[i] != (float) (nextValue + i)) correct = false;

            nextValue += 5;
        }

        check (correct, "250 samples through an 8-slot buffer, all in order");
    }

    std::printf ("\n== 5. Partial reads leave the remainder intact ==\n");
    {
        SpscRingBuffer<16> fifo;
        const float source[6] = { 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f };
        fifo.write (source, 6);

        float sink[2] {};
        fifo.read (sink, 2);
        check (sink[0] == 10.0f && sink[1] == 20.0f, "first two read correctly");
        checkEqual ((long long) fifo.size(), 4, "four remain");

        float rest[4] {};
        fifo.read (rest, 4);
        check (rest[0] == 30.0f && rest[3] == 60.0f, "remainder is still in order");
    }

    std::printf ("\n== 6. discardAllButNewest keeps the newest ==\n");
    {
        SpscRingBuffer<64> fifo;
        for (int i = 0; i < 40; ++i)
            fifo.push ((float) i);

        fifo.discardAllButNewest (10);
        checkEqual ((long long) fifo.size(), 10, "10 remain");

        float sink[10] {};
        fifo.read (sink, 10);
        checkEqual ((long long) sink[0], 30, "oldest kept is 30, not 0");
        checkEqual ((long long) sink[9], 39, "newest kept is 39");
    }

    std::printf ("\n== 7. discardAllButNewest is a no-op when under the limit ==\n");
    {
        SpscRingBuffer<64> fifo;
        for (int i = 0; i < 5; ++i)
            fifo.push ((float) i);

        fifo.discardAllButNewest (10);
        checkEqual ((long long) fifo.size(), 5, "nothing discarded");
    }

    std::printf ("\n== 8. Two-thread soak: nothing lost, nothing reordered ==\n");
    {
        // The single-threaded sections cannot catch a memory-ordering mistake.
        // This can — a missing release/acquire pair shows up as a value arriving
        // before the data it describes.
        SpscRingBuffer<1024> fifo;

        constexpr long long total = 2'000'000;
        std::atomic<bool> producerDone { false };
        std::atomic<long long> consumed { 0 };
        std::atomic<long long> mismatches { 0 };

        std::thread producer ([&]
        {
            long long sent = 0;
            while (sent < total)
                if (fifo.push ((float) (sent % 1024)))
                    ++sent;

            producerDone.store (true, std::memory_order_release);
        });

        std::thread consumer ([&]
        {
            long long received = 0;
            float sink[128];

            while (received < total)
            {
                const auto got = fifo.read (sink, 128);

                for (size_t i = 0; i < got; ++i)
                {
                    if (sink[i] != (float) ((received + (long long) i) % 1024))
                        mismatches.fetch_add (1, std::memory_order_relaxed);
                }

                received += (long long) got;

                if (got == 0 && producerDone.load (std::memory_order_acquire) && fifo.isEmpty())
                    break;
            }

            consumed.store (received, std::memory_order_release);
        });

        producer.join();
        consumer.join();

        checkEqual (consumed.load(), total, "every sample arrived");
        checkEqual (mismatches.load(), 0, "every sample arrived in order");
    }

    std::printf ("\n== 9. reset() empties the buffer ==\n");
    {
        SpscRingBuffer<16> fifo;
        for (int i = 0; i < 10; ++i) fifo.push ((float) i);

        fifo.reset();
        check (fifo.isEmpty(), "empty after reset");
        checkEqual ((long long) fifo.size(), 0, "size is 0");
    }

    std::printf ("\n%s  (%d failure%s)\n\n",
                 failures == 0 ? "ALL PASSED" : "FAILURES",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}

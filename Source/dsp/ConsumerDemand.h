#pragma once

#include <atomic>
#include <cstdint>

namespace nl
{
// Lets the audio thread skip work that nobody is reading. A consumer calls touch() every time it
// reads (any thread). The producer calls activeFor() once per block: it stays active for
// kHoldSeconds after the last touch, so a consumer that polls at 30 Hz keeps it active, and an
// editor that is closed (no reads) lets it go idle. Real-time safe: no allocation, no lock.
class ConsumerDemand
{
public:
    static constexpr double kHoldSeconds = 0.5;

    // Message thread, with audio stopped (or on a sample-rate change). Idle until the first touch.
    void prepare (double sampleRate) noexcept
    {
        hold = (uint64_t) (kHoldSeconds * sampleRate);
        idle = hold;
        touched.store (false, std::memory_order_relaxed);
    }

    // Consumer side, any thread.
    void touch() noexcept
    {
        if (! touched.load (std::memory_order_relaxed)) touched.store (true, std::memory_order_relaxed);
    }

    // Audio thread, once per block of n samples. Returns whether the consumer is active for this block.
    bool activeFor (int n) noexcept
    {
        if (touched.load (std::memory_order_relaxed))
        {
            touched.store (false, std::memory_order_relaxed);
            idle = 0;
        }
        else
        {
            idle += (uint64_t) n;
        }
        return idle < hold;
    }

private:
    std::atomic<bool> touched { false };
    uint64_t hold = 0, idle = 0; // audio thread only, except the first two in prepare()
};
} // namespace nl

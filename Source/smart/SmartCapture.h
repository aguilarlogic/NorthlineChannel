#pragma once

#include "SmartAnalysis.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nl::smart
{
// Captures kCaptureSeconds of the strip input (mono: the channels averaged, taken before the
// input stage) for Smart Start.
//
// The buffer is allocated in prepare(), on the message thread, and never reallocated while the
// audio thread may write it. write() runs on the audio thread: it copies samples and touches
// atomics only, with no allocation and no lock. The message thread calls request() to start a
// capture, polls ready(), and then takes the samples with take().
class SmartCapture
{
public:
    enum State : int { Idle = 0, Recording = 1, Ready = 2 };

    // Message thread, while audio is stopped (prepareToPlay). Cancels any capture in progress.
    void prepare (double sampleRate)
    {
        const size_t length = (size_t) std::ceil (kCaptureSeconds * sampleRate);
        buffer.assign (length, 0.0f);
        capacity = length;
        state.store (Idle, std::memory_order_release);
        requestSerial.store (requestSerial.load() + 1, std::memory_order_release);
        seenSerial = requestSerial.load();
        written = 0;
    }

    // Message thread: starts a capture of the full length at the next block. Only call when idle.
    void request() noexcept
    {
        target.store (capacity, std::memory_order_relaxed);
        requestSerial.fetch_add (1, std::memory_order_release);
    }

    // Message thread: abandons a capture that has not finished.
    void cancel() noexcept
    {
        if (state.load (std::memory_order_acquire) == Recording) state.store (Idle, std::memory_order_release);
        target.store (0, std::memory_order_relaxed);
    }

    State getState() const noexcept { return (State) state.load (std::memory_order_acquire); }
    bool ready() const noexcept { return getState() == Ready; }

    // Message thread, once ready(): copies the captured mono samples out and frees the buffer
    // for the next capture.
    std::vector<float> take()
    {
        std::vector<float> out;
        if (! ready()) return out;
        out.assign (buffer.begin(), buffer.begin() + (std::ptrdiff_t) written);
        state.store (Idle, std::memory_order_release);
        return out;
    }

    // Audio thread. io holds numCh (1 or 2) channels of input, before any processing.
    void write (const float* const* io, int numCh, int n) noexcept
    {
        const auto serial = requestSerial.load (std::memory_order_acquire);
        if (serial != seenSerial)
        {
            // A new request always starts a capture, even over a stale Ready left by cancel().
            seenSerial = serial;
            written = 0;
            if (target.load (std::memory_order_relaxed) > 0) state.store (Recording, std::memory_order_release);
        }
        if (state.load (std::memory_order_relaxed) != Recording) return;

        const size_t limit = std::min (target.load (std::memory_order_relaxed), capacity);
        const size_t count = std::min ((size_t) n, limit - written);
        float* dst = buffer.data() + written;
        if (numCh > 1)
            for (size_t i = 0; i < count; ++i) dst[i] = 0.5f * (io[0][i] + io[1][i]);
        else
            for (size_t i = 0; i < count; ++i) dst[i] = io[0][i];
        written += count;
        if (written >= limit) state.store (Ready, std::memory_order_release);
    }

private:
    std::vector<float> buffer;      // allocated in prepare(); its size is the capacity
    size_t capacity = 0;
    std::atomic<size_t> target { 0 };
    std::atomic<unsigned> requestSerial { 0 };
    std::atomic<int> state { Idle };
    unsigned seenSerial = 0;        // audio thread only
    size_t written = 0;             // audio thread only, valid once Ready
};
} // namespace nl::smart

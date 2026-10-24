#pragma once

#include "Meters.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nl
{
// One scope point: the peak levels (dBFS) of one 5 ms grid cell at the module's input and output,
// and the module's gain reduction (<= 0 dB) and detector level (dBFS) over the same cell.
struct ScopePoint
{
    float inDb = -120.0f, outDb = -120.0f, grDb = 0.0f, keyDb = -120.0f;
};

// Decimates a module slot's signal onto a fixed 5 ms grid and hands the points to the UI through a
// wait-free SPSC ring.
//
// The grid is driven by absolute sample time: a persistent 64-bit sample counter (from prepare())
// decides where each cell closes, so the points do not depend on the host block size. Input and
// output levels are sample-exact. Gain reduction and detector level are block-rate values from the
// module: a cell keeps the most reduced GR and the highest key level seen while it was open.
//
// Audio thread: process(). UI thread: pop(). Allocated in the constructor; prepare() only resets the
// grid and bumps a generation, so a consumer drops what was written before a prepare().
class ScopeTap
{
public:
    static constexpr double kPeriodSec = 0.005;
    static constexpr size_t kCapacity = 1024; // points (about 5 s at 200 per second)

    ScopeTap() { ring.allocate (kCapacity); }

    // Message thread, with audio stopped (or on a sample-rate change).
    void prepare (double sampleRate) noexcept
    {
        hop = (uint64_t) juce::jmax (1L, std::lround (sampleRate * kPeriodSec));
        pos = 0;
        closeAt = hop - 1;
        peakIn = peakOut = 0.0f;
        grMin = 0.0f;
        keyMax = -120.0f;
        ringGeneration.fetch_add (1, std::memory_order_release);
    }

    // Audio thread. in and out are numCh channel pointers of n samples (they may alias).
    void process (const float* const* in, const float* const* out, int numCh, int n, float gainReductionDb, float keyDb) noexcept
    {
        grMin = std::min (grMin, gainReductionDb);
        keyMax = std::max (keyMax, keyDb);
        for (int i = 0; i < n; ++i, ++pos)
        {
            for (int c = 0; c < numCh; ++c)
            {
                peakIn = std::max (peakIn, std::abs (in[c][i]));
                peakOut = std::max (peakOut, std::abs (out[c][i]));
            }
            if (pos == closeAt)
            {
                ring.push (ScopePoint { toDb (peakIn), toDb (peakOut), grMin, keyMax });
                peakIn = peakOut = 0.0f;
                closeAt += hop;
                grMin = gainReductionDb;
                keyMax = keyDb;
            }
        }
    }

    // UI thread. Copies up to maxCount points, oldest first. After a prepare() it discards the ring
    // and returns 0 once.
    size_t pop (ScopePoint* out, size_t maxCount) noexcept
    {
        if (maxCount == 0) return 0;
        const auto gen = ringGeneration.load (std::memory_order_acquire);
        if (gen != seenGeneration)
        {
            seenGeneration = gen;
            while (ring.pop (out, maxCount) == maxCount) {}
            return 0;
        }
        return ring.pop (out, maxCount);
    }

    // Number of samples per grid cell at the prepared rate (0 before prepare()).
    int samplesPerCell() const noexcept { return (int) hop; }

    // Dropped points are not counted; the ring holds kCapacity points.
    static float toDb (float peak) noexcept { return peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f; }

private:
    SpscRing<ScopePoint> ring;
    std::atomic<uint32_t> ringGeneration { 0 };
    uint32_t seenGeneration = 0; // consumer side
    uint64_t hop = 240, pos = 0, closeAt = 239;
    float peakIn = 0.0f, peakOut = 0.0f, grMin = 0.0f, keyMax = -120.0f;
};
} // namespace nl

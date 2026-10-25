#pragma once

#include "ConsumerDemand.h"
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
        gridStale = false;
        peakIn = peakOut = 0.0f;
        grMin = 0.0f;
        keyMax = -120.0f;
        demand.prepare (sampleRate);
        ringGeneration.fetch_add (1, std::memory_order_release);
    }

    // Audio thread. in and out are numCh channel pointers of n samples (they may alias). Nothing is
    // computed while no consumer has read the tap lately (see ConsumerDemand); the grid still advances.
    void process (const float* const* in, const float* const* out, int numCh, int n, float gainReductionDb, float keyDb) noexcept
    {
        if (! demand.activeFor (n))
        {
            skip (n);
            return;
        }
        if (gridStale) // the grid was advanced while idle: re-align the open cell now
        {
            closeAt = nextCloseAt (pos);
            peakIn = peakOut = 0.0f;
            grMin = 0.0f;
            keyMax = -120.0f;
            gridStale = false;
        }
        grMin = std::min (grMin, gainReductionDb);
        keyMax = std::max (keyMax, keyDb);
        int i = 0;
        while (i < n)
        {
            // The samples up to the next close point, that point included, belong to the open cell.
            const int len = (int) std::min<uint64_t> ((uint64_t) (n - i), closeAt - pos + 1);
            for (int c = 0; c < numCh; ++c)
            {
                peakIn = std::max (peakIn, peakOf (in[c] + i, len));
                peakOut = std::max (peakOut, peakOf (out[c] + i, len));
            }
            i += len;
            pos += (uint64_t) len;
            if (pos - 1 == closeAt)
            {
                ring.push (ScopePoint { toDb (peakIn), toDb (peakOut), grMin, keyMax });
                peakIn = peakOut = 0.0f;
                closeAt += hop;
                grMin = gainReductionDb;
                keyMax = keyDb;
            }
        }
    }

    // Largest magnitude of len samples. Reads exactly len samples (four independent maxima, so the loop is not
    // a single dependency chain).
    static float peakOf (const float* x, int len) noexcept
    {
        float m0 = 0.0f, m1 = 0.0f, m2 = 0.0f, m3 = 0.0f;
        int j = 0;
        for (; j + 4 <= len; j += 4)
        {
            m0 = std::max (m0, std::abs (x[j]));
            m1 = std::max (m1, std::abs (x[j + 1]));
            m2 = std::max (m2, std::abs (x[j + 2]));
            m3 = std::max (m3, std::abs (x[j + 3]));
        }
        for (; j < len; ++j) m0 = std::max (m0, std::abs (x[j]));
        return std::max (std::max (m0, m1), std::max (m2, m3));
    }

    // UI thread. Copies up to maxCount points, oldest first. The first call after a prepare() discards
    // everything in the ring (points from before the prepare and up to one UI interval after it) and
    // returns 0.
    size_t pop (ScopePoint* out, size_t maxCount) noexcept
    {
        demand.touch();
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

    // Audio thread: advances the grid over n samples without measuring them (no consumer is reading).
    // The next cell closes at the same absolute position it would have, and the open cell starts empty.
    void skip (int n) noexcept
    {
        pos += (uint64_t) n;
        gridStale = true; // the next measured block re-aligns the cell (same result, no work while idle)
    }

    // Number of samples per grid cell at the prepared rate (0 before prepare()).
    int samplesPerCell() const noexcept { return (int) hop; }

    // Dropped points are not counted; the ring holds kCapacity points.
    static float toDb (float peak) noexcept { return peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f; }

private:
    // The first close point at or after sample index p: cells close at hop - 1 + k * hop.
    uint64_t nextCloseAt (uint64_t p) const noexcept
    {
        if (p <= hop - 1) return hop - 1;
        return hop - 1 + ((p - (hop - 1) + hop - 1) / hop) * hop;
    }

    SpscRing<ScopePoint> ring;
    ConsumerDemand demand;
    std::atomic<uint32_t> ringGeneration { 0 };
    uint32_t seenGeneration = 0; // consumer side
    uint64_t hop = 240, pos = 0, closeAt = 239;
    float peakIn = 0.0f, peakOut = 0.0f, grMin = 0.0f, keyMax = -120.0f;
    bool gridStale = false; // audio thread: set by skip(), cleared by the next measured block
};
} // namespace nl

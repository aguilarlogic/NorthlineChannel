#pragma once

// CPU timing for the benchmarks and measurement suites.
//
// Wall-clock numbers are unusable on a machine that compiles in parallel: a block that is pre-empted
// for 2 ms looks like a 2 ms block. On Windows the calling thread's cycle count (QueryThreadCycleTime)
// counts only the cycles this thread ran, so pre-emption does not show up in it. The cycle count is
// converted to seconds with a rate calibrated against the steady clock (the highest of a few short
// spins, so a pre-emption during a spin does not lower it). Elsewhere the thread clock falls back to
// the steady clock, so there the "CPU" numbers are wall-clock numbers.
//
// QueryThreadCycleTime is a system call (about 0.1 us), so it is called once per timed run, never per
// block. Per-block wall times (for the p99) come from the high-resolution counter instead.

#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace tool
{
struct CpuClock
{
    static constexpr bool isThreadTime =
#if defined(_WIN32)
        true;
#else
        false;
#endif

    // Steady wall clock in seconds.
    static double wallSeconds() noexcept
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Thread CPU time of the calling thread, in seconds (see the file comment).
    static double threadSeconds() noexcept
    {
#if defined(_WIN32)
        static const double cyclesPerSecond = calibrate();
        ULONG64 cycles = 0;
        QueryThreadCycleTime (GetCurrentThread(), &cycles);
        return (double) cycles / cyclesPerSecond;
#else
        return wallSeconds();
#endif
    }

    // Cycles per second of the calling thread, from the highest of three 30 ms spins.
    static double calibrate() noexcept
    {
#if defined(_WIN32)
        double best = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            ULONG64 c0 = 0, c1 = 0;
            QueryThreadCycleTime (GetCurrentThread(), &c0);
            const double t0 = wallSeconds();
            double t1 = t0;
            volatile unsigned sink = 0;
            while (t1 - t0 < 0.03)
            {
                for (int i = 0; i < 1000; ++i) sink = sink + (unsigned) i;
                t1 = wallSeconds();
            }
            QueryThreadCycleTime (GetCurrentThread(), &c1);
            best = std::max (best, (double) (c1 - c0) / (t1 - t0));
        }
        return best > 0.0 ? best : 1.0;
#else
        return 1.0;
#endif
    }
};

// Thread CPU time and wall time of one timed run.
struct TimedRun
{
    double cpuUsPerBlock = 0.0;    // mean thread CPU per block, harness cost removed
    double wallUsPerBlock = 0.0;   // mean wall time per block (includes pre-emption)
    double wallP99Us = 0.0;        // 99th percentile wall time per block
    double audioSeconds = 0.0;     // audio duration timed
};

// Runs `warmup` untimed calls and `blocks` timed calls of body (index argument). `harness(index)` does
// the per-block work that is not the plug-in (refilling the input) and is timed separately and
// subtracted from the thread time, so the result is the cost of body alone.
template <typename Body, typename Harness>
TimedRun timeBlocks (int warmup, int blocks, double secondsPerBlock, Body&& body, Harness&& harness)
{
    for (int i = 0; i < warmup; ++i) { harness (i); body (i); }

    // Harness only, same loop shape.
    const double h0 = CpuClock::threadSeconds();
    for (int i = 0; i < blocks; ++i) harness (warmup + i);
    const double harnessSec = CpuClock::threadSeconds() - h0;

    // Harness and plug-in, one thread-time reading around the whole loop; per-block wall times on the side.
    std::vector<double> wall;
    wall.reserve ((size_t) blocks);
    const double tickUs = 1.0e6 / (double) juce::Time::getHighResolutionTicksPerSecond();
    const double c0 = CpuClock::threadSeconds();
    for (int i = 0; i < blocks; ++i)
    {
        harness (warmup + i);
        const auto t0 = juce::Time::getHighResolutionTicks();
        body (warmup + i);
        wall.push_back ((double) (juce::Time::getHighResolutionTicks() - t0) * tickUs);
    }
    const double totalSec = CpuClock::threadSeconds() - c0;

    TimedRun r;
    r.audioSeconds = blocks * secondsPerBlock;
    r.cpuUsPerBlock = std::max (0.0, totalSec - harnessSec) * 1.0e6 / blocks;
    double sum = 0.0;
    for (double t : wall) sum += t;
    r.wallUsPerBlock = sum / (double) blocks;
    std::sort (wall.begin(), wall.end());
    r.wallP99Us = wall[(size_t) (0.99 * (double) (wall.size() - 1))];
    return r;
}

// Median of a small set (the input is copied).
inline double medianOf (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const size_t k = v.size();
    return k % 2 == 1 ? v[k / 2] : 0.5 * (v[k / 2 - 1] + v[k / 2]);
}

inline double minOf (const std::vector<double>& v)
{
    return v.empty() ? 0.0 : *std::min_element (v.begin(), v.end());
}
} // namespace tool

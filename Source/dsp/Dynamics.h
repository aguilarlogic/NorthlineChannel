#pragma once

#include "Common.h"
#include "Filters.h"
#include "../Params.h"
#include <vector>

// Building blocks shared by the three dynamics modules (GATE, DE-ESS, COMP).
//
// Determinism contract used by all three modules:
//  * setParams() only derives coefficients and stores targets. Nothing it computes reaches the
//    signal until the next control tick (an absolute sample index that is a multiple of
//    kControlBlock), so automation applied anywhere inside a tick interval has the same effect
//    whatever the host block size.
//  * Per-sample processing runs in segments that never cross a control tick; the position inside
//    the tick is the persistent unsigned ControlClock phase.
//  * Discrete changes (type, mode, band on/off) start a cross-fade against a preallocated shadow
//    copy of the module core that keeps running the previous configuration.
namespace nl
{
namespace dyn
{
constexpr double kLn9 = 2.1972245773362196;
constexpr int kSeg = kControlBlock; // samples per processing segment (one control tick)

// One-pole coefficient for a 10-90% rise time of `ms`. This is what every attack and release
// knob of the dynamics modules means: the time a step response needs from 10% to 90%.
inline float riseCoef (float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f) return 0.0f;
    return (float) std::exp (-kLn9 / (0.001 * (double) ms * sampleRate));
}

inline int lookaheadSamples (int choice, double sampleRate) noexcept
{
    const int last = (int) (sizeof (kLookaheadMs) / sizeof (kLookaheadMs[0])) - 1;
    return (int) std::lround ((double) kLookaheadMs[std::clamp (choice, 0, last)] * 0.001 * sampleRate);
}

inline int maxLookaheadSamples (double sampleRate) noexcept
{
    return (int) std::ceil ((double) kLookaheadMs[sizeof (kLookaheadMs) / sizeof (kLookaheadMs[0]) - 1] * 0.001 * sampleRate);
}

//==============================================================================
// Mono delay line. The size is fixed in prepare(); process() never allocates.
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        uint32_t size = 4;
        while ((int) size < maxDelay + 2) size <<= 1;
        buf.assign (size, 0.0f);
        mask = size - 1;
        pos = 0;
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }

    // Writes x and returns the sample written `delay` calls ago (delay <= maxDelay).
    inline float process (float x, uint32_t delay) noexcept
    {
        buf[pos & mask] = x;
        const float y = buf[(pos - delay) & mask];
        ++pos;
        return y;
    }

private:
    std::vector<float> buf;
    uint32_t mask = 3, pos = 0;
};

//==============================================================================
// Peak follower: instant attack, a hold of `holdN` samples, then a one-pole release.
// The hold keeps the envelope flat across the gaps between rectified waveform peaks, which
// removes the gain ripple a bare |x| detector causes at low frequencies.
struct PeakDetector
{
    float env = 0.0f;
    int hold = 0;

    inline float process (float a, int holdN, float releaseCoef) noexcept
    {
        if (a >= env) { env = a; hold = holdN; }
        else if (hold > 0) --hold;
        else env = (a + 1.0e-20f) + (env - (a + 1.0e-20f)) * releaseCoef; // floor keeps it out of denormals
        return env;
    }
};

//==============================================================================
// Smoothed value whose segment ramp is interpolated per sample from the absolute tick phase.
struct Ramp
{
    Smoothed sm;
    float from = 0.0f, to = 0.0f;

    void prepare (double sampleRate, float ms) { sm.prepare (sampleRate, ms); }
    void setTarget (float t) noexcept { sm.setTarget (t); }
    void snap() noexcept { sm.snap(); from = to = sm.current; }
    void tick() noexcept { from = to; sm.tick(); to = sm.current; }
    // Value at position p (0 .. kControlBlock-1) inside the current tick.
    inline float at (int p) const noexcept { return from + (to - from) * ((float) p * (1.0f / (float) kControlBlock)); }
};

//==============================================================================
// Raised-cosine cross-fade between a shadow core (old configuration) and the live core.
struct ShadowFade
{
    uint32_t pos = 0, len = 1;
    bool active = false;

    void start (uint32_t samples) noexcept { pos = 0; len = std::max<uint32_t> (1u, samples); active = true; }
    void stop() noexcept { active = false; }

    // Weight of the live core for the next sample (0 = all shadow, ->1 = all live).
    inline float next() noexcept
    {
        const float w = 0.5f - 0.5f * std::cos (kPi * (float) pos / (float) len);
        if (++pos >= len) active = false;
        return w;
    }
};

//==============================================================================
// Slow-moving "memory" of how deep and how long the gain reduction has been, updated at
// control rate (once per tick), so its time constants do not depend on sample rate or channel
// count.
struct Memory
{
    float value = 0.0f;
    inline void update (float depth01, float chargeCoef, float dischargeCoef) noexcept
    {
        value = depth01 + (value - depth01) * (depth01 > value ? chargeCoef : dischargeCoef);
    }
};

//==============================================================================
// Static curves (dB in, dB gain out).

// Compressor (Giannoulis, Massberg & Reiss, JAES 2012): returns the gain change (<= 0) for a
// detector level, with a quadratic soft knee of width `knee` centred on the threshold.
inline float compressorGainDb (float levelDb, float thresh, float invRatio, float knee) noexcept
{
    const float over = levelDb - thresh;
    if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
    {
        const float t = over + 0.5f * knee;
        return (invRatio - 1.0f) * t * t / (2.0f * knee);
    }
    return over <= 0.0f ? 0.0f : (invRatio - 1.0f) * over;
}

// Downward expander: below the threshold the output falls `ratio` dB per input dB, with the
// same quadratic knee construction. Continuous in level and slope (for knee > 0).
inline float expanderGainDb (float levelDb, float thresh, float ratio, float knee) noexcept
{
    const float under = thresh - levelDb;
    const float s = ratio - 1.0f;
    if (knee > 0.0f && 2.0f * std::abs (under) <= knee)
    {
        const float t = under + 0.5f * knee;
        return -s * t * t / (2.0f * knee);
    }
    return under <= 0.0f ? 0.0f : -s * under;
}
} // namespace dyn
} // namespace nl

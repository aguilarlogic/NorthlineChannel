#pragma once

#include "Filters.h"

// Dynamic EQ band detection: the band's region of the detection signal, a mean-square level, and
// the gain law that turns the level into extra band gain. See docs/dsp/eq.md for the numbers.
namespace nl::eqd
{
// Attack / release of the dynamic gain (time constants, 1/e) for the Fast / Medium / Slow choices.
struct DynSpeed
{
    float attackMs, releaseMs;
};

inline constexpr DynSpeed kDynSpeeds[3] { { 2.0f, 50.0f }, { 8.0f, 150.0f }, { 30.0f, 500.0f } };

inline DynSpeed dynSpeed (int choice) noexcept { return kDynSpeeds[std::clamp (choice, 0, 2)]; }

// Level over the threshold (dB) at which the full range is reached.
constexpr float kDynFullRangeDb = 12.0f;
// Soft knee around the threshold (dB, total width).
constexpr float kDynKneeDb = 6.0f;

// Fraction (0..1) of the range applied when the band level is `overDb` above the threshold:
// zero below the knee, quadratic through the knee, linear up to kDynFullRangeDb, then constant.
inline float dynFraction (float overDb) noexcept
{
    constexpr float half = 0.5f * kDynKneeDb;
    float s;
    if (overDb <= -half) return 0.0f;
    if (overDb < half) { const float t = overDb + half; s = t * t / (2.0f * kDynKneeDb); }
    else s = std::min (overDb, kDynFullRangeDb);
    return s * (1.0f / kDynFullRangeDb);
}

// Extra gain in dB for a band level, threshold and range (negative range = dynamic cut).
inline float dynGainDb (float levelDb, float threshDb, float rangeDb) noexcept
{
    return rangeDb * dynFraction (levelDb - threshDb);
}

// Mean-square time constant of the detector for a band frequency: about 1.5 periods, kept
// between 2 and 30 ms so low bands do not ripple at twice their frequency.
inline float detectorTimeMs (float freqHz) noexcept { return std::clamp (1500.0f / std::max (freqHz, 1.0f), 2.0f, 30.0f); }

// Region filter of a band: band-pass for bells, low-pass below a low shelf, high-pass above a
// high shelf. Unity gain in the pass region.
enum class Region { BandPass, LowPass, HighPass };

inline SvfCoefs makeRegionFilter (Region r, double fs, float freq, float q)
{
    switch (r)
    {
        case Region::LowPass:  return makeSvf (SvfType::LowPass, fs, freq, q);
        case Region::HighPass: return makeSvf (SvfType::HighPass, fs, freq, q);
        default:               return makeSvf (SvfType::BandPass, fs, freq, q);
    }
}

// Per-band detector: region filter per channel followed by a one-pole mean-square follower.
struct BandDetector
{
    SvfCoefs c;
    SvfState st[2];
    float ms[2] {};
    float coef = 0.99f; // per-sample mean-square smoothing

    void reset() noexcept
    {
        st[0].reset();
        st[1].reset();
        ms[0] = ms[1] = 0.0f;
    }

    void setFilter (Region r, double fs, float freq, float q)
    {
        c = makeRegionFilter (r, fs, freq, q);
        coef = 1.0f - timeCoef (detectorTimeMs (freq), fs);
    }

    inline void run (int ch, const float* x, int len) noexcept
    {
        float m = ms[ch];
        auto& s = st[ch];
        for (int i = 0; i < len; ++i)
        {
            const float y = s.process (x[i], c);
            m += coef * (y * y - m);
        }
        ms[ch] = m;
    }

    // Called at control ticks: flushes denormals and drops NaN / Inf. Not done inside run() so
    // the result does not depend on how the host splits blocks.
    void sanitize() noexcept
    {
        for (auto& m : ms)
            if (m < 1.0e-20f || ! (m < 1.0e12f)) m = 0.0f;
    }

    // Level in dB (mean square, so a full-scale sine reads -3 dB) of one channel or both linked.
    float levelDb (int numCh, int target) const noexcept
    {
        float m;
        if (numCh == 1) m = ms[0];
        else if (target == 0) m = std::max (ms[0], ms[1]);
        else m = ms[target - 1];
        return 3.0102999566f * fastLog2 (std::max (m, 1.0e-12f));
    }
};
} // namespace nl::eqd

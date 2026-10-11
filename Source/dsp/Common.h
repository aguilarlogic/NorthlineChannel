#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace nl
{
constexpr float kPi = 3.14159265358979323846f;
constexpr double kPiD = 3.14159265358979323846;

// Control-rate granularity: smoothed parameters and filter coefficients are
// refreshed once per kControlBlock samples. The counter persists across host
// blocks, so output does not depend on the host's buffer size.
constexpr int kControlBlock = 32;

//==============================================================================
// Fast log2 / exp2. Accuracy is verified by the tool's unit tests:
// fastLog2 max error < 2e-6 (≈1e-5 dB), fastExp2 max relative error < 3e-6.
inline float fastLog2 (float x) noexcept
{
    x = std::max (x, 1.0e-20f);
    uint32_t i;
    std::memcpy (&i, &x, 4);
    int e = int ((i >> 23) & 0xff) - 127;
    i = (i & 0x007fffffu) | 0x3f800000u;
    float m;
    std::memcpy (&m, &i, 4); // m in [1, 2)
    if (m > 1.41421356f) { m *= 0.5f; ++e; }
    const float t = (m - 1.0f) / (m + 1.0f);
    const float t2 = t * t;
    const float p = t * (2.8853900817779268f + t2 * (0.9617966939259756f + t2 * (0.5770780163555854f + t2 * 0.4121985831111324f)));
    return float (e) + p;
}

inline float fastExp2 (float x) noexcept
{
    x = std::clamp (x, -125.0f, 125.0f);
    const float fi = std::nearbyint (x);
    const float f = x - fi; // [-0.5, 0.5]
    const float p = 1.0f + f * (0.6931471805599453f + f * (0.2402265069591007f + f * (0.05550410866482158f
                        + f * (0.009618129107628477f + f * 0.0013333558146428443f))));
    const uint32_t bits = uint32_t (int (fi) + 127) << 23;
    float s;
    std::memcpy (&s, &bits, 4);
    return s * p;
}

constexpr float kDbPerLog2 = 6.020599913279624f;   // 20*log10(2)
constexpr float kLog2PerDb = 0.16609640474436813f; // 1/kDbPerLog2

inline float fastGainToDb (float g) noexcept { return kDbPerLog2 * fastLog2 (g); }
inline float fastDbToGain (float db) noexcept { return fastExp2 (db * kLog2PerDb); }

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

// One-pole coefficient for a 1/e time constant in milliseconds.
inline float timeCoef (float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f) return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate));
}

//==============================================================================
// Linear ramp used for click-free on/off transitions of modules and sections.
struct Fade
{
    float value = 0.0f, target = 0.0f, step = 0.0f;

    void prepare (double sampleRate, float ms = 10.0f) { step = float (1.0 / std::max (1.0, 0.001 * ms * sampleRate)); }
    void setTarget (bool on) noexcept { target = on ? 1.0f : 0.0f; }
    void snap (bool on) noexcept { value = target = on ? 1.0f : 0.0f; }
    bool isSilent() const noexcept { return value == 0.0f && target == 0.0f; }
    bool isFullyOn() const noexcept { return value == 1.0f && target == 1.0f; }

    float next() noexcept
    {
        if (value < target) value = std::min (target, value + step);
        else if (value > target) value = std::max (target, value - step);
        return value;
    }
};

// Smoothed parameter with a one-pole glide evaluated at control rate.
struct Smoothed
{
    float current = 0.0f, target = 0.0f, coef = 0.0f;
    bool first = true;

    void prepare (double sampleRate, float ms = 30.0f) { coef = timeCoef (ms, sampleRate / kControlBlock); first = true; }
    void setTarget (float t) noexcept { target = t; if (first) { current = t; first = false; } }
    void snap() noexcept { current = target; }
    // Returns true when the value moved (so dependent coefficients need refreshing).
    bool tick() noexcept
    {
        if (current == target) return false;
        current = target + (current - target) * coef;
        if (std::abs (current - target) < 1.0e-4f * (1.0f + std::abs (target))) current = target;
        return true;
    }
};

//==============================================================================
// Persistent control-rate clock. Control ticks fall on absolute sample indices that are
// multiples of kControlBlock, whatever the host's block size, so anything driven by it is
// deterministic across block sizes. The phase is unsigned and wraps safely.
struct ControlClock
{
    uint32_t phase = 0; // samples since the last tick, 0 .. kControlBlock-1

    void reset() noexcept { phase = 0; }
    void advance (int n) noexcept { phase = (phase + (uint32_t) n) % (uint32_t) kControlBlock; }
};

// Smoothed gain applied with a deterministic ramp: the smoother ticks exactly at control
// boundaries and the gain is interpolated linearly across each kControlBlock segment.
struct RampedGain
{
    Smoothed sm;
    float from = 1.0f, to = 1.0f;

    void prepare (double sampleRate, float ms) { sm.prepare (sampleRate, ms); }
    void setTarget (float t) noexcept { sm.setTarget (t); }
    void snap() noexcept { sm.snap(); from = to = sm.current; }
    bool isUnity() const noexcept { return from == 1.0f && to == 1.0f && sm.target == 1.0f; }

    // Applies the gain to n samples of each channel; `phase` is the ControlClock phase at the
    // first sample. Call clock.advance(n) once after every user of the clock has run.
    void apply (float* const* io, int numCh, int n, uint32_t phase) noexcept
    {
        int i = 0;
        while (i < n)
        {
            const int p = (int) ((phase + (uint32_t) i) % (uint32_t) kControlBlock);
            if (p == 0) { sm.tick(); from = to; to = sm.current; }
            const int len = std::min (n - i, kControlBlock - p);
            const float d = (to - from) * (1.0f / (float) kControlBlock);
            for (int c = 0; c < numCh; ++c)
            {
                float g = from + d * (float) p;
                float* x = io[c] + i;
                for (int j = 0; j < len; ++j, g += d) x[j] *= g;
            }
            i += len;
        }
    }
};

// Smoothed plain value with the same deterministic ramp as RampedGain, for stages that need
// the per-sample value themselves (cross-fades, mix). forEachSegment calls
// fn (start, length, valueAtStart, valuePerSample) once per control segment.
struct RampedValue
{
    Smoothed sm;
    float from = 0.0f, to = 0.0f;

    void prepare (double sampleRate, float ms) { sm.prepare (sampleRate, ms); }
    void setTarget (float t) noexcept { sm.setTarget (t); }
    void snap() noexcept { sm.snap(); from = to = sm.current; }
    float target() const noexcept { return sm.target; }
    // True when the value is constant (no ramp running and the smoother has arrived).
    bool isConstant() const noexcept { return from == to && sm.current == sm.target && to == sm.target; }

    template <typename Fn>
    void forEachSegment (int n, uint32_t phase, Fn&& fn) noexcept
    {
        int i = 0;
        while (i < n)
        {
            const int p = (int) ((phase + (uint32_t) i) % (uint32_t) kControlBlock);
            if (p == 0) { sm.tick(); from = to; to = sm.current; }
            const int len = std::min (n - i, kControlBlock - p);
            const float d = (to - from) * (1.0f / (float) kControlBlock);
            fn (i, len, from + d * (float) p, d);
            i += len;
        }
    }
};

inline bool isFiniteAndSmall (float x) noexcept { return std::isfinite (x) && std::abs (x) < 1.0e6f; }

// Orthonormal mid/side matrix (self-inverse, power preserving).
inline void lrToMs (float* l, float* r, int n) noexcept
{
    constexpr float s = 0.70710678118654752f;
    for (int i = 0; i < n; ++i)
    {
        const float a = l[i], b = r[i];
        l[i] = (a + b) * s;
        r[i] = (a - b) * s;
    }
}
} // namespace nl

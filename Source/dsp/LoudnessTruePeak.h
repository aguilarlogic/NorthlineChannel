#pragma once

#include "Common.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

#if JUCE_USE_SSE_INTRINSICS
 #include <xmmintrin.h>
#endif

namespace nl
{
// Inter-sample ("true") peak estimation by polyphase interpolation (ITU-R BS.1770-4 Annex 2).
// The kernel is a Kaiser-windowed sinc with 16 taps per phase (the standard asks for 12), so
// phase 0 reproduces the input sample exactly. 4 phases below 96 kHz, 2 below 192 kHz and 1
// (plain sample peak) above, as the annex allows.
class TruePeakKernel
{
public:
    static constexpr int kTaps = 16;
    static constexpr int kHalf = kTaps / 2; // detection delay in samples
    static constexpr int kMaxPhases = 8;

    int phases = 4;
    float h[kMaxPhases][kTaps] {};
    float l1 = 1.0f; // largest sum of |taps| over the phases: |interpolated| <= l1 * max |input|

    // `basePhases` is the interpolation factor below 96 kHz (4 = BS.1770 true-peak meter; the limiter
    // detector uses 8). Higher sample rates need half as many phases per doubling.
    void design (double sampleRate, int basePhases = 4)
    {
        phases = sampleRate < 96000.0 ? basePhases : (sampleRate < 192000.0 ? basePhases / 2 : std::max (1, basePhases / 4));
        constexpr double beta = 8.0;
        auto bessel0 = [] (double x)
        {
            double s = 1.0, t = 1.0;
            for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
            return s;
        };
        const double b0 = bessel0 (beta);
        l1 = 1.0f;
        for (int p = 0; p < kMaxPhases; ++p)
        {
            double sum = 0.0, d[kTaps] {};
            const double frac = p < phases ? (double) p / phases : 0.0;
            for (int j = 0; j < kTaps; ++j)
            {
                const double u = frac + (kHalf - 1) - j; // distance from tap j to the interpolation point
                const double s = std::abs (u) < 1.0e-12 ? 1.0 : std::sin (kPiD * u) / (kPiD * u);
                const double r = u / kHalf;
                const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / b0;
                d[j] = (p == 0) ? (j == kHalf - 1 ? 1.0 : 0.0) : s * w;
                sum += d[j];
            }
            double abssum = 0.0;
            for (int j = 0; j < kTaps; ++j)
            {
                h[p][j] = (float) (d[j] / sum);
                abssum += std::abs (d[j] / sum);
            }
            if (p < phases) l1 = std::max (l1, (float) abssum);
        }
    }
};

// Dot product of one kernel phase with a window of kTaps samples.
inline float truePeakDot (const float* a, const float* b) noexcept
{
#if JUCE_USE_SSE_INTRINSICS
    __m128 acc = _mm_mul_ps (_mm_loadu_ps (a), _mm_loadu_ps (b));
    acc = _mm_add_ps (acc, _mm_mul_ps (_mm_loadu_ps (a + 4), _mm_loadu_ps (b + 4)));
    acc = _mm_add_ps (acc, _mm_mul_ps (_mm_loadu_ps (a + 8), _mm_loadu_ps (b + 8)));
    acc = _mm_add_ps (acc, _mm_mul_ps (_mm_loadu_ps (a + 12), _mm_loadu_ps (b + 12)));
    alignas (16) float t[4];
    _mm_store_ps (t, acc);
    return (t[0] + t[1]) + (t[2] + t[3]);
#else
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (int j = 0; j < TruePeakKernel::kTaps; j += 4)
    {
        s0 += a[j] * b[j];
        s1 += a[j + 1] * b[j + 1];
        s2 += a[j + 2] * b[j + 2];
        s3 += a[j + 3] * b[j + 3];
    }
    return (s0 + s1) + (s2 + s3);
#endif
}

// One channel of the interpolator. push() every sample; slotPeak() then returns the peak of the
// interpolated values around audio slot (newest - kHalf), i.e. the estimate is delayed by
// TruePeakKernel::kHalf samples relative to the newest input.
class TruePeakChannel
{
public:
    static constexpr int T = TruePeakKernel::kTaps;

    void reset() noexcept
    {
        std::fill (std::begin (hist), std::end (hist), 0.0f);
        blk[0] = blk[1] = blk[2] = 0.0f;
        wi = blkCount = 0;
        prev[0] = prev[1] = 0.0f;
    }

    void push (float x) noexcept
    {
        hist[wi] = hist[wi + T] = x;
        wi = (wi + 1 == T) ? 0 : wi + 1;
        blk[2] = std::max (blk[2], std::abs (x));
        if (++blkCount == 8)
        {
            blk[0] = blk[1];
            blk[1] = blk[2];
            blk[2] = 0.0f;
            blkCount = 0;
        }
    }

    // Upper bound for the last kTaps input magnitudes (a superset window).
    float windowMax() const noexcept { return std::max (blk[0], std::max (blk[1], blk[2])); }

    // Peak magnitude of the interpolated signal at the newest slot. With `refine`, a parabola
    // through three neighbouring grid values estimates the continuous-time maximum (removes most
    // of the up-to-0.5 dB under-read of a 4x grid near Nyquist); without it this is the plain
    // BS.1770 estimate. `skip` leaves the interpolation out (caller knows nothing can matter).
    float slotPeak (const TruePeakKernel& k, bool refine, bool skip) noexcept
    {
        if (skip)
        {
            prev[0] = prev[1] = 0.0f;
            return 0.0f;
        }
        const float* w = hist + wi;
        const int P = k.phases;
        float g[2 + TruePeakKernel::kMaxPhases];
        g[0] = prev[0];
        g[1] = prev[1];
        g[2] = std::abs (w[TruePeakKernel::kHalf - 1]);
        for (int p = 1; p < P; ++p) g[2 + p] = std::abs (truePeakDot (k.h[p], w));

        float best = 0.0f;
        if (! refine)
        {
            for (int p = 0; p < P; ++p) best = std::max (best, g[2 + p]);
        }
        else
        {
            // Points -1 .. P-2 have both neighbours available; the last one is refined next slot.
            for (int m = 1; m < P + 1; ++m)
            {
                const float a = g[m - 1], b = g[m], c = g[m + 1];
                float v = b;
                if (b >= a && b >= c)
                {
                    const float den = 2.0f * b - a - c;
                    if (den > 1.0e-9f) v = b + (a - c) * (a - c) / (8.0f * den);
                }
                best = std::max (best, v);
            }
        }
        if (P == 1)
        {
            prev[0] = prev[1] = 0.0f;
            return g[2];
        }
        prev[0] = g[P];     // grid value of phase P-2
        prev[1] = g[P + 1]; // grid value of phase P-1
        return best;
    }

private:
    float hist[2 * T] {};
    float blk[3] {};
    float prev[2] {};
    int wi = 0, blkCount = 0;
};

// Stereo interpolator for the meter. Same kernel, same dot products and same estimate as
// TruePeakChannel (without refinement). The skip bound is one number for both channels: the
// largest |sample| of the last kTaps pairs, rounded up to whole 4-pair blocks, so it is only
// ever looser (never tighter) than the window. A looser bound merely computes the interpolation
// more often, so the results do not change.
//
// The ring position and block bookkeeping live in a small State that the caller copies into a
// local for its inner loop, so the compiler can keep them in registers.
class TruePeakStereo
{
public:
    static constexpr int T = TruePeakKernel::kTaps;
    static constexpr int kBlock = 4;

    struct State
    {
        int wi = 0, phase = 0, bi = 0;
        float cur = 0.0f; // maximum of the partial block
    };

    void reset() noexcept
    {
        std::fill (&hist[0][0], &hist[0][0] + 2 * 2 * T, 0.0f);
        std::fill (std::begin (blk), std::end (blk), 0.0f);
        state = State {};
    }

    State getState() const noexcept { return state; }
    void setState (const State& s) noexcept { state = s; }

    // Pushes one stereo pair (mono: pass the same sample twice).
    void push (State& s, float l, float r) noexcept
    {
        hist[0][s.wi] = hist[0][s.wi + T] = l;
        hist[1][s.wi] = hist[1][s.wi + T] = r;
        s.wi = (s.wi + 1 == T) ? 0 : s.wi + 1;
        s.cur = std::max (s.cur, std::max (std::abs (l), std::abs (r)));
        if (++s.phase == kBlock)
        {
            blk[s.bi] = s.cur;
            s.bi = (s.bi + 1) & 3;
            s.cur = 0.0f;
            s.phase = 0;
        }
    }

    // Upper bound on |interpolated value| / l1 for both channels. The last kTaps pairs lie inside
    // the four completed blocks plus the partial one.
    float windowMax (const State& s) const noexcept
    {
        return std::max (s.cur, std::max (std::max (blk[0], blk[1]), std::max (blk[2], blk[3])));
    }

    // Peak magnitude of the interpolated channel `ch` at the newest slot (phase 0 is the slot itself).
    float slotPeak (const State& s, const TruePeakKernel& k, int ch) const noexcept
    {
        const float* w = hist[ch] + s.wi;
        float best = std::abs (w[TruePeakKernel::kHalf - 1]);
        for (int p = 1; p < k.phases; ++p) best = std::max (best, std::abs (truePeakDot (k.h[p], w)));
        return best;
    }

private:
    float hist[2][2 * T] {}; // each channel twice, so the window is contiguous
    float blk[4] {};         // maxima of the last four completed blocks
    State state;
};
} // namespace nl

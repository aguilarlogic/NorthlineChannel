#pragma once

#include "Common.h"
#include "Filters.h"
#include <juce_core/juce_core.h>
#include <cmath>

#if JUCE_USE_SSE_INTRINSICS
 #include <emmintrin.h>
#endif

namespace nl
{
// ITU-R BS.1770-4 K-weighting: stage 1 (head-related high shelf) and stage 2 (RLB high-pass),
// designed for the actual sample rate (the standard's table is the 48 kHz case).
struct KWeighting
{
    static void design (double fs, BiquadD& stage1, BiquadD& stage2) noexcept
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPiD * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            stage1.b0 = (Vh + Vb * K / Q + K * K) / a0;
            stage1.b1 = 2.0 * (K * K - Vh) / a0;
            stage1.b2 = (Vh - Vb * K / Q + K * K) / a0;
            stage1.a1 = 2.0 * (K * K - 1.0) / a0;
            stage1.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPiD * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            stage2.b0 = 1.0;
            stage2.b1 = -2.0;
            stage2.b2 = 1.0;
            stage2.a1 = 2.0 * (K * K - 1.0) / a0;
            stage2.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
};

// Two doubles, one per stereo channel. SSE2 on x64 (always available there), scalar elsewhere.
// Every operation is lane-wise, so a lane computes exactly what the scalar code would.
struct Pair2
{
#if JUCE_USE_SSE_INTRINSICS
    __m128d v;
    static Pair2 make (double lane0, double lane1) noexcept { return { _mm_set_pd (lane1, lane0) }; }
    double lane (int i) const noexcept { return i == 0 ? _mm_cvtsd_f64 (v) : _mm_cvtsd_f64 (_mm_unpackhi_pd (v, v)); }
    friend Pair2 operator+ (Pair2 a, Pair2 b) noexcept { return { _mm_add_pd (a.v, b.v) }; }
    friend Pair2 operator- (Pair2 a, Pair2 b) noexcept { return { _mm_sub_pd (a.v, b.v) }; }
    friend Pair2 operator* (Pair2 a, Pair2 b) noexcept { return { _mm_mul_pd (a.v, b.v) }; }
#else
    double d[2] {};
    static Pair2 make (double lane0, double lane1) noexcept { return { { lane0, lane1 } }; }
    double lane (int i) const noexcept { return d[i]; }
    friend Pair2 operator+ (Pair2 a, Pair2 b) noexcept { return { { a.d[0] + b.d[0], a.d[1] + b.d[1] } }; }
    friend Pair2 operator- (Pair2 a, Pair2 b) noexcept { return { { a.d[0] - b.d[0], a.d[1] - b.d[1] } }; }
    friend Pair2 operator* (Pair2 a, Pair2 b) noexcept { return { { a.d[0] * b.d[0], a.d[1] * b.d[1] } }; }
#endif
};

// BiquadD for two channels at once: the same recursion as BiquadD in each lane.
struct BiquadPair
{
    Pair2 b0, b1, b2, a1, a2, z1, z2;

    void setup (const BiquadD& c) noexcept
    {
        b0 = Pair2::make (c.b0, c.b0);
        b1 = Pair2::make (c.b1, c.b1);
        b2 = Pair2::make (c.b2, c.b2);
        a1 = Pair2::make (c.a1, c.a1);
        a2 = Pair2::make (c.a2, c.a2);
        reset();
    }

    void reset() noexcept
    {
        z1 = Pair2::make (0.0, 0.0);
        z2 = Pair2::make (0.0, 0.0);
    }

    inline Pair2 process (Pair2 x) noexcept
    {
        const Pair2 y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Loudness of a mean-square energy value (BS.1770 constant -0.691), in LUFS.
inline double energyToLufs (double energy) noexcept { return energy > 0.0 ? -0.691 + 10.0 * std::log10 (energy) : -200.0; }
inline double lufsToEnergy (double lufs) noexcept { return std::pow (10.0, (lufs + 0.691) * 0.1); }

//==============================================================================
// VU ballistics: the rectified, averaged input drives a second-order low-pass whose step
// response reaches 99% of its final value after 300 ms with about 1.25% overshoot (the IEC 60268-17
// figures: 99% in 300 ms, overshoot 1 to 1.5%). Evaluated at control rate with the exact
// zero-order-hold discretisation, so the result does not depend on the host block size.
class VuBallistics
{
public:
    static constexpr double kOvershoot = 0.0125;   // 1.25%
    static constexpr double kRiseTime99 = 0.300;   // s
    // Calibration: a sine reads RMS (rectified average * pi / (2 sqrt 2)).
    static constexpr float kSineRmsCal = 1.1107207345f;

    void prepare (double controlRate) noexcept
    {
        dt = 1.0 / controlRate;
        const double lnO = std::log (kOvershoot);
        zeta = -lnO / std::sqrt (kPiD * kPiD + lnO * lnO);
        const double s = std::sqrt (1.0 - zeta * zeta);
        // Normalised time tau = w0 * t at which the step response first reaches 99%.
        auto step = [&] (double tau)
        {
            return 1.0 - std::exp (-zeta * tau) * (std::cos (s * tau) + zeta / s * std::sin (s * tau));
        };
        double lo = 0.0, hi = kPiD / s; // monotone up to the first peak
        for (int i = 0; i < 80; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (step (mid) < 0.99 ? lo : hi) = mid;
        }
        w0 = 0.5 * (lo + hi) / kRiseTime99;

        const double sigma = zeta * w0, wd = w0 * s;
        const double e = std::exp (-sigma * dt), c = std::cos (wd * dt), sn = std::sin (wd * dt);
        // exp(A dt) with A = [[0, 1], [-w0^2, -2 sigma]]
        a11 = e * (c + sigma / wd * sn);
        a12 = e * sn / wd;
        a21 = -e * w0 * w0 / wd * sn;
        a22 = e * (c - sigma / wd * sn);
        reset();
    }

    void reset() noexcept { x1 = x2 = 0.0; }

    // `u` is the control-block mean of the rectified input (already scaled by kSineRmsCal).
    void tick (double u) noexcept
    {
        const double d1 = x1 - u, d2 = x2;
        x1 = u + a11 * d1 + a12 * d2;
        x2 = a21 * d1 + a22 * d2;
    }

    double value() const noexcept { return x1; }
    double getZeta() const noexcept { return zeta; }
    double getW0() const noexcept { return w0; }

private:
    double dt = 1.0, zeta = 0.8, w0 = 10.0;
    double a11 = 1, a12 = 0, a21 = 0, a22 = 1;
    double x1 = 0, x2 = 0;
};
} // namespace nl

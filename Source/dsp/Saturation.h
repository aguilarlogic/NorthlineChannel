#pragma once

#include "Filters.h"

namespace nl
{
// Memoryless shaping curves with closed-form first and second antiderivatives, processed
// with antiderivative anti-aliasing (ADAA; Parker, Zavalishin and Le Bivic, DAFx 2016;
// Bilbao, Esqueda, Parker and Valimaki, IEEE SPL 2017). Every curve has unity small-signal
// gain at x = 0 (except Atan's ceiling, which is pi/2).
namespace shaper
{
enum Type { Odd = 0, Even, Heavy, Crush, Tape, Tube, Transformer, numTypes };

//------------------------------------------------------------------------------
// u / sqrt(1 + u^2): soft, odd harmonics (Odd, Transformer).
struct Sig
{
    static double f (double u) noexcept { return u / std::sqrt (1.0 + u * u); }
    static double F1 (double u) noexcept { return std::sqrt (1.0 + u * u); }
    static double F2 (double u) noexcept { return 0.5 * (u * std::sqrt (1.0 + u * u) + std::asinh (u)); }
};

// Biased sigmoid, normalised to f(0) = 0 and unit slope: even and odd harmonics (Even, Tube).
struct BiasedSig
{
    double b = 0.45, sb = 0.0, norm = 1.0;

    void set (double bias) noexcept
    {
        b = bias;
        sb = Sig::f (b);
        const double d = 1.0 + b * b;
        norm = d * std::sqrt (d);
    }
    BiasedSig() { set (0.45); }
    double f (double x) const noexcept { return (Sig::f (x + b) - sb) * norm; }
    double F1 (double x) const noexcept { return (std::sqrt (1.0 + (x + b) * (x + b)) - sb * x) * norm; }
    double F2 (double x) const noexcept { const double u = x + b; return (0.5 * (u * std::sqrt (1.0 + u * u) + std::asinh (u)) - 0.5 * sb * x * x) * norm; }
};

// Cubic soft clip with a hard knee at |x| = 1.5 (Heavy).
struct Cubic
{
    static double f (double x) noexcept
    {
        const double u = x * (2.0 / 3.0);
        if (u >= 1.0) return 1.0;
        if (u <= -1.0) return -1.0;
        return 1.5 * u - 0.5 * u * u * u;
    }
    static double F1 (double x) noexcept
    {
        const double u = x * (2.0 / 3.0);
        if (u > 1.0) return x - 0.5625;
        if (u < -1.0) return -x - 0.5625;
        const double u2 = u * u;
        return 1.125 * u2 - 0.1875 * u2 * u2;
    }
    static double F2 (double x) noexcept
    {
        const double u = x * (2.0 / 3.0);
        if (u > 1.0) return 0.5 * x * x - 0.5625 * x + 0.225;
        if (u < -1.0) return -(0.5 * x * x + 0.5625 * x + 0.225);
        const double u2 = u * u;
        return u * u2 * (0.5625 - 0.05625 * u2);
    }
};

// Asymmetric soft-knee clip: +1 / -0.72 thresholds with quadratic knees (Crush). A knee that
// is not razor sharp keeps the sine / noise level spread small and the aliasing manageable.
struct Clip
{
    static constexpr double kPosT = 1.0, kPosW = 0.7, kNegT = 0.72, kNegW = 0.5;

    // Positive-side curve g(x) = x below T - w, quadratic knee, T above T + w, and its antiderivatives.
    static double g0 (double x, double T, double w) noexcept
    {
        const double d = x - (T - w);
        if (d <= 0.0) return x;
        if (d >= 2.0 * w) return T;
        return x - d * d / (4.0 * w);
    }
    static double g1 (double x, double T, double w) noexcept
    {
        const double d = x - (T - w);
        if (d <= 0.0) return 0.5 * x * x;
        if (d < 2.0 * w) return 0.5 * x * x - d * d * d / (12.0 * w);
        const double b = T + w;
        return T * x + 0.5 * b * b - 2.0 * w * w / 3.0 - T * b;
    }
    static double g2 (double x, double T, double w) noexcept
    {
        const double d = x - (T - w);
        if (d <= 0.0) return x * x * x / 6.0;
        if (d < 2.0 * w) return x * x * x / 6.0 - d * d * d * d / (48.0 * w);
        const double b = T + w;
        const double c = 0.5 * b * b - 2.0 * w * w / 3.0 - T * b;
        const double e = b * b * b / 6.0 - w * w * w / 3.0 - 0.5 * T * b * b - c * b;
        return 0.5 * T * x * x + c * x + e;
    }
    static double f (double x) noexcept { return x >= 0.0 ? g0 (x, kPosT, kPosW) : -g0 (-x, kNegT, kNegW); }
    static double F1 (double x) noexcept { return x >= 0.0 ? g1 (x, kPosT, kPosW) : g1 (-x, kNegT, kNegW); }
    static double F2 (double x) noexcept { return x >= 0.0 ? g2 (x, kPosT, kPosW) : -g2 (-x, kNegT, kNegW); }
};

// atan: soft knee with a long, gentle approach to the ceiling (Tape).
struct Atan
{
    static double f (double x) noexcept { return std::atan (x); }
    static double F1 (double x) noexcept { return x * std::atan (x) - 0.5 * std::log1p (x * x); }
    static double F2 (double x) noexcept { return 0.5 * (x * x - 1.0) * std::atan (x) + 0.5 * x - 0.5 * x * std::log1p (x * x); }
};

//------------------------------------------------------------------------------
// First- or second-order ADAA around a shaping curve `S`. Order 1 has the linear response
// (1 + z^-1) / 2 (cos(w/2): -6 dB at fs/3). Order 2 has (1 + z^-1 + z^-2) / 3, which has a
// null at fs/3, so only order 1 can be corrected to a flat passband at 1x; order 2 is used
// where the rate is high (oversampled). The droop is corrected before the shaper (see
// SatEngine.h), so the averaging acts as the matching de-emphasis for aliasing.
template <class S>
struct Adaa
{
    S s;
    int order = 2;
    double x1 = 0.0, x2 = 0.0, F1c = 0.0, F2c1 = 0.0, F2c2 = 0.0, Dprev = 0.0;

    void reset() noexcept
    {
        x1 = x2 = 0.0;
        rebias();
    }

    // Recomputes the cached antiderivative values (after the curve changed, e.g. a new bias).
    void rebias() noexcept
    {
        F1c = s.F1 (x1);
        F2c1 = s.F2 (x1);
        F2c2 = s.F2 (x2);
        Dprev = D (x1, x2, F2c1, F2c2);
    }

    inline double process (double x) noexcept
    {
        if (! (std::abs (x) < 1.0e6)) x = 0.0; // NaN / Inf / absurd values must not poison the state
        if (order == 1)
        {
            const double F = s.F1 (x), dx = x - x1;
            const double y = std::abs (dx) < kEps1 ? s.f (0.5 * (x + x1)) : (F - F1c) / dx;
            x1 = x;
            F1c = F;
            return y;
        }

        const double F2x = s.F2 (x);
        const double Dcur = D (x, x1, F2x, F2c1);
        const double d2 = x - x2;
        const double y = std::abs (d2) < kEps2 ? s.f ((x + x1 + x2) * (1.0 / 3.0)) : 2.0 * (Dcur - Dprev) / d2;
        x2 = x1;
        F2c2 = F2c1;
        x1 = x;
        F2c1 = F2x;
        Dprev = Dcur;
        return y;
    }

private:
    static constexpr double kEps1 = 1.0e-4, kEps2 = 1.0e-3;

    double D (double a, double b, double F2a, double F2b) const noexcept
    {
        const double d = a - b;
        return std::abs (d) < kEps2 ? s.F1 (0.5 * (a + b)) : (F2a - F2b) / d;
    }
};

//------------------------------------------------------------------------------
// Drive law. Drive 0 % is a clean -12 dB pre-gain (the output stage restores unity), 100 % is
// the type's maximum shaper gain. Gains are in dB and apply before the shaping curve.
constexpr double kDriveMinDb = -12.0;

// Shaper gain at 100 % drive, per type. Chosen so that the Eco (1x) alias level for a 5 kHz sine
// at -6 dBFS stays at or below -40 dB (Crush -30 dB, ROADMAP section 1): a heavier drive saturates
// harder but cannot be anti-aliased at 1x and zero latency. The numbers are measured in the "pre"
// measurement suite; HQ has much more headroom than this.
inline double driveMaxDb (int type) noexcept
{
    switch (type)
    {
        case Even:        return 10.0;
        case Heavy:       return 11.0;
        case Crush:       return 7.5;
        case Tape:        return 8.5;
        case Tube:        return 10.0;
        case Odd:
        case Transformer:
        default:          return 7.0;
    }
}

inline double driveDb (int type, float drivePercent) noexcept
{
    const double d = std::clamp ((double) drivePercent, 0.0, 100.0) * 0.01;
    return kDriveMinDb + (driveMaxDb (type) - kDriveMinDb) * d;
}
} // namespace shaper
} // namespace nl

#pragma once

#include "Filters.h"

namespace nl
{
// Memoryless shapers with closed-form antiderivatives, processed with
// first-order antiderivative anti-aliasing (ADAA; Parker, Zavalishin & Le Bivic,
// DAFx 2016). All shapers have unity small-signal gain at x = 0.
namespace shaper
{
enum Type { Odd = 0, Even, Heavy, Crush };

constexpr double kEvenBias = 0.45;
constexpr double kCrushNeg = 0.72; // asymmetric negative clip level

inline double sig (double u) { return u / std::sqrt (1.0 + u * u); }
inline double sigSlope0 (double b) { const double d = 1.0 + b * b; return 1.0 / (d * std::sqrt (d)); }

inline double f (int type, double x)
{
    switch (type)
    {
        case Odd: return sig (x);
        case Even:
        {
            static const double sb = sig (kEvenBias), norm = 1.0 / sigSlope0 (kEvenBias);
            return (sig (x + kEvenBias) - sb) * norm;
        }
        case Heavy:
        {
            const double u = x * (2.0 / 3.0);
            if (u >= 1.0) return 1.0;
            if (u <= -1.0) return -1.0;
            return 1.5 * u - 0.5 * u * u * u;
        }
        case Crush:
        default:
            return std::clamp (x, -kCrushNeg, 1.0);
    }
}

// Antiderivatives (constants of integration are irrelevant for ADAA but must be
// consistent so the function is continuous).
inline double F (int type, double x)
{
    switch (type)
    {
        case Odd: return std::sqrt (1.0 + x * x);
        case Even:
        {
            static const double sb = sig (kEvenBias), norm = 1.0 / sigSlope0 (kEvenBias);
            return (std::sqrt (1.0 + (x + kEvenBias) * (x + kEvenBias)) - sb * x) * norm;
        }
        case Heavy:
        {
            const double u = x * (2.0 / 3.0);
            double H;
            if (u > 1.0) H = u - 0.375;
            else if (u < -1.0) H = -u - 0.375;
            else { const double u2 = u * u; H = 0.75 * u2 - 0.125 * u2 * u2; }
            return 1.5 * H;
        }
        case Crush:
        default:
            if (x > 1.0) return x - 0.5;
            if (x < -kCrushNeg) return -kCrushNeg * x - 0.5 * kCrushNeg * kCrushNeg;
            return 0.5 * x * x;
    }
}

// First-order ADAA has the linear response (1 + z^-1)/2, i.e. cos(w/2): about
// -2 dB at 10 kHz and -5 dB at 15 kHz at 48 kHz. A minimum-phase one-pole
// de-emphasis (1 + rho) / (1 + rho z^-1) restores it to within -0.4 dB up to
// 16 kHz without adding latency.
constexpr double kAdaaCompRho = 0.7;

struct Adaa1
{
    double x1 = 0.0, F1 = 0.0, yPrev = 0.0;
    int type = Odd;

    void reset (int t) { type = t; x1 = 0.0; F1 = F (type, 0.0); yPrev = 0.0; }

    inline double process (double x) noexcept
    {
        const double Fx = F (type, x);
        const double dx = x - x1;
        double v;
        if (std::abs (dx) < 1.0e-6)
            v = f (type, 0.5 * (x + x1));
        else
            v = (Fx - F1) / dx;
        x1 = x;
        F1 = Fx;
        yPrev = (1.0 + kAdaaCompRho) * v - kAdaaCompRho * yPrev;
        return yPrev;
    }
};

inline double driveDb (int type, float drivePercent)
{
    const double d = std::clamp ((double) drivePercent, 0.0, 100.0) * 0.01;
    return type == Crush ? 12.0 + 36.0 * d : 24.0 * d;
}

// Output gain that keeps a -12 dBFS peak at the same level after the shaper.
inline double compensation (int type, double gIn)
{
    constexpr double ref = 0.25;
    const double pos = f (type, gIn * ref), neg = -f (type, -gIn * ref);
    const double avg = 0.5 * (pos + neg);
    return avg > 1.0e-9 ? ref / avg : 1.0;
}
} // namespace shaper
} // namespace nl

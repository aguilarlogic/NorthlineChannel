#pragma once

#include "Filters.h"
#include <algorithm>
#include <array>
#include <cmath>

// EQ band design: magnitude-matched second-order filters realised on the TPT SVF.
//
// The analog prototypes are the RBJ bell and shelves (the same ones the bilinear design
// approximated). A digital second-order section is found whose magnitude follows the analog
// response up to 0.9 * Nyquist:
//   1. Closed form (Vicanek, "Matched Second Order Digital Filters"): poles by impulse
//      invariance, numerator fitted to the analog magnitude at DC, Nyquist and one more point.
//   2. If the closed form misses the tolerance, a bounded refinement (Levenberg-Marquardt on the
//      dB error over a small frequency grid, Lawson re-weighting toward minimax), first with DC
//      and Nyquist held exact (3 free values), then with all five SVF values free.
// The result is mapped onto the SVF as (g, k, m0, m1, m2); see docs/dsp/eq.md.
//
// Everything here is a pure function of (shape, sample rate): no state, no allocation.
namespace nl::eqd
{
enum class Kind { Bell, LowShelf, HighShelf };

struct Shape
{
    Kind kind = Kind::Bell;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
};

// SVF values in double precision (output = m0*v0 + m1*v1 + m2*v2 with the TPT integrators g, k).
struct SvfParams
{
    double g = 0.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

inline SvfCoefs toCoefs (const SvfParams& p) noexcept
{
    SvfCoefs c;
    const double a1 = 1.0 / (1.0 + p.g * (p.g + p.k));
    c.g = (float) p.g;
    c.k = (float) p.k;
    c.a1 = (float) a1;
    c.a2 = (float) (p.g * a1);
    c.a3 = (float) (p.g * p.g * a1);
    c.m0 = (float) p.m0;
    c.m1 = (float) p.m1;
    c.m2 = (float) p.m2;
    return c;
}

// Magnitude response (linear) of an SVF at frequency f, from its float coefficients.
inline double svfMagnitude (const SvfCoefs& c, double f, double fs) noexcept
{
    const double w = std::tan (kPiD * std::min (f, 0.4999 * fs) / fs) / std::max ((double) c.g, 1.0e-12);
    const double x = w * w, k = (double) c.k, m0 = (double) c.m0;
    const double re = m0 * (1.0 - x) + (double) c.m2;
    const double im = w * (m0 * k + (double) c.m1);
    return std::sqrt ((re * re + im * im) / ((1.0 - x) * (1.0 - x) + k * k * x));
}

//==============================================================================
// Analog prototype of one band.
struct Analog
{
    Kind kind = Kind::Bell;
    double A = 1.0;        // 10^(gain/40)
    double iq2 = 1.0;      // 1/Q^2
    double fc = 1000.0;
    double fp = 1000.0;    // pole frequency of the analog denominator
    double zeta = 0.5;     // pole damping
    double g0 = 1.0;       // |H(0)|

    void init (const Shape& s) noexcept
    {
        kind = s.kind;
        A = std::pow (10.0, s.gainDb / 40.0);
        const double q = std::max (s.q, 0.05);
        iq2 = 1.0 / (q * q);
        fc = s.freq;
        switch (kind)
        {
            case Kind::Bell:      fp = fc;                  zeta = 1.0 / (2.0 * A * q); g0 = 1.0;   break;
            case Kind::LowShelf:  fp = fc / std::sqrt (A);  zeta = 1.0 / (2.0 * q);     g0 = A * A; break;
            case Kind::HighShelf: fp = fc * std::sqrt (A);  zeta = 1.0 / (2.0 * q);     g0 = 1.0;   break;
        }
    }

    // |H|^2 at W2 = (f / fc)^2.
    double magSq (double W2) const noexcept
    {
        switch (kind)
        {
            case Kind::Bell:
            {
                const double u = 1.0 - W2, e = W2 * iq2;
                return (u * u + A * A * e) / (u * u + e / (A * A));
            }
            case Kind::LowShelf:
            {
                const double e = W2 * A * iq2, a = A - W2, b = 1.0 - A * W2;
                return A * A * (a * a + e) / (b * b + e);
            }
            default:
            {
                const double e = W2 * A * iq2, a = A - W2, b = 1.0 - A * W2;
                return A * A * (b * b + e) / (a * a + e);
            }
        }
    }

    double db (double f) const noexcept { const double r = f / fc; return 10.0 * std::log10 (magSq (r * r)); }
};

//==============================================================================
// Frequency grid shared by all designs at one sample rate.
struct Context
{
    static constexpr int kLow = 12, kHigh = 22, kAbs = kLow + kHigh, kExtra = 3, kRel = 13, kMax = kAbs + kExtra + kRel;

    double fs = 48000.0;
    double absFreq[kAbs + kExtra] {}, absT[kAbs + kExtra] {}; // t = tan^2(w/2)
    double absW[kAbs + kExtra] {};                            // fit weight

    // Log-spaced from 25 Hz to 0.9 Nyquist, denser above 3 kHz where the digital response departs
    // from the analog one, plus three light points between 0.93 and 0.99 Nyquist.
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        const double fmax = 0.9 * 0.5 * fs, fmin = 25.0, fmid = std::min (3000.0, 0.5 * fmax);
        for (int i = 0; i < kLow; ++i)
            absFreq[i] = fmin * std::pow (fmid / fmin, (double) i / kLow);
        for (int i = 0; i < kHigh; ++i)
            absFreq[kLow + i] = fmid * std::pow (fmax / fmid, (double) i / (kHigh - 1));
        for (int i = 0; i < kAbs; ++i) absW[i] = 1.0;
        for (int i = 0; i < kExtra; ++i)
        {
            absFreq[kAbs + i] = 0.5 * fs * (0.93 + 0.03 * i);
            absW[kAbs + i] = 0.25;
        }
        for (int i = 0; i < kAbs + kExtra; ++i)
        {
            const double tn = std::tan (kPiD * absFreq[i] / fs);
            absT[i] = tn * tn;
        }
    }
};
//==============================================================================
namespace detail
{
constexpr double kC10 = 4.3429448190325175; // 10 / ln 10

struct Fit
{
    int n = 0;
    double t[Context::kMax] {}, target[Context::kMax] {}, weight[Context::kMax] {};
};

// Parameter vector theta = { ln g, ln k, m0, p, m2 } with p = m0*k + m1.
// |H|^2 = ((m0 (1-x) + m2)^2 + x p^2) / ((1-x)^2 + k^2 x),  x = tan^2(w/2) / g^2.
struct Theta
{
    double v[5] {};
};

inline double residuals (const Fit& f, const Theta& th, const double* w, double* r) noexcept
{
    const double g = std::exp (th.v[0]), k = std::exp (th.v[1]);
    const double m0 = th.v[2], p = th.v[3], m2 = th.v[4];
    const double ig2 = 1.0 / (g * g);
    double cost = 0.0;
    for (int i = 0; i < f.n; ++i)
    {
        const double x = f.t[i] * ig2, omx = 1.0 - x;
        const double ax = m0 * omx + m2;
        const double nn = ax * ax + x * p * p + 1.0e-30, dd = omx * omx + k * k * x + 1.0e-30;
        r[i] = kC10 * std::log (nn / dd) - f.target[i];
        cost += w[i] * r[i] * r[i];
    }
    return cost;
}

// Gauss-Newton normal equations for the free parameters (mask bit i = parameter i is free).
inline void normalEquations (const Fit& f, const Theta& th, const double* w, const double* r, int mask,
                             double JtJ[5][5], double Jtr[5]) noexcept
{
    const double g = std::exp (th.v[0]), k = std::exp (th.v[1]);
    const double m0 = th.v[2], p = th.v[3], m2 = th.v[4];
    const double ig2 = 1.0 / (g * g);
    for (int a = 0; a < 5; ++a)
    {
        Jtr[a] = 0.0;
        for (int b = 0; b < 5; ++b) JtJ[a][b] = 0.0;
    }
    for (int i = 0; i < f.n; ++i)
    {
        const double x = f.t[i] * ig2, omx = 1.0 - x;
        const double ax = m0 * omx + m2;
        const double nn = ax * ax + x * p * p + 1.0e-30, dd = omx * omx + k * k * x + 1.0e-30;
        const double inn = 1.0 / nn, idd = 1.0 / dd;
        double j[5];
        j[0] = kC10 * (-2.0 * x * (-2.0 * ax * m0 + p * p) * inn + 2.0 * x * (-2.0 * omx + k * k) * idd);
        j[1] = kC10 * (-2.0 * k * k * x * idd);
        j[2] = kC10 * (2.0 * ax * omx * inn);
        j[3] = kC10 * (2.0 * x * p * inn);
        j[4] = kC10 * (2.0 * ax * inn);
        const double wi = w[i];
        for (int a = 0; a < 5; ++a)
        {
            if (! (mask & (1 << a))) continue;
            Jtr[a] += wi * j[a] * r[i];
            for (int b = 0; b < 5; ++b)
                if (mask & (1 << b)) JtJ[a][b] += wi * j[a] * j[b];
        }
    }
}

// Solves the (damped) normal equations on the free parameters. Returns false if singular.
inline bool solveStep (const double JtJ[5][5], const double Jtr[5], int mask, double lambda, double dx[5]) noexcept
{
    int idx[5], n = 0;
    for (int a = 0; a < 5; ++a)
        if (mask & (1 << a)) idx[n++] = a;
    double M[5][6];
    for (int a = 0; a < n; ++a)
    {
        for (int b = 0; b < n; ++b) M[a][b] = JtJ[idx[a]][idx[b]];
        M[a][a] += lambda * (JtJ[idx[a]][idx[a]] + 1.0e-12);
        M[a][n] = -Jtr[idx[a]];
    }
    for (int c = 0; c < n; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs (M[r][c]) > std::abs (M[piv][c])) piv = r;
        if (std::abs (M[piv][c]) < 1.0e-300) return false;
        if (piv != c)
            for (int b = c; b <= n; ++b) std::swap (M[piv][b], M[c][b]);
        for (int r = c + 1; r < n; ++r)
        {
            const double fct = M[r][c] / M[c][c];
            for (int b = c; b <= n; ++b) M[r][b] -= fct * M[c][b];
        }
    }
    double sol[5];
    for (int c = n - 1; c >= 0; --c)
    {
        double s = M[c][n];
        for (int b = c + 1; b < n; ++b) s -= M[c][b] * sol[b];
        sol[c] = s / M[c][c];
    }
    for (int a = 0; a < 5; ++a) dx[a] = 0.0;
    for (int a = 0; a < n; ++a) dx[idx[a]] = sol[a];
    return true;
}

inline double maxAbs (const double* r, int n) noexcept
{
    double m = 0.0;
    for (int i = 0; i < n; ++i) m = std::max (m, std::abs (r[i]));
    return m;
}

// Levenberg-Marquardt with Lawson re-weighting. Keeps the iterate with the smallest max error.
// `outer` re-weighting rounds of `inner` LM steps each.
inline double refine (const Fit& f, Theta& th, int mask, int outer, int inner) noexcept
{
    double w[Context::kMax], r[Context::kMax];
    for (int i = 0; i < f.n; ++i) w[i] = f.weight[i];
    double cost = residuals (f, th, w, r);
    double bestMax = maxAbs (r, f.n);
    Theta best = th;

    for (int o = 0; o < outer; ++o)
    {
        double lambda = 1.0e-3;
        for (int it = 0; it < inner; ++it)
        {
            double JtJ[5][5], Jtr[5];
            normalEquations (f, th, w, r, mask, JtJ, Jtr);
            bool ok = false;
            for (int tries = 0; tries < 6; ++tries)
            {
                double dx[5];
                if (solveStep (JtJ, Jtr, mask, lambda, dx))
                {
                    Theta cand = th;
                    for (int a = 0; a < 5; ++a) cand.v[a] += dx[a];
                    cand.v[0] = std::clamp (cand.v[0], -12.0, 6.0);
                    cand.v[1] = std::clamp (cand.v[1], -8.0, 6.0);
                    double rc[Context::kMax];
                    const double cc = residuals (f, cand, w, rc);
                    if (std::isfinite (cc) && cc < cost)
                    {
                        th = cand;
                        cost = cc;
                        for (int i = 0; i < f.n; ++i) r[i] = rc[i];
                        lambda = std::max (lambda * 0.3, 1.0e-9);
                        ok = true;
                        break;
                    }
                }
                lambda *= 10.0;
            }
            if (! ok) break;
        }
        const double m = maxAbs (r, f.n);
        if (m < bestMax) { bestMax = m; best = th; }
        // Lawson update: emphasise the points with the largest error.
        double s = 0.0;
        for (int i = 0; i < f.n; ++i) { w[i] = w[i] * std::abs (r[i]) + 1.0e-6; s += w[i]; }
        const double norm = (double) f.n / s;
        for (int i = 0; i < f.n; ++i) w[i] *= norm;
        cost = 0.0;
        for (int i = 0; i < f.n; ++i) cost += w[i] * r[i] * r[i];
    }
    th = best;
    return bestMax;
}
} // namespace detail

//==============================================================================
struct Options
{
    double acceptClosed = 0.12; // dB: closed form is kept if its grid error is below this
    double accept3 = 0.22;      // dB: 3-value refinement is kept if below this
    int outer3 = 3, inner3 = 4; // refinement effort
    int outer5 = 4, inner5 = 5;
    double polishAbove = 0.25;   // dB: extra rounds if the five-value fit is still above this
    int outerPolish = 16;
};

struct Result
{
    SvfParams p;
    double maxErrDb = 0.0; // on the solver grid
    int stage = 0;         // 0 identity, 1 closed form, 2 three-value refinement, 3 five-value refinement, 4 fallback
    bool refined = false;  // the refinement ran (stages 2 to 4 were attempted)
};

// Matched-z poles of the analog denominator mapped to the SVF integrator gain g and damping k.
inline void matchedPoles (const Analog& an, double fs, double& g, double& k) noexcept
{
    const double th = 2.0 * kPiD * an.fp / fs;
    const double z = an.zeta;
    double u, v, omA2;
    if (z < 1.0)
    {
        const double e = std::exp (-z * th), s = std::sin (0.5 * th * std::sqrt (1.0 - z * z));
        u = (1.0 - e) * (1.0 - e) + 4.0 * e * s * s;
        v = (1.0 + e) * (1.0 + e) - 4.0 * e * s * s;
        omA2 = -std::expm1 (-2.0 * z * th); // 1 - a2
    }
    else
    {
        const double r = std::sqrt (z * z - 1.0);
        const double e1 = -std::expm1 (-(z - r) * th), e2 = -std::expm1 (-(z + r) * th); // 1 - p1, 1 - p2
        const double p1 = 1.0 - e1, p2 = 1.0 - e2;
        u = e1 * e2;
        v = (1.0 + p1) * (1.0 + p2);
        omA2 = 1.0 - p1 * p2;
    }
    g = std::sqrt (u / v);
    k = 2.0 * omA2 / (g * v);
}

// Poles of the bilinear transform with the analog pole frequency prewarped (the previous design).
inline void bilinearPoles (const Analog& an, double fs, double& g, double& k) noexcept
{
    g = std::tan (kPiD * std::min (an.fp, 0.495 * fs) / fs);
    k = 2.0 * an.zeta;
}

// Fits the middle SVF value p (p = m0*k + m1) with m0, m2 and the poles given: exactly at one
// anchor frequency if that has a real solution, otherwise as a least-squares estimate over the
// grid. p is kept away from zero, where the response is flat in p.
inline double fitP (const Analog& an, const detail::Fit& fit, double fs, double g, double k, double m0, double m2, double fAnchor) noexcept
{
    const double tn = std::tan (kPiD * fAnchor / fs);
    const double x = tn * tn / (g * g), omx = 1.0 - x;
    const double r = fAnchor / an.fc;
    const double hs = an.magSq (r * r);
    const double ax = m0 * omx + m2;
    const double p2 = (hs * (omx * omx + k * k * x) - ax * ax) / std::max (x, 1.0e-12);
    const double floorP = 0.02 * std::max (k, 0.1);
    if (p2 > floorP * floorP) return std::sqrt (p2);

    double num = 0.0, den = 0.0;
    for (int i = 0; i < fit.n; ++i)
    {
        const double xi = fit.t[i] / (g * g), omi = 1.0 - xi, axi = m0 * omi + m2;
        const double hsi = std::exp (fit.target[i] / detail::kC10);
        const double pi2 = (hsi * (omi * omi + k * k * xi) - axi * axi) / std::max (xi, 1.0e-12);
        const double wgt = fit.weight[i] * xi / (1.0 + xi * xi);
        num += wgt * std::max (pi2, 0.0);
        den += wgt;
    }
    return std::max (std::sqrt (num / std::max (den, 1.0e-12)), floorP);
}

// Fit data: the absolute grid plus points around fc scaled by the band's bandwidth.
inline void buildFit (const Context& ctx, const Analog& an, detail::Fit& fit) noexcept
{
    const double nyq = 0.5 * ctx.fs;
    double rr = std::sqrt (1.0 + 0.25 * an.iq2) + 0.5 * std::sqrt (an.iq2); // half-gain frequency ratio
    rr = std::min (rr, 8.0);
    int n = 0;
    for (int i = 0; i < Context::kAbs + Context::kExtra; ++i)
    {
        fit.t[n] = ctx.absT[i];
        const double r = ctx.absFreq[i] / an.fc;
        fit.target[n] = detail::kC10 * std::log (an.magSq (r * r));
        fit.weight[n] = ctx.absW[i];
        ++n;
    }
    const double relFactor[Context::kRel] { 1.0, std::pow (rr, 0.25), std::pow (rr, -0.25), std::pow (rr, 0.5), std::pow (rr, -0.5),
                                            std::pow (rr, 0.75), std::pow (rr, -0.75), rr, 1.0 / rr, std::pow (rr, 1.5), std::pow (rr, -1.5),
                                            rr * rr, 1.0 / (rr * rr) };
    for (int i = 0; i < Context::kRel; ++i)
    {
        const double f = std::clamp (an.fc * relFactor[i], 20.0, 0.9 * nyq);
        const double tn = std::tan (kPiD * f / ctx.fs);
        const double r = f / an.fc;
        fit.t[n] = tn * tn;
        fit.target[n] = detail::kC10 * std::log (an.magSq (r * r));
        fit.weight[n] = i == 0 ? 1.5 : 1.0;
        ++n;
    }
    fit.n = n;
}

// With allowRefine false and the closed form above the tolerance, the closed form is returned and
// wantsRefine is set: the caller retries on a later control tick (the refinement is budgeted per tick).
inline Result designBandBudgeted (const Context& ctx, const Shape& s, bool allowRefine, bool& wantsRefine, const Options& opt = {}) noexcept
{
    wantsRefine = false;
    Result res;
    const double fs = ctx.fs;
    Shape sh = s;
    sh.freq = std::clamp (sh.freq, 5.0, 0.49 * fs);
    sh.q = std::clamp (sh.q, 0.1, 40.0);
    sh.gainDb = std::clamp (sh.gainDb, -40.0, 40.0);

    Analog an;
    an.init (sh);

    double g, k;
    matchedPoles (an, fs, g, k);

    if (std::abs (sh.gainDb) < 1.0e-6)
    {
        res.p = { g, k, 1.0, 0.0, 0.0 };
        res.stage = 0;
        return res;
    }

    detail::Fit fit;
    buildFit (ctx, an, fit);
    const double nyq = 0.5 * fs;
    const double G0 = an.g0;

    // Analog gain at Nyquist.
    const double rn = nyq / an.fc;
    const double G1 = std::sqrt (an.magSq (rn * rn));
    const double m0 = G1, m2 = G0 - G1;
    const double anchor = an.fc <= 0.4 * fs ? an.fc : 0.2 * fs;

    auto thetaFrom = [&] (double gg, double kk)
    {
        detail::Theta th;
        th.v[0] = std::log (gg);
        th.v[1] = std::log (kk);
        th.v[2] = m0;
        th.v[3] = fitP (an, fit, fs, gg, kk, m0, m2, anchor);
        th.v[4] = m2;
        return th;
    };
    auto errOf = [&] (const detail::Theta& th)
    {
        double r[Context::kMax], w[Context::kMax];
        for (int i = 0; i < fit.n; ++i) w[i] = 1.0;
        detail::residuals (fit, th, w, r);
        double m = 0.0;
        for (int i = 0; i < fit.n; ++i)
            if (fit.weight[i] >= 0.5) m = std::max (m, std::abs (r[i]));
        return m;
    };

    // Stage 1: closed forms.
    detail::Theta cands[2];
    cands[0] = thetaFrom (g, k);
    double gb, kb;
    bilinearPoles (an, fs, gb, kb);
    cands[1] = thetaFrom (gb, kb);
    double e[2] = { errOf (cands[0]), errOf (cands[1]) };
    int bi = e[1] < e[0] ? 1 : 0;
    detail::Theta best = cands[bi];
    double bestErr = e[bi];
    int stage = 1;

    const bool needRefine = ! (bestErr <= opt.acceptClosed);
    wantsRefine = needRefine && ! allowRefine;
    if (needRefine && allowRefine)
    {
        res.refined = true;
        // Stage 2: DC and Nyquist exact (free: ln g, ln k, p).
        for (int c = 0; c < 2; ++c)
        {
            detail::Theta th = cands[c];
            detail::refine (fit, th, 0b01011, opt.outer3, opt.inner3);
            const double ee = errOf (th);
            if (ee < bestErr) { bestErr = ee; best = th; stage = 2; }
        }
        if (! (bestErr <= opt.accept3))
        {
            // Stage 3: all five values free, from the best and from the other closed form.
            detail::Theta starts[2] = { best, cands[1 - bi] };
            for (int c = 0; c < 2; ++c)
            {
                detail::Theta th = starts[c];
                detail::refine (fit, th, 0b11111, opt.outer5, opt.inner5);
                const double ee = errOf (th);
                if (ee < bestErr) { bestErr = ee; best = th; stage = 3; }
            }
            if (bestErr > opt.polishAbove)
            {
                detail::Theta th = best; // rare: extra rounds for the hardest corners
                detail::refine (fit, th, 0b11111, opt.outerPolish, opt.inner5);
                const double ee = errOf (th);
                if (ee < bestErr) { bestErr = ee; best = th; }
            }
        }
    }

    if (! std::isfinite (bestErr))
    {
        // Safety net: bell/shelf poles by the plain bilinear transform, numerator from stage 1.
        best = cands[1];
        stage = 4;
        bestErr = errOf (best);
    }

    res.p.g = std::exp (best.v[0]);
    res.p.k = std::exp (best.v[1]);
    res.p.m0 = best.v[2];
    res.p.m1 = best.v[3] - best.v[2] * res.p.k;
    res.p.m2 = best.v[4];
    res.maxErrDb = bestErr;
    res.stage = stage;
    return res;
}

inline Result designBand (const Context& ctx, const Shape& s, const Options& opt = {}) noexcept
{
    bool wants = false;
    return designBandBudgeted (ctx, s, true, wants, opt);
}
} // namespace nl::eqd





#pragma once

#include "Common.h"
#include <complex>

namespace nl
{
//==============================================================================
// Trapezoidal (TPT) state-variable filter after A. Simper, "Linear trapezoidal
// integrated SVF" (Cytomic, 2013/2016). Output = m0*v0 + m1*v1 + m2*v2.
// Well behaved under fast coefficient modulation, which matters for automation
// and for the dynamic bands.
enum class SvfType { LowPass, HighPass, BandPass, Bell, LowShelf, HighShelf };

struct SvfCoefs
{
    float g = 0.0f, k = 1.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;

    // Magnitude response at frequency f (Hz) for display.
    float magnitude (double f, double fs) const
    {
        const double w = std::tan (kPiD * std::min (f, 0.4999 * fs) / fs) / std::max ((double) g, 1.0e-9);
        const std::complex<double> s (0.0, w);
        const auto h = (double) m0 + ((double) m1 * s + (double) m2) / (s * s + (double) k * s + 1.0);
        return (float) std::abs (h);
    }
};

inline SvfCoefs makeSvf (SvfType type, double fs, float freq, float q, float gainDb = 0.0f)
{
    SvfCoefs c;
    const double f = std::clamp ((double) freq, 5.0, 0.47 * fs);
    double g = std::tan (kPiD * f / fs);
    double k = 1.0 / std::max (0.025, (double) q);
    const double A = std::pow (10.0, gainDb / 40.0);

    switch (type)
    {
        case SvfType::LowPass:   c.m0 = 0; c.m1 = 0; c.m2 = 1; break;
        case SvfType::HighPass:  c.m0 = 1; c.m1 = (float) -k; c.m2 = -1; break;
        case SvfType::BandPass:  c.m0 = 0; c.m1 = (float) k; c.m2 = 0; break; // unity gain at centre
        case SvfType::Bell:
            k = 1.0 / (std::max (0.025, (double) q) * A);
            c.m0 = 1; c.m1 = (float) (k * (A * A - 1.0)); c.m2 = 0;
            break;
        case SvfType::LowShelf:
            g /= std::sqrt (A);
            c.m0 = 1; c.m1 = (float) (k * (A - 1.0)); c.m2 = (float) (A * A - 1.0);
            break;
        case SvfType::HighShelf:
            g *= std::sqrt (A);
            c.m0 = (float) (A * A); c.m1 = (float) (k * (1.0 - A) * A); c.m2 = (float) (1.0 - A * A);
            break;
    }

    const double a1 = 1.0 / (1.0 + g * (g + k));
    c.g = (float) g;
    c.k = (float) k;
    c.a1 = (float) a1;
    c.a2 = (float) (g * a1);
    c.a3 = (float) (g * g * a1);
    return c;
}

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // Returns v1 (band) and v2 (low); v0 is the input.
    inline void tick (float v0, const SvfCoefs& c, float& v1, float& v2) noexcept
    {
        const float v3 = v0 - ic2;
        v1 = c.a1 * ic1 + c.a2 * v3;
        v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
    }

    inline float process (float v0, const SvfCoefs& c) noexcept
    {
        float v1, v2;
        tick (v0, c, v1, v2);
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};

//==============================================================================
// One-pole TPT low-pass / high-pass.
struct OnePole
{
    float G = 0.0f, s = 0.0f;

    void setCutoff (double fs, float freq) noexcept
    {
        const double g = std::tan (kPiD * std::clamp ((double) freq, 1.0, 0.47 * fs) / fs);
        G = (float) (g / (1.0 + g));
    }
    void reset() noexcept { s = 0.0f; }
    inline float lowpass (float x) noexcept
    {
        const float v = (x - s) * G;
        const float lp = v + s;
        s = lp + v;
        return lp;
    }
    inline float highpass (float x) noexcept { return x - lowpass (x); }

    float magnitudeLP (double f, double fs) const
    {
        const double g = G / std::max (1.0e-9, 1.0 - (double) G);
        const double w = std::tan (kPiD * std::min (f, 0.4999 * fs) / fs) / g;
        return (float) (1.0 / std::sqrt (1.0 + w * w));
    }
};

//==============================================================================
// High-pass or low-pass with 6/12/18/24 dB/oct slopes. Butterworth by default
// (RES = 0.707); RES scales the Q of the final section only. Slope changes
// cross-fade between the old and new structures to avoid clicks.
class SlopeFilter
{
public:
    enum Slope { s6 = 0, s12, s18, s24 };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        fade.prepare (fs, 15.0f);
        freq.prepare (fs, 40.0f);
        reset();
    }

    void reset()
    {
        for (auto& set : sets)
            for (auto& ch : set.ch) ch = {};
        fade.snap (false);
        freq.snap();
        fading = false;
        pendingSlope = -1;
        dirty = true;
    }

    void setParams (bool isHighPass, float frequencyHz, int slope, float res)
    {
        highPass = isHighPass;
        freq.setTarget (std::log (std::max (5.0f, frequencyHz)));
        if (std::abs (res - resonance) > 1.0e-4f) { resonance = res; dirty = true; }
        if (slope != sets[active].slope)
        {
            if (! fading)
            {
                const int other = 1 - active;
                sets[other].slope = slope;
                for (auto& ch : sets[other].ch) ch = {};
                fadeFrom = active;
                active = other;
                fading = true;
                fade.value = 0.0f;
                fade.target = 1.0f;
                fadeValueForChannel = 0.0f;
                dirty = true;
            }
            else
            {
                pendingSlope = slope; // applied after the current fade
            }
        }
    }

    // Called once per control block.
    void updateControl()
    {
        if (freq.tick() || dirty)
        {
            computeCoefs (sets[0]);
            computeCoefs (sets[1]);
            dirty = false;
        }
    }

    inline float processSample (int ch, float x) noexcept
    {
        if (! fading)
            return run (sets[active], ch, x);

        const float a = run (sets[fadeFrom], ch, x);
        const float b = run (sets[active], ch, x);
        return a + (b - a) * fadeValueForChannel;
    }

    // Advances the slope cross-fade once per sample frame (call after both channels).
    inline void advance() noexcept
    {
        if (! fading) return;
        fadeValueForChannel = fade.next();
        if (fade.value >= 1.0f)
        {
            fading = false;
            if (pendingSlope >= 0)
            {
                const int p = pendingSlope;
                pendingSlope = -1;
                setParams (highPass, std::exp (freq.target), p, resonance);
            }
        }
    }

    float magnitude (double f) const
    {
        const auto& set = sets[active];
        float m = 1.0f;
        if (set.slope == s6 || set.slope == s18)
        {
            const float lp = set.onePole.magnitudeLP (f, fs);
            m *= highPass ? std::sqrt (std::max (0.0f, 1.0f - lp * lp)) : lp;
        }
        if (set.slope != s6) m *= set.svfA.magnitude (f, fs);
        if (set.slope == s24) m *= set.svfB.magnitude (f, fs);
        return m;
    }

private:
    struct ChannelState
    {
        OnePole p;
        SvfState a, b;
    };

    struct Set
    {
        int slope = s12;
        OnePole onePole; // coefficient holder
        SvfCoefs svfA, svfB;
        ChannelState ch[2];
    };

    void computeCoefs (Set& set)
    {
        const float f = std::exp (freq.current);
        const auto type = highPass ? SvfType::HighPass : SvfType::LowPass;
        const float resScale = resonance / 0.70710678f;
        set.onePole.setCutoff (fs, f);
        switch (set.slope)
        {
            case s6: break;
            case s12: set.svfA = makeSvf (type, fs, f, resonance); break;
            case s18: set.svfA = makeSvf (type, fs, f, 1.0f * resScale); break;
            case s24:
                set.svfA = makeSvf (type, fs, f, 0.54119610f);
                set.svfB = makeSvf (type, fs, f, 1.30656296f * resScale);
                break;
            default: break;
        }
        for (auto& ch : set.ch) ch.p.G = set.onePole.G;
    }

    inline float run (Set& set, int ch, float x) noexcept
    {
        auto& st = set.ch[ch];
        switch (set.slope)
        {
            case s6:  return highPass ? st.p.highpass (x) : st.p.lowpass (x);
            case s12: return st.a.process (x, set.svfA);
            case s18:
            {
                const float y = highPass ? st.p.highpass (x) : st.p.lowpass (x);
                return st.a.process (y, set.svfA);
            }
            case s24: return st.b.process (st.a.process (x, set.svfA), set.svfB);
            default:  return x;
        }
    }

    double fs = 48000.0;
    Set sets[2];
    int active = 0, fadeFrom = 1, pendingSlope = -1;
    bool fading = false, highPass = true, dirty = true;
    float resonance = 0.70710678f, fadeValueForChannel = 0.0f;
    Fade fade;
    Smoothed freq;
};

//==============================================================================
// Direct-form-II-transposed biquad in double precision (metering filters).
struct BiquadD
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
} // namespace nl

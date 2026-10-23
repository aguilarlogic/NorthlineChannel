#pragma once

#include "TestUtil.h"
#include "dsp/EqModule.h"
#include <complex>

// Helpers shared by the EQ acceptance tests (tools/tests/TestEq.cpp) and the EQ measure suite
// (tools/measure/MeasureEq.cpp): analog prototypes, stimuli, a block-split runner with
// sample-accurate events, the splatter metric and small measurement helpers.

namespace eqtest
{
using namespace nl;

using Cplx = std::complex<double>;

//==============================================================================
// Independent analog prototypes (RBJ bell and shelves), complex arithmetic.
inline Cplx analogH (eqd::Kind kind, double gainDb, double q, double fc, double f)
{
    const double A = std::pow (10.0, gainDb / 40.0), r = std::sqrt (A);
    const Cplx s (0.0, f / fc);
    switch (kind)
    {
        case eqd::Kind::Bell:     return (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
        case eqd::Kind::LowShelf: return A * (s * s + s * (r / q) + A) / (A * s * s + s * (r / q) + 1.0);
        default:                  return A * (A * s * s + s * (r / q) + 1.0) / (s * s + s * (r / q) + A);
    }
}

inline double analogDb (eqd::Kind kind, double gainDb, double q, double fc, double f) { return 20.0 * std::log10 (std::abs (analogH (kind, gainDb, q, fc, f))); }

//==============================================================================
inline EqParams defaultEq() { return readEqParams (ParamSnapshot::defaults()); }

struct Event
{
    int sample;
    EqParams p;
};

inline std::vector<float> sineVec (double fs, double f, float amp, int n)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * kPiD * f * i / fs);
    return v;
}

inline std::vector<float> noiseVec (int n, float amp, int seed)
{
    juce::Random rng (seed);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = amp * (rng.nextFloat() * 2.0f - 1.0f);
    return v;
}

// Runs the module over l / r in place in host blocks of `block`. Events are sample-accurate: the
// host block is split at the event position, like a VST3 host delivering parameter changes with a
// sample offset. (Changes applied only at block starts cannot be block-size independent for any
// module: a block of 37 or 511 samples would see them up to 36 or 510 samples early.)
inline void runEq (EqModule& m, std::vector<float>& l, std::vector<float>& r, int block, const std::vector<Event>& ev = {},
            const std::vector<float>* keyL = nullptr, const std::vector<float>* keyR = nullptr)
{
    const int n = (int) l.size();
    size_t next = 0;
    for (int pos = 0; pos < n; pos += block)
    {
        const int end = std::min(pos + block, n);
        int cur = pos;
        while (cur < end)
        {
            while (next < ev.size() && ev[next].sample <= cur) m.setParams (ev[next++].p);
            const int stop = next < ev.size() ? std::min (end, ev[next].sample) : end;
            const int len = stop - cur;
            float* io[2] { l.data() + cur, r.data() + cur };
            const float* k[2] { keyL != nullptr ? keyL->data() + cur : nullptr, keyR != nullptr ? keyR->data() + cur : nullptr };
            m.process (io, 2, len, keyL != nullptr ? k : nullptr);
            cur = stop;
        }
    }
}


inline double rmsOf (const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (s / std::max (1, to - from));
}

// Steady-state gain in dB of the module at one frequency on one channel.
inline double measuredGainDb (double fs, const EqParams& p, double f, int channel = 0, float ampDb = -20.0f)
{
    EqModule m;
    m.prepare (fs);
    m.setParams (p);
    const int n = (int) (fs * (f < 200.0 ? 1.2 : 0.6));
    const float amp = juce::Decibels::decibelsToGain (ampDb);
    auto l = sineVec (fs, f, amp, n), r = l;
    runEq (m, l, r, 256);
    const auto& y = channel == 0 ? l : r;
    return 20.0 * std::log10 (rmsOf (y, n / 2, n) / (amp / std::sqrt (2.0)));
}

//==============================================================================
inline void fft (std::vector<Cplx>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const Cplx wl = std::polar (1.0, -2.0 * kPiD / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            Cplx w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const Cplx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Splatter: spectral power of a 50 ms Hann window above 4 * f0 relative to the power below it (dB).
// (The window resolves only 20 Hz, so the low side is not usable as a measure for low tones.)
// A smooth gain or filter change leaves it far below -60 dB; a hard switch does not.
inline double splatterDb (const std::vector<float>& x, int start, double fs, double f0)
{
    const int len = (int) (0.05 * fs), nfft = 8192;
    std::vector<Cplx> a ((size_t) nfft, Cplx (0.0, 0.0));
    for (int i = 0; i < len && start + i < (int) x.size(); ++i)
        a[(size_t) i] = (double) x[(size_t) (start + i)] * (0.5 - 0.5 * std::cos (2.0 * kPiD * i / (len - 1)));
    fft (a);
    double inside = 0.0, outside = 0.0;
    for (int k = 1; k < nfft / 2; ++k)
    {
        const double f = (double) k * fs / nfft, e = std::norm (a[(size_t) k]);
        (f <= f0 * 4.0 ? inside : outside) += e;
    }
    return 10.0 * std::log10 ((outside + 1.0e-30) / (inside + 1.0e-30));
}

inline double maxStep (const std::vector<float>& x, int from, int to)
{
    double m = 0.0;
    for (int i = std::max (from, 1); i < to; ++i) m = std::max (m, (double) std::abs (x[(size_t) i] - x[(size_t) i - 1]));
    return m;
}

//==============================================================================
// level 0: continuous parameters only; 1: + band types, targets, character; 2: + dynamic bands;
// 3: + external key and band solo.
inline EqParams randomEq (juce::Random& rng, int level)
{
    EqParams p;
    p.character = level >= 1 ? rng.nextInt (2) : 0;
    p.solo = level >= 3 && rng.nextFloat() < 0.4f ? rng.nextInt (5) : 0;
    for (auto& b : p.band)
    {
        b.on = rng.nextFloat() > 0.15f;
        b.type = level >= 1 ? rng.nextInt (3) : 2;
        b.target = level >= 1 ? rng.nextInt (3) : 0;
        b.freq = 20.0f * std::pow (1000.0f, rng.nextFloat());
        b.gain = rng.nextFloat() < 0.15f ? 0.0f : rng.nextFloat() * 36.0f - 18.0f;
        b.q = 0.2f * std::pow (50.0f, rng.nextFloat());
        b.dyn = level >= 2 && rng.nextBool();
        b.dynThresh = -60.0f + 60.0f * rng.nextFloat();
        b.dynRange = rng.nextFloat() * 36.0f - 18.0f;
        b.dynSpeed = rng.nextInt (3);
        b.dynExt = level >= 3 && rng.nextBool();
    }
    return p;
}

} // namespace eqtest

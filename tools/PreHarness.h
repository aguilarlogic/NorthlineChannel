#pragma once

#include "TestUtil.h"
#include "dsp/PreModule.h"
#include <complex>
#include <memory>
#include <vector>

// Helpers shared by the PRE tests (tools/tests/TestPre.cpp) and the PRE measurement suite
// (tools/measure/MeasurePre.cpp). They drive nl::PreModule directly with a stereo signal
// (left = right) and return channel 0.
namespace tool::pre
{
using nl::PreModule;
using nl::PreParams;

constexpr double kOpLevel = -18.0; // Operating Level, dBFS RMS (default)

// A PRE configuration with only the saturation section on (filters and Weight off).
inline PreParams satParams (int type, float drivePct, bool hq = false, bool autoGain = true)
{
    PreParams p;
    p.satOn = true;
    p.satType = type;
    p.drive = drivePct;
    p.satAutoGain = autoGain;
    p.opLevel = (float) kOpLevel;
    p.hq = hq;
    return p;
}

//==============================================================================
// Stimuli
inline std::vector<float> sineVec (double fs, double freq, double peak, size_t n, double phase = 0.0)
{
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i) x[i] = (float) (peak * std::sin (2.0 * nl::kPiD * freq * (double) i / fs + phase));
    return x;
}

// Sine with the given RMS level in dBFS.
inline std::vector<float> sineRmsVec (double fs, double freq, double rmsDb, size_t n)
{
    return sineVec (fs, freq, std::sqrt (2.0) * std::pow (10.0, rmsDb / 20.0), n);
}

// Pink noise at the given RMS level in dBFS (deterministic seed).
inline std::vector<float> pinkVec (size_t n, double rmsDb, int seed = 1)
{
    const auto b = pinkNoise ((int) n, (float) rmsDb, seed);
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + n);
}

// Bin-exact frequency nearest to f for an N-point analysis at fs.
inline double binExact (double f, double fs, int N) { return std::round (f * N / fs) * fs / N; }

//==============================================================================
// Rendering
// Feeds x through a freshly prepared module in blocks of `block` samples; returns channel 0.
// Output is not latency-compensated (see PreModule::latency()).
inline std::vector<float> render (const PreParams& p, double fs, const std::vector<float>& x, int block = 512)
{
    auto m = std::make_unique<PreModule>();
    m->prepare (fs, 2048);
    m->setParams (p);
    m->reset();
    std::vector<float> l = x, r = x;
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, x.size() - pos);
        float* ch[2] { l.data() + pos, r.data() + pos };
        m->process (ch, 2, n);
    }
    return l;
}

// Parameter change at an absolute sample index (automation).
struct Event
{
    size_t at = 0;
    PreParams p;
};

// Like render(), but applies each event's parameters when the stream reaches its sample index,
// whatever the block size. Blocks are split at event positions so every block size sees the same
// sequence of setParams() calls.
inline std::vector<float> renderAutomated (const PreParams& start, double fs, const std::vector<float>& x, int block,
                                           const std::vector<Event>& events)
{
    auto m = std::make_unique<PreModule>();
    m->prepare (fs, 2048);
    m->setParams (start);
    m->reset();
    std::vector<float> l = x, r = x;
    size_t pos = 0, ei = 0;
    while (pos < x.size())
    {
        while (ei < events.size() && events[ei].at <= pos) m->setParams (events[ei++].p);
        size_t len = std::min<size_t> ((size_t) block, x.size() - pos);
        if (ei < events.size()) len = std::min (len, events[ei].at - pos);
        float* ch[2] { l.data() + pos, r.data() + pos };
        m->process (ch, 2, (int) len);
        pos += len;
    }
    return l;
}

//==============================================================================
// Analysis
inline double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}

inline double rmsDbOf (const std::vector<float>& x, size_t from, size_t to)
{
    return 20.0 * std::log10 (std::max (1.0e-12, rmsOf (x, from, to)));
}

// Peak amplitude of the least-squares fit a sin + b cos at frequency f over x[from, to).
// Exact for a steady sine of any window length (no leakage between cycles).
inline double fitPeak (const std::vector<float>& x, size_t from, size_t to, double fs, double f)
{
    double ss = 0, cc = 0, sc = 0, sy = 0, cy = 0;
    for (size_t i = from; i < to; ++i)
    {
        const double s = std::sin (2.0 * nl::kPiD * f * (double) i / fs), c = std::cos (2.0 * nl::kPiD * f * (double) i / fs);
        ss += s * s; cc += c * c; sc += s * c;
        sy += s * x[i]; cy += c * x[i];
    }
    const double det = ss * cc - sc * sc;
    const double a = (sy * cc - cy * sc) / det, b = (cy * ss - sy * sc) / det;
    return std::sqrt (a * a + b * b);
}

// Gain (dB) of a steady sine at f through the module, measured on a -40 dBFS tone.
inline double gainDb (const PreParams& p, double fs, double f, double inDb = -40.0)
{
    const size_t warm = (size_t) (0.25 * fs), len = (size_t) (0.25 * fs);
    const auto x = sineRmsVec (fs, f, inDb, warm + len);
    const auto y = render (p, fs, x);
    const double in = fitPeak (x, warm, warm + len, fs, f), out = fitPeak (y, warm, warm + len, fs, f);
    return 20.0 * std::log10 (out / in);
}

// Blackman-windowed power spectrum of N samples from `from`; bins 0 .. N/2.
inline std::vector<double> powerSpectrum (const std::vector<float>& x, size_t from, int N)
{
    std::vector<std::complex<double>> a ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.42 - 0.5 * std::cos (2.0 * nl::kPiD * i / N) + 0.08 * std::cos (4.0 * nl::kPiD * i / N);
        a[(size_t) i] = w * (double) x[from + (size_t) i];
    }
    // iterative radix-2 FFT
    const size_t n = (size_t) N;
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * nl::kPiD / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    std::vector<double> P ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) P[(size_t) k] = std::norm (a[(size_t) k]);
    return P;
}

// Aliasing as in ROADMAP section 1: energy outside the true harmonics of a bin-exact sine at f0,
// relative to the total energy, in dB. Uses an N-point analysis starting at `from`.
inline double aliasDb (const std::vector<float>& y, size_t from, double fs, double f0, int N = 8192)
{
    const auto P = powerSpectrum (y, from, N);
    const int bin = (int) std::lround (f0 * N / fs);
    std::vector<bool> harm ((size_t) N / 2 + 1, false);
    for (int h = 1; h * bin <= N / 2; ++h)
        for (int k = h * bin - 3; k <= h * bin + 3; ++k)
            if (k >= 0 && k <= N / 2) harm[(size_t) k] = true;
    double total = 0.0, alias = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        total += P[(size_t) k];
        if (k > 4 && ! harm[(size_t) k]) alias += P[(size_t) k];
    }
    return 10.0 * std::log10 (alias / total + 1.0e-30);
}

// Total harmonic distortion (dB, 20 log amplitude ratio) of a bin-exact sine at f0, harmonics 2..10.
inline double thdDb (const std::vector<float>& y, size_t from, double fs, double f0, int N = 16384)
{
    const auto P = powerSpectrum (y, from, N);
    const int bin = (int) std::lround (f0 * N / fs);
    auto band = [&] (int center)
    {
        double s = 0.0;
        for (int k = center - 3; k <= center + 3; ++k) if (k >= 0 && k <= N / 2) s += P[(size_t) k];
        return s;
    };
    double harm = 0.0;
    for (int h = 2; h <= 10 && h * bin <= N / 2; ++h) harm += band (h * bin);
    return 10.0 * std::log10 (harm / band (bin) + 1.0e-30);
}

// Level (dB) of harmonic h of a bin-exact sine at f0 relative to the fundamental.
inline double harmonicDb (const std::vector<float>& y, size_t from, double fs, double f0, int h, int N = 16384)
{
    const auto P = powerSpectrum (y, from, N);
    const int bin = (int) std::lround (f0 * N / fs);
    auto band = [&] (int center)
    {
        double s = 0.0;
        for (int k = center - 3; k <= center + 3; ++k) if (k >= 0 && k <= N / 2) s += P[(size_t) k];
        return s;
    };
    return 10.0 * std::log10 (band (h * bin) / band (bin) + 1.0e-30);
}

// Spectral splatter (dB re the carrier) of a sine at f0 around a switch at sample `center`: energy
// more than 150 Hz from the carrier in a 50 ms Blackman window, relative to the carrier energy.
inline double splatterDb (const std::vector<float>& y, size_t center, double fs, double f0)
{
    const int L = (int) std::lround (0.05 * fs);
    const int N = 8192;
    std::vector<std::complex<double>> a ((size_t) N);
    for (int i = 0; i < L; ++i)
    {
        // 4-term Blackman-Harris: sidelobes below -90 dB, so the floor is the module's, not the window's
        const double x = 2.0 * nl::kPiD * i / (L - 1);
        const double w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x);
        a[(size_t) i] = w * (double) y[center - (size_t) (L / 2) + (size_t) i];
    }
    const size_t n = (size_t) N;
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * nl::kPiD / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    double carrier = 0.0, splat = 0.0;
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = k * fs / N;
        const double p = std::norm (a[(size_t) k]);
        if (std::abs (f - f0) <= 150.0) carrier += p;
        else splat += p;
    }
    return 10.0 * std::log10 (splat / std::max (carrier, 1.0e-30) + 1.0e-30);
}
} // namespace tool::pre

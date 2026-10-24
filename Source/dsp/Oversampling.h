#pragma once

#include "Common.h"
#include <vector>

#if defined (__SSE2__) || defined (_M_X64) || defined (_M_AMD64)
 #include <emmintrin.h>
 #define NL_OS_SSE 1
#else
 #define NL_OS_SSE 0
#endif

namespace nl
{
//==============================================================================
// 4x linear-phase oversampler: two cascaded 2x FIR stages (Kaiser-windowed sinc), designed
// in terms of the base rate so the filters are identical at 44.1, 48 or 96 kHz.
//
//   stage "wide"  (4 fs rate): passband 0.375 fs, stopband from 1.4817 fs  (21 taps)
//   stage "tight" (2 fs rate): passband 0.375 fs, stopband from 0.5183 fs  (81 taps)
//
// The tight stage puts >= 90 dB of attenuation at 24.9 kHz when the base rate is 48 kHz, so
// harmonics just above Nyquist (the 5th harmonic of a 5 kHz tone) do not fold back. Both
// stages are symmetric, so the whole chain is linear phase with an exact integer latency of
// (wideDelay / 2 + tightDelay) base-rate samples (= 45).
//
// Everything is preallocated in prepare(); up() / down() never allocate. Chunks must be at
// most maxChunk base-rate samples long.
namespace oversampling
{
constexpr int kWideTaps = 21;   // delay 10 samples at 4 fs
constexpr int kTightTaps = 81;  // delay 40 samples at 2 fs
constexpr int kLatency = (kWideTaps - 1) / 2 / 2 + (kTightTaps - 1) / 2; // 5 + 40 = 45
static_assert (((kWideTaps - 1) / 2) % 2 == 0, "wide stage delay must be even for an integer total latency");

inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1.0e-18 * sum) break;
    }
    return sum;
}

// Kaiser-windowed sinc low-pass with `taps` (odd) coefficients; fc is the cut-off in cycles
// per sample of the filter's own rate. DC gain is normalised to `gain`.
inline std::vector<double> kaiserLowpass (int taps, double fc, double beta, double gain)
{
    std::vector<double> h ((size_t) taps);
    const int mid = (taps - 1) / 2;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        const double t = (double) (i - mid);
        const double sinc = t == 0.0 ? 2.0 * fc : std::sin (2.0 * kPiD * fc * t) / (kPiD * t);
        const double r = t / (double) mid;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        h[(size_t) i] = sinc * w;
        sum += h[(size_t) i];
    }
    for (auto& v : h) v *= gain / sum;
    return h;
}

// Dot product of n floats (n is a multiple of 4).
inline float dot (const float* a, const float* b, int n) noexcept
{
#if NL_OS_SSE
    __m128 acc0 = _mm_setzero_ps(), acc1 = _mm_setzero_ps();
    int i = 0;
    for (; i + 8 <= n; i += 8)
    {
        acc0 = _mm_add_ps (acc0, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));
        acc1 = _mm_add_ps (acc1, _mm_mul_ps (_mm_loadu_ps (a + i + 4), _mm_loadu_ps (b + i + 4)));
    }
    for (; i < n; i += 4)
        acc0 = _mm_add_ps (acc0, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));
    acc0 = _mm_add_ps (acc0, acc1);
    alignas (16) float t[4];
    _mm_store_ps (t, acc0);
    return (t[0] + t[1]) + (t[2] + t[3]);
#else
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (int i = 0; i < n; i += 4)
    {
        s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1]; s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3];
    }
    return (s0 + s1) + (s2 + s3);
#endif
}

// acc[i] += c * s[i]. A FIR is evaluated with its taps in the outer loop, so every output adds its taps
// in the same order whatever the block size, and the loop vectorises over the outputs.
inline void accumulateTap (float* acc, const float* s, float c, int n) noexcept
{
    for (int i = 0; i < n; ++i) acc[i] += c * s[i];
}

// One FIR with streaming history. Coefficients are stored zero-padded on the "old" side to a
// multiple of 4 so the dot product can read a contiguous window.
struct History
{
    std::vector<float> buf;
    int pad = 0, cap = 0;

    void allocate (int paddedTaps, int capacity)
    {
        pad = paddedTaps;
        cap = capacity;
        buf.assign ((size_t) (pad + cap), 0.0f);
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    float* head() noexcept { return buf.data() + pad; }          // where new samples go
    void commit (int n) noexcept { std::memmove (buf.data(), buf.data() + n, sizeof (float) * (size_t) pad); }
};

inline std::vector<float> padCoefs (const std::vector<double>& h, int& padded)
{
    padded = ((int) h.size() + 3) & ~3;
    std::vector<float> c ((size_t) padded, 0.0f);
    // right-aligned: c[padded - size + k] = h[k]; h is symmetric so no reversal is needed
    for (size_t k = 0; k < h.size(); ++k) c[(size_t) padded - h.size() + k] = (float) h[k];
    return c;
}

// acc[i] += c * (a[i] + b[i]): the two taps of a symmetric pair share one multiplication.
inline void accumulatePair (float* acc, const float* a, const float* b, float c, int n) noexcept
{
    for (int i = 0; i < n; ++i) acc[i] += c * (a[i] + b[i]);
}

// Output i of a symmetric phase with L taps c (c[k] == c[L-1-k]): sum over k of c[k] * x[i - (L-1) + k], where
// x[0 .. n) is the new input and the L-1 samples before it are history. Each pair (k, L-1-k) is one product.
inline void symmetricPhase (const float* c, int L, const float* x, float* acc, int n) noexcept
{
    for (int k = 0; k < L / 2; ++k) accumulatePair (acc, x + 1 - L + k, x - k, c[k], n);
    if (L & 1) accumulateTap (acc, x - (L - 1) / 2, c[(L - 1) / 2], n);
}

// Interpolate by 2: n inputs -> 2n outputs, polyphase (even / odd taps). The prototype is linear phase, so both
// phases are symmetric and each output takes one product per tap pair (81 products per input instead of 81 + 21).
struct Up2
{
    std::vector<float> even, odd;
    int le = 0, lo = 0;
    History hist;
    std::vector<float> accE, accO; // scratch, sized in design()

    void design (const std::vector<double>& h, int maxIn)
    {
        std::vector<double> e, o;
        for (size_t k = 0; k < h.size(); ++k) (k % 2 == 0 ? e : o).push_back (h[k]);
        even.resize (e.size());
        odd.resize (o.size());
        for (size_t k = 0; k < e.size(); ++k) even[k] = (float) e[k];
        for (size_t k = 0; k < o.size(); ++k) odd[k] = (float) o[k];
        le = (int) e.size();
        lo = (int) o.size();
        hist.allocate (std::max (le, lo), maxIn);
        accE.assign ((size_t) maxIn, 0.0f);
        accO.assign ((size_t) maxIn, 0.0f);
    }
    void reset() { hist.clear(); }

    void process (const float* in, float* out, int n) noexcept
    {
        float* x = hist.head();
        std::memcpy (x, in, sizeof (float) * (size_t) n);
        std::fill (accE.begin(), accE.begin() + n, 0.0f);
        std::fill (accO.begin(), accO.begin() + n, 0.0f);
        symmetricPhase (even.data(), le, x, accE.data(), n);
        symmetricPhase (odd.data(), lo, x, accO.data(), n);
        for (int i = 0; i < n; ++i)
        {
            out[2 * i] = accE[(size_t) i];
            out[2 * i + 1] = accO[(size_t) i];
        }
        hist.commit (n);
    }
};

// Decimate by 2: 2n inputs -> n outputs. The prototype is symmetric (odd length L), so each output takes one
// product per tap pair: out[i] = sum_k taps[k] * ext[2i + k] + middle tap, with ext[j] = the input at j + 1 - L.
// Splitting ext into even and odd samples, ext[2i + k] = phase(k & 1)[i + (k >> 1)], so each pair is a
// contiguous pass over the outputs.
struct Down2
{
    std::vector<float> taps;
    int L = 0;
    History hist;
    std::vector<float> phase0, phase1, acc; // scratch, sized in design()

    void design (const std::vector<double>& h, int maxOut)
    {
        taps.resize (h.size());
        for (size_t k = 0; k < h.size(); ++k) taps[k] = (float) h[k];
        L = (int) h.size();
        hist.allocate (L - 1, 2 * maxOut);
        phase0.assign ((size_t) (maxOut + L), 0.0f);
        phase1.assign ((size_t) (maxOut + L), 0.0f);
        acc.assign ((size_t) maxOut, 0.0f);
    }
    void reset() { hist.clear(); }

    void process (const float* in, float* out, int n) noexcept
    {
        float* x = hist.head();
        std::memcpy (x, in, sizeof (float) * (size_t) (2 * n));
        const float* ext = x + 1 - L;
        const int len = n + (L - 1) / 2; // phase entries the outputs read: up to ext[2n + L - 3]
        for (int m = 0; m < len; ++m)
        {
            phase0[(size_t) m] = ext[2 * m];
            phase1[(size_t) m] = ext[2 * m + 1];
        }
        std::fill (acc.begin(), acc.begin() + n, 0.0f);
        auto ph = [&] (int k) { return (k & 1 ? phase1.data() : phase0.data()) + (k >> 1); };
        for (int k = 0; k < (L - 1) / 2; ++k) accumulatePair (acc.data(), ph (k), ph (L - 1 - k), taps[(size_t) k], n);
        accumulateTap (acc.data(), ph ((L - 1) / 2), taps[(size_t) ((L - 1) / 2)], n);
        std::memcpy (out, acc.data(), sizeof (float) * (size_t) n);
        hist.commit (2 * n);
    }
};
} // namespace oversampling

//==============================================================================
class Oversampler4
{
public:
    static constexpr int kFactor = 4;

    // Integer latency in base-rate samples (identical for every sample rate).
    static constexpr int latency() noexcept { return oversampling::kLatency; }

    void prepare (int maxChunk, int numChannels)
    {
        using namespace oversampling;
        maxN = std::max (1, maxChunk);
        const auto tight = kaiserLowpass (kTightTaps, 0.5 * (0.375 + 0.5183) / 2.0, 9.0, 2.0);
        const auto wide = kaiserLowpass (kWideTaps, 0.5 * (0.375 + 1.4817) / 4.0, 9.0, 2.0);
        const auto tightD = kaiserLowpass (kTightTaps, 0.5 * (0.375 + 0.5183) / 2.0, 9.0, 1.0);
        const auto wideD = kaiserLowpass (kWideTaps, 0.5 * (0.375 + 1.4817) / 4.0, 9.0, 1.0);
        channels = std::max (1, std::min (2, numChannels));
        for (int c = 0; c < channels; ++c)
        {
            auto& s = st[c];
            s.upTight.design (tight, maxN);
            s.upWide.design (wide, 2 * maxN);
            s.downWide.design (wideD, 2 * maxN);
            s.downTight.design (tightD, maxN);
            s.mid.assign ((size_t) (2 * maxN), 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (int c = 0; c < channels; ++c) resetChannel (c);
    }

    void resetChannel (int ch)
    {
        auto& s = st[ch];
        s.upTight.reset(); s.upWide.reset(); s.downWide.reset(); s.downTight.reset();
    }

    // n base-rate samples -> 4n samples at the oversampled rate.
    void up (int ch, const float* in, float* out4, int n) noexcept
    {
        auto& s = st[ch];
        s.upTight.process (in, s.mid.data(), n);
        s.upWide.process (s.mid.data(), out4, 2 * n);
    }

    // 4n samples at the oversampled rate -> n base-rate samples.
    void down (int ch, const float* in4, float* out, int n) noexcept
    {
        auto& s = st[ch];
        s.downWide.process (in4, s.mid.data(), 2 * n);
        s.downTight.process (s.mid.data(), out, n);
    }

private:
    struct Channel
    {
        oversampling::Up2 upTight, upWide;
        oversampling::Down2 downWide, downTight;
        std::vector<float> mid;
    };
    Channel st[2];
    int maxN = 1, channels = 2;
};

//==============================================================================
// Integer delay for the dry path of a latency-bearing section.
class DryDelay
{
public:
    void prepare (int maxDelay, int numChannels)
    {
        size = 1;
        while (size < maxDelay + 2) size <<= 1;
        mask = size - 1;
        channels = std::max (1, std::min (2, numChannels));
        for (int c = 0; c < 2; ++c) buf[c].assign ((size_t) size, 0.0f);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) std::fill (buf[c].begin(), buf[c].end(), 0.0f);
        pos = 0;
    }
    // Writes `in` and returns the sample delayed by `delay` (0 = no delay) for each position.
    void process (int ch, const float* in, float* out, int n, int delay) noexcept
    {
        auto& b = buf[ch];
        int p = pos;
        for (int i = 0; i < n; ++i, ++p)
        {
            b[(size_t) (p & mask)] = in[i];
            out[i] = b[(size_t) ((p - delay) & mask)];
        }
        if (ch == channels - 1 || channels == 1) pos = p;
    }

private:
    std::vector<float> buf[2];
    int size = 1, mask = 0, pos = 0, channels = 2;
};
} // namespace nl

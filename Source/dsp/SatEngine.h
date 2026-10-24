#pragma once

#include "Oversampling.h"
#include "Saturation.h"
#include <array>
#include <complex>
#include <thread>
#include <vector>

namespace nl
{
// Single-channel saturation engine plus the numerical calibration of its gain staging.
//
// Chain (all linear stages are exactly inverted, so only the harmonics are shaped):
//   tone pre-tilt -> type pre-emphasis -> ADAA droop pre-emphasis -> x drive gain
//   -> shaping curve (ADAA) -> x output gain -> type post-emphasis + level trims
//   -> tone post-tilt -> DC blocker (asymmetric types)
namespace sat
{
constexpr float kShelfQ = 0.70710678f;

// ADAA order: Eco (1x) uses order 1, because order 2 has a null at fs/3 (see Saturation.h).
constexpr int kEcoOrder = 1, kHqOrder = 2;
// Zero-phase droop FIR delay on the 4x path (4x samples): 7 + the ADAA-2 average (1) = 8 = 2 base samples.
constexpr int kHqDroopDelay = 11;

struct TypeSpec
{
    double lsFreq, lsDb, lsQ; // low-frequency pre-emphasis bell (the post stage applies the inverse), 0 dB = none
    double hsFreq, hsDb;      // pre-emphasis high shelf
    bool trimLS;              // post low-frequency level trim exists for this type
    bool dc;             // asymmetric: needs a DC blocker
};

inline TypeSpec spec (int type) noexcept
{
    switch (type)
    {
        case shaper::Even:        return { 150.0, 0.0, 0.7, 4000.0, 0.0, false, true };
        case shaper::Heavy:       return { 160.0, 3.0, 0.7, 4000.0, 0.0, true, false };
        case shaper::Crush:       return { 150.0, 0.0, 0.7, 1500.0, 4.0, false, false };
        case shaper::Tape:        return { 80.0, 2.5, 0.8, 5500.0, 3.0, true, false };
        case shaper::Tube:        return { 150.0, 0.0, 0.7, 4000.0, 0.0, false, true };
        case shaper::Transformer: return { 90.0, 5.0, 0.6, 4000.0, 0.0, true, false };
        case shaper::Odd:
        default:                  return { 150.0, 0.0, 0.7, 4000.0, 0.0, false, false };
    }
}

// Even bias (static) and Tube bias (envelope-driven, b = kTubeBiasMax * env / (env + kTubeBiasKnee) with env
// following |shaper input|). Both are scaled by biasWeight(U), where U = drive + operating level (dB) is the
// shaper level the calibration is keyed by. An asymmetric curve makes even harmonics in proportion to its
// bias at low level, so the bias is absent at a clean setting (THD at the operating level < -70 dB) and
// builds up with the drive.
constexpr double kEvenBias = 0.45, kTubeBiasMax = 0.55, kTubeBiasKnee = 0.4;
constexpr double kBiasU0 = -22.0, kBiasU1 = -8.0;
// Tape: the extra high-frequency loss (dB, at full envelope) fades in with e = env / (env + kTapeKnee).
constexpr double kTapeKnee = 0.4, kTapeDynHsDb = -3.0, kTapeBumpHz = 60.0, kTapeBumpDb = 1.5;
inline double biasWeight (double U) noexcept { return std::clamp ((U - kBiasU0) / (kBiasU1 - kBiasU0), 0.0, 1.0); }

// Minimum-phase FIR that pre-compensates the droop of order-N ADAA ((1 + z^-1) / 2 for order 1,
// (1 + z^-1 + z^-2) / 3 for order 2): the target gain is the inverse droop up to fs/3 (16 kHz at 48 kHz), then
// tapers smoothly to 0 dB by 0.46 fs, so the ADAA path is flat to 16 kHz without boosting
// whatever sits near Nyquist ahead of the shaper. Designed with the real-cepstrum method;
// `taps` coefficients are returned (zero latency, causal).
constexpr int kMaxDroopTaps = 64;

struct DroopFir
{
    std::array<float, kMaxDroopTaps> h {};
    int taps = 0;
    bool sym = false; // h[k] == h[taps - 1 - k] (zero-phase design): firRingSym
};

// Target log-gain of the droop correction for order-N ADAA at `rate`, 512-point grid (the magnitude of the
// inverse droop up to fs/3, tapering smoothly to 0 dB by 0.46 fs; see designDroopFirTaps).
inline std::vector<double> droopLogTarget (int order, double rate, double f1, double f2)
{
    constexpr int N = 512;
    std::vector<double> L (N);
    auto droopLn = [&] (double f)
    {
        const double w = 2.0 * kPiD * f / rate;
        return order == 1 ? -std::log (std::abs (std::cos (0.5 * w))) : -std::log (std::abs (1.0 + 2.0 * std::cos (w)) / 3.0);
    };
    for (int k = 0; k < N; ++k)
    {
        const double f = rate * (double) std::min (k, N - k) / (double) N;
        double l;
        if (f <= f1) l = droopLn (f);
        else if (f < f2) l = droopLn (f1) * 0.5 * (1.0 + std::cos (kPiD * (f - f1) / (f2 - f1)));
        else l = 0.0;
        L[(size_t) k] = l;
    }
    return L;
}

inline std::array<float, kMaxDroopTaps> designDroopFirTaps (int order, double rate, double baseFs, int taps)
{
    constexpr int N = 512;
    std::vector<double> L = droopLogTarget (order, rate, baseFs / 3.0, 0.46 * baseFs), c (N), h (N);
    // real cepstrum, folded to a causal (minimum-phase) cepstrum
    for (int n = 0; n < N; ++n)
    {
        double acc = 0.0;
        for (int k = 0; k < N; ++k) acc += L[(size_t) k] * std::cos (2.0 * kPiD * (double) k * n / N);
        c[(size_t) n] = acc / N;
    }
    for (int n = 1; n < N / 2; ++n) c[(size_t) n] *= 2.0;
    for (int n = N / 2 + 1; n < N; ++n) c[(size_t) n] = 0.0;
    // H = exp(DFT(c)), h = IDFT(H)
    std::vector<std::complex<double>> H (N);
    for (int k = 0; k < N; ++k)
    {
        std::complex<double> acc = 0.0;
        for (int n = 0; n < N / 2 + 1; ++n) acc += c[(size_t) n] * std::polar (1.0, -2.0 * kPiD * (double) k * n / N);
        H[(size_t) k] = std::exp (acc);
    }
    std::array<float, kMaxDroopTaps> out {};
    for (int n = 0; n < std::min (taps, kMaxDroopTaps); ++n)
    {
        std::complex<double> acc = 0.0;
        for (int k = 0; k < N; ++k) acc += H[(size_t) k] * std::polar (1.0, 2.0 * kPiD * (double) k * n / N);
        out[(size_t) n] = (float) (acc.real() / N);
    }
    return out;
}

inline DroopFir makeDroopFir (int order, double rate, double baseFs)
{
    DroopFir d;
    d.taps = rate > 1.5 * baseFs ? 12 : 32;
    d.h = designDroopFirTaps (order, rate, baseFs, d.taps);
    return d;
}

// Zero-phase variant for the oversampled path: the same target magnitude, realised as a symmetric
// Hann-windowed FIR with its centre at sample `delay`. The path delay is then exact: `delay` plus the
// one-sample average of ADAA order 2, so it is an integer number of base samples when delay + 1 is a
// multiple of 4 (kHqDroopDelay). Taps: 2 * delay + 1 (at most kMaxDroopTaps).
inline DroopFir makeDroopFirZeroPhase (int order, double rate, double baseFs, int delay)
{
    constexpr int N = 512;
    // Taper well above the passband edge (the windowed FIR smooths the taper into the band it borders).
    const std::vector<double> L = droopLogTarget (order, rate, 0.5 * baseFs, 0.625 * baseFs);
    DroopFir d;
    d.taps = 2 * delay + 1;
    for (int m = 0; m < d.taps; ++m)
    {
        const int n = m - delay;
        double acc = 0.0;
        for (int k = 0; k < N; ++k) acc += std::exp (L[(size_t) k]) * std::cos (2.0 * kPiD * (double) k * n / N);
        const double w = 0.5 * (1.0 + std::cos (kPiD * (double) n / (delay + 1.0)));
        d.h[(size_t) m] = (float) (acc / N * w);
    }
    // the window changes the DC gain: normalise so the path has exactly unity gain at DC
    double sum = 0.0;
    for (int m = 0; m < d.taps; ++m) sum += d.h[(size_t) m];
    for (int m = 0; m < d.taps; ++m) d.h[(size_t) m] = (float) (d.h[(size_t) m] / sum);
    // the design is symmetric (up to rounding): make it exact, so firRingSym can share one product per pair
    for (int m = 0; m < delay; ++m) d.h[(size_t) (d.taps - 1 - m)] = d.h[(size_t) m];
    d.sym = true;
    return d;
}

//------------------------------------------------------------------------------
struct Ramp
{
    double g = 1.0, dg = 0.0, go = 1.0, dgo = 0.0; // pre-shaper gain and output gain, value and per-sample step
};

// Coefficients shared by the channels of one engine.
struct Coefs
{
    int type = 0, order = 1;
    double rate = 48000.0;
    bool dc = false, tube = false, preLSOn = false, preHSOn = false, postLSOn = false;
    bool dynHF = false, bump = false; // Tape: level-dependent HF loss and the head bump
    SvfCoefs dynHS, bumpBell;
    DroopFir droop;
    SvfCoefs preLS, preHS, postLS, postHS;
    bool tiltOn = false;
    SvfCoefs tPreLS, tPreHS, tPostLS, tPostHS;
    float tPreGain = 1.0f, tPostGain = 1.0f;
    OnePole dcCoef;
    float envAtt = 0.0f, envRel = 0.0f;
    float lsTrim = 0.0f, hsTrim = 0.0f;
    float biasScale = 1.0f; // biasWeight() of the current shaper level

    void configure (int t, double rate_, int order_, const DroopFir& droopFir)
    {
        type = t;
        rate = rate_;
        order = order_;
        const auto sp = spec (t);
        dc = sp.dc;
        tube = t == shaper::Tube;
        dynHF = bump = t == shaper::Tape;
        if (dynHF) dynHS = makeSvf (SvfType::HighShelf, rate, 5500.0f, kShelfQ, (float) kTapeDynHsDb);
        if (bump) bumpBell = makeSvf (SvfType::Bell, rate, (float) kTapeBumpHz, 1.5f, (float) kTapeBumpDb);
        preLSOn = sp.lsDb != 0.0;
        preHSOn = sp.hsDb != 0.0;
        postLSOn = sp.trimLS;
        preLS = makeSvf (SvfType::Bell, rate, (float) sp.lsFreq, (float) sp.lsQ, (float) sp.lsDb);
        preHS = makeSvf (SvfType::HighShelf, rate, (float) sp.hsFreq, kShelfQ, (float) sp.hsDb);
        droop = droopFir;
        dcCoef.setCutoff (rate, 20.0f);
        envAtt = timeCoef (5.0f, rate);
        envRel = timeCoef (80.0f, rate);
        updatePost();
    }

    // Level trims (dB) added to the post-emphasis shelves.
    void setTrim (float lsDb, float hsDb)
    {
        lsTrim = lsDb;
        hsTrim = hsDb;
        updatePost();
    }

    // tone in -1 .. 1: +1 moves harmonics up. The shaper sees the lows raised and the highs cut, so the
    // harmonics it generates at high orders come out relatively louder after the matching post tilt.
    // -1 does the reverse (harmonics toward the lows).
    void setTone (float t)
    {
        tiltOn = std::abs (t) > 1.0e-4f;
        if (! tiltOn) return;
        const float a = 6.0f * t;
        tPreLS = makeSvf (SvfType::LowShelf, rate, kTiltFreq, kShelfQ, a);
        tPreHS = makeSvf (SvfType::HighShelf, rate, kTiltFreq, kShelfQ, -a);
        tPostLS = makeSvf (SvfType::LowShelf, rate, kTiltFreq, kShelfQ, -a);
        tPostHS = makeSvf (SvfType::HighShelf, rate, kTiltFreq, kShelfQ, a);
        // keep the pink-noise power into the shaper constant
        const double p = 0.5 * (std::pow (10.0, a / 10.0) + std::pow (10.0, -a / 10.0));
        tPreGain = (float) (1.0 / std::sqrt (p));
        tPostGain = 1.0f / tPreGain;
    }

private:
    static constexpr float kTiltFreq = 600.0f;

    void updatePost()
    {
        const auto sp = spec (type);
        postLS = makeSvf (SvfType::Bell, rate, (float) sp.lsFreq, (float) sp.lsQ, (float) -sp.lsDb + lsTrim);
        postHS = makeSvf (SvfType::HighShelf, rate, (float) sp.hsFreq, kShelfQ, (float) -sp.hsDb + hsTrim);
    }
};

struct State
{
    float droopHist[64] {};
    int droopPos = 0;
    SvfState preLS, preHS, postLS, postHS, tPreLS, tPreHS, tPostLS, tPostHS, bumpSt, dynSt;
    OnePole dc;
    float env = 0.0f;
    shaper::Adaa<shaper::Sig> sig;
    shaper::Adaa<shaper::BiasedSig> biased;
    shaper::Adaa<shaper::Cubic> cubic;
    shaper::Adaa<shaper::Clip> clip;
    shaper::Adaa<shaper::Atan> atn;

    void reset (const Coefs& c) noexcept
    {
        std::fill (std::begin (droopHist), std::end (droopHist), 0.0f);
        droopPos = 0;
        for (auto* s : { &preLS, &preHS, &postLS, &postHS, &tPreLS, &tPreHS, &tPostLS, &tPostHS, &bumpSt, &dynSt }) s->reset();
        dc.reset();
        dc.G = c.dcCoef.G;
        env = 0.0f;
        biased.s.set (0.0); // runSegment() sets the bias before every segment
        for (auto* a : { &sig.order, &biased.order, &cubic.order, &clip.order, &atn.order }) *a = c.order;
        sig.reset(); biased.reset(); cubic.reset(); clip.reset(); atn.reset();
    }

    // Keeps the filter and shaper memory but picks up recomputed coefficients (DC blocker).
    void syncCoefs (const Coefs& c) noexcept { dc.G = c.dcCoef.G; }
};

// Sets the bias of the Even / Tube shaper. Called once per control tick (and per calibration segment), never
// per host block or per sample, so the envelope-driven bias is the same whatever the host block split.
inline void updateBias (const Coefs& c, State& st) noexcept
{
    if (c.type != shaper::Even && c.type != shaper::Tube) return;
    const double base = c.tube ? kTubeBiasMax * st.env / (st.env + kTubeBiasKnee) : kEvenBias;
    st.biased.s.set (base * c.biasScale);
    st.biased.rebias();
}

// Causal FIR over one block, with the history in a 64-entry ring (`pos` is the next write slot, the newest
// sample is at pos - 1): out[i] = sum over k of h[k] * x[i - k]. Each output adds its taps in the same order
// whatever the block size, so splitting a block does not change the result. n <= kFirMaxBlock.
constexpr int kFirMaxBlock = 4 * kControlBlock; // a 4x control segment, the longest block the shaper gets

inline void firRing (const float* h, int taps, float* ring, int& pos, const float* in, float* out, int n) noexcept
{
    float win[kFirMaxBlock + 64]; // taps - 1 history samples, then the block
    const int hl = std::max (0, taps - 1);
    for (int j = 0; j < hl; ++j) win[j] = ring[(pos - hl + j) & 63];
    for (int i = 0; i < n; ++i) win[hl + i] = in[i];
    for (int i = 0; i < n; ++i) ring[(pos + i) & 63] = in[i];
    pos = (pos + n) & 63;
    std::fill (out, out + n, 0.0f);
    for (int k = 0; k < taps; ++k)
    {
        const float hk = h[k];
        const float* src = win + hl - k;
        for (int i = 0; i < n; ++i) out[i] += hk * src[i];
    }
}

// firRing for a symmetric FIR (odd taps, h[k] == h[taps - 1 - k]): tap k and tap taps-1-k read x[i-k] and
// win[i + k], so each pair is one product.
inline void firRingSym (const float* h, int taps, float* ring, int& pos, const float* in, float* out, int n) noexcept
{
    float win[kFirMaxBlock + 64];
    const int hl = taps - 1, c = hl / 2;
    for (int j = 0; j < hl; ++j) win[j] = ring[(pos - hl + j) & 63];
    for (int i = 0; i < n; ++i) win[hl + i] = in[i];
    for (int i = 0; i < n; ++i) ring[(pos + i) & 63] = in[i];
    pos = (pos + n) & 63;
    const float* mid = win + hl - c;
    for (int i = 0; i < n; ++i) out[i] = h[c] * mid[i];
    for (int k = 0; k < c; ++k)
    {
        const float hk = h[k];
        const float* a = win + hl - k;
        const float* b = win + k;
        for (int i = 0; i < n; ++i) out[i] += hk * (a[i] + b[i]);
    }
}

// The droop filter of a design: the symmetric variant when the design is zero-phase.
inline void droopFilter (const DroopFir& d, float* ring, int& pos, const float* in, float* out, int n) noexcept
{
    if (d.sym) firRingSym (d.h.data(), d.taps, ring, pos, in, out, n);
    else firRing (d.h.data(), d.taps, ring, pos, in, out, n);
}

// One chunk of at most kFirMaxBlock samples for N channels (N = 1 or 2). The channels run in one loop, so the
// sequential filters of the two channels overlap in the pipeline; each channel's arithmetic is unchanged.
// The ramp is taken by reference, so chunks continue it exactly.
template <class S, bool Tube, int N>
inline void runChunk (shaper::Adaa<S>* const* adaa, State* const* st, const Coefs& c, const float* const* in, float* const* out, int n, Ramp& r) noexcept
{
    // Pass 1: pre-emphasis (sequential filters). Pass 2: the droop FIR over the block (linear, no feedback).
    // Pass 3: gain, shaper, post-emphasis, per sample.
    float pre[N][kFirMaxBlock], drooped[N][kFirMaxBlock];
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < N; ++ch)
        {
            State& s = *st[ch];
            float x = in[ch][i];
            if (c.tiltOn) x = s.tPreHS.process (s.tPreLS.process (x, c.tPreLS), c.tPreHS) * c.tPreGain;
            if (c.preLSOn) x = s.preLS.process (x, c.preLS);
            if (c.preHSOn) x = s.preHS.process (x, c.preHS);
            pre[ch][i] = x;
        }
    for (int ch = 0; ch < N; ++ch) droopFilter (c.droop, st[ch]->droopHist, st[ch]->droopPos, pre[ch], drooped[ch], n);
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < N; ++ch)
        {
            State& s = *st[ch];
            shaper::Adaa<S>& adaaCh = *adaa[ch];
            const float x = drooped[ch][i];
            const double u = (double) x * r.g;
            if (Tube || c.dynHF) // envelope of the shaper input: Tube bias, Tape HF loss
            {
                const float a = (float) std::abs (u);
                s.env += (a - s.env) * (a > s.env ? 1.0f - c.envAtt : 1.0f - c.envRel);
            }
            double ys;
            if (c.dc)
            {
                // Remove only the DC / sub-audio part of the distortion residual (shaper output minus its
                // linear response), so low-frequency program content passes untouched.
                const double lin = c.order == 1 ? 0.5 * (u + adaaCh.x1) : (u + adaaCh.x1 + adaaCh.x2) * (1.0 / 3.0);
                ys = adaaCh.process (u);
                ys -= (double) s.dc.lowpass ((float) (ys - lin));
            }
            else
            {
                ys = adaaCh.process (u);
            }
            float y = (float) (ys * r.go);
            if (c.postLSOn) y = s.postLS.process (y, c.postLS);
            y = s.postHS.process (y, c.postHS);
            if (c.bump) y = s.bumpSt.process (y, c.bumpBell);
            if (c.dynHF)
            {
                const float e = s.env / (s.env + (float) kTapeKnee);
                y += e * (s.dynSt.process (y, c.dynHS) - y);
            }
            if (c.tiltOn) y = s.tPostLS.process (s.tPostHS.process (y, c.tPostHS), c.tPostLS) * c.tPostGain;
            out[ch][i] = y;
        }
        r.g += r.dg;
        r.go += r.dgo;
    }
}

template <class S, bool Tube, int N>
inline void runSegment (shaper::Adaa<S> State::* member, State* const* st, const Coefs& c, const float* const* in, float* const* out, int n, Ramp r) noexcept
{
    shaper::Adaa<S>* adaa[N];
    for (int ch = 0; ch < N; ++ch) adaa[ch] = &(st[ch]->*member);
    for (int i = 0; i < n; i += kFirMaxBlock)
    {
        const float* inC[N];
        float* outC[N];
        for (int ch = 0; ch < N; ++ch) { inC[ch] = in[ch] + i; outC[ch] = out[ch] + i; }
        runChunk<S, Tube, N> (adaa, st, c, inC, outC, std::min (kFirMaxBlock, n - i), r);
    }
}

template <int N>
inline void processN (State* const* st, const Coefs& c, const float* const* in, float* const* out, int n, const Ramp& r) noexcept
{
    switch (c.type)
    {
        case shaper::Even:        runSegment<shaper::BiasedSig, false, N> (&State::biased, st, c, in, out, n, r); break;
        case shaper::Tube:        runSegment<shaper::BiasedSig, true, N> (&State::biased, st, c, in, out, n, r); break;
        case shaper::Heavy:       runSegment<shaper::Cubic, false, N> (&State::cubic, st, c, in, out, n, r); break;
        case shaper::Crush:       runSegment<shaper::Clip, false, N> (&State::clip, st, c, in, out, n, r); break;
        case shaper::Tape:        runSegment<shaper::Atan, false, N> (&State::atn, st, c, in, out, n, r); break;
        case shaper::Odd:
        case shaper::Transformer:
        default:                  runSegment<shaper::Sig, false, N> (&State::sig, st, c, in, out, n, r); break;
    }
}

inline void process (State& st, const Coefs& c, const float* in, float* out, int n, const Ramp& r) noexcept
{
    State* s[1] { &st };
    const float* i[1] { in };
    float* o[1] { out };
    processN<1> (s, c, i, o, n, r);
}

// Both channels of a stereo section in one pass (see runChunk).
inline void processStereo (State& s0, State& s1, const Coefs& c, const float* in0, const float* in1, float* out0, float* out1, int n, const Ramp& r) noexcept
{
    State* s[2] { &s0, &s1 };
    const float* i[2] { in0, in1 };
    float* o[2] { out0, out1 };
    processN<2> (s, c, i, o, n, r);
}

//==============================================================================
// Calibration. The output of the chain depends on the drive gain and the input level only
// through their product, so the tables are indexed by U = drive (dB) + operating level (dBFS)
// and are valid for any operating level. For each U the raw chain (output gain 1, no trims)
// is run with sines at 100 Hz / 1 kHz / 5 kHz and pink noise at -18 dBFS RMS, and the output
// gain C (plus low / high shelf trims for types with frequency-dependent drive) is solved
// in the least-squares sense so that every stimulus returns to its input RMS.
constexpr int kCalPoints = 36;
constexpr double kCalU0 = -40.0, kCalDU = 2.0, kCalRefLevel = -18.0;

struct CalTable
{
    std::array<float, kCalPoints> c {}, l {}, h {}; // output gain (at the -18 dBFS reference) and trims, dB
};

inline double lookup (const std::array<float, kCalPoints>& t, double U) noexcept
{
    const double p = std::clamp ((U - kCalU0) / kCalDU, 0.0, (double) (kCalPoints - 1) - 1.0e-9);
    const int i = (int) p;
    const double fr = p - i;
    return (double) t[(size_t) i] * (1.0 - fr) + (double) t[(size_t) i + 1] * fr;
}

namespace detail
{
// Stationary pink-like multisine: 6 equal-power components per octave from 20 Hz to 20 kHz on a
// 10 Hz grid (so 4800 samples hold whole cycles at 48 kHz) with pseudo-random phases. Its
// amplitude distribution is close to Gaussian but, unlike random noise, it has no slow level
// fluctuations, so the calibration does not depend on a particular noise realisation.
inline std::vector<float> pinkMultisine (int n, double fs)
{
    std::vector<double> v ((size_t) n, 0.0);
    uint32_t s = 2463534242u;
    auto rnd = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) (s >> 8) / 16777216.0; };
    double lastF = 0.0;
    for (int k = 0; k < 60; ++k)
    {
        const double f = std::round (20.0 * std::pow (1000.0, k / 59.0) / 10.0) * 10.0;
        if (f == lastF) continue;
        lastF = f;
        const double ph = 2.0 * kPiD * rnd();
        for (int i = 0; i < n; ++i) v[(size_t) i] += std::sin (2.0 * kPiD * f * (double) i / fs + ph);
    }
    std::vector<float> out ((size_t) n);
    for (int i = 0; i < n; ++i) out[(size_t) i] = (float) v[(size_t) i];
    return out;
}

struct Stimulus
{
    std::vector<float> x;
    double rms = 0.0;
    double lsWeight = 0.0, hsWeight = 0.0; // dB change of the post shelf per dB of trim at this stimulus
};

inline double acRms (const std::vector<float>& v, size_t from)
{
    double m = 0.0, s = 0.0;
    const size_t n = v.size() - from;
    for (size_t i = from; i < v.size(); ++i) { m += v[i]; s += (double) v[i] * v[i]; }
    m /= (double) n;
    return std::sqrt (std::max (0.0, s / (double) n - m * m));
}

inline CalTable computeTable (int type, bool hq)
{
    constexpr double fs = 48000.0;
    const double sigma = std::pow (10.0, kCalRefLevel / 20.0);
    const auto sp = spec (type);
    const int warm = 2400;

    // stimuli
    std::vector<Stimulus> stim (4);
    const double freqs[3] { 100.0, 1000.0, 5000.0 };
    for (int k = 0; k < 3; ++k)
    {
        auto& s = stim[(size_t) k];
        s.x.resize ((size_t) (warm + 2400));
        for (size_t i = 0; i < s.x.size(); ++i) s.x[i] = (float) (sigma * std::sqrt (2.0) * std::sin (2.0 * kPiD * freqs[k] * (double) i / fs));
    }
    {
        auto& s = stim[3];
        s.x = pinkMultisine (warm + 4800, fs);
        const double r = acRms (s.x, (size_t) warm);
        for (auto& v : s.x) v = (float) (v * sigma / r);
    }
    for (auto& s : stim) s.rms = acRms (s.x, (size_t) warm);

    // shelf weights: dB response of a +1 dB shelf at each stimulus
    const auto ls1 = makeSvf (SvfType::Bell, fs, (float) sp.lsFreq, (float) sp.lsQ, 1.0f);
    const auto hs1 = makeSvf (SvfType::HighShelf, fs, (float) sp.hsFreq, kShelfQ, 1.0f);
    for (int k = 0; k < 3; ++k)
    {
        stim[(size_t) k].lsWeight = 20.0 * std::log10 ((double) ls1.magnitude (freqs[k], fs));
        stim[(size_t) k].hsWeight = 20.0 * std::log10 ((double) hs1.magnitude (freqs[k], fs));
    }
    {
        double pl = 0.0, ph = 0.0;
        const int pts = 96;
        for (int k = 0; k < pts; ++k)
        {
            const double f = 20.0 * std::pow (1000.0, (k + 0.5) / pts);
            pl += std::pow ((double) ls1.magnitude (f, fs), 2.0);
            ph += std::pow ((double) hs1.magnitude (f, fs), 2.0);
        }
        stim[3].lsWeight = 10.0 * std::log10 (pl / pts);
        stim[3].hsWeight = 10.0 * std::log10 (ph / pts);
    }

    Coefs cf;
    if (hq) cf.configure (type, 4.0 * fs, kHqOrder, makeDroopFirZeroPhase (kHqOrder, 4.0 * fs, fs, kHqDroopDelay));
    else cf.configure (type, fs, kEcoOrder, makeDroopFir (kEcoOrder, fs, fs));
    State st;
    Oversampler4 os;
    os.prepare (32, 1);
    std::vector<float> out;
    CalTable tab;
    for (int k = 0; k < kCalPoints; ++k)
    {
        const double U = kCalU0 + kCalDU * k;
        cf.biasScale = (float) biasWeight (U);
        Ramp r;
        r.g = std::pow (10.0, (U - kCalRefLevel) / 20.0);

        double d[4];
        for (int i = 0; i < 4; ++i)
        {
            const auto& s = stim[(size_t) i];
            st.reset (cf);
            os.reset();
            out.assign (s.x.size(), 0.0f);
            for (size_t p = 0; p < s.x.size(); p += 32)
            {
                updateBias (cf, st);
                const int n = (int) std::min<size_t> (32, s.x.size() - p);
                if (hq)
                {
                    float up[4 * 32], o4[4 * 32];
                    os.up (0, s.x.data() + p, up, n);
                    process (st, cf, up, o4, 4 * n, r);
                    os.down (0, o4, out.data() + p, n);
                }
                else
                {
                    process (st, cf, s.x.data() + p, out.data() + p, n, r);
                }
            }
            d[i] = 20.0 * std::log10 (s.rms / std::max (1.0e-12, acRms (out, (size_t) warm)));
        }

        // least squares for (C, l, h) with a light ridge on the trims
        const bool useL = sp.trimLS;
        double A[3][3] {}, bvec[3] {};
        for (int i = 0; i < 4; ++i)
        {
            const double row[3] { 1.0, useL ? stim[(size_t) i].lsWeight : 0.0, stim[(size_t) i].hsWeight };
            const double wgt = i == 3 ? 3.0 : 1.0; // the three sines and the noise count equally
            for (int a = 0; a < 3; ++a)
            {
                bvec[a] += wgt * row[a] * d[i];
                for (int b = 0; b < 3; ++b) A[a][b] += wgt * row[a] * row[b];
            }
        }
        A[1][1] += 0.05 + (useL ? 0.0 : 1.0e6);
        A[2][2] += 0.05;
        // solve 3x3 by Cramer / Gaussian elimination
        double M[3][4];
        for (int a = 0; a < 3; ++a) { for (int b = 0; b < 3; ++b) M[a][b] = A[a][b]; M[a][3] = bvec[a]; }
        for (int col = 0; col < 3; ++col)
        {
            int piv = col;
            for (int r2 = col + 1; r2 < 3; ++r2) if (std::abs (M[r2][col]) > std::abs (M[piv][col])) piv = r2;
            for (int c2 = 0; c2 < 4; ++c2) std::swap (M[col][c2], M[piv][c2]);
            for (int r2 = col + 1; r2 < 3; ++r2)
            {
                const double f = M[r2][col] / M[col][col];
                for (int c2 = col; c2 < 4; ++c2) M[r2][c2] -= f * M[col][c2];
            }
        }
        double sol[3];
        for (int a = 2; a >= 0; --a)
        {
            double v = M[a][3];
            for (int b = a + 1; b < 3; ++b) v -= M[a][b] * sol[b];
            sol[a] = v / M[a][a];
        }
        tab.l[(size_t) k] = (float) std::clamp (sol[1], -8.0, 8.0);
        tab.h[(size_t) k] = (float) std::clamp (sol[2], -8.0, 8.0);
        tab.c[(size_t) k] = (float) sol[0];
    }
    return tab;
}
} // namespace detail

// All tables, computed once per process on first use (call from prepare(), never from the
// audio thread). Each type runs on its own thread; the result does not depend on scheduling.
// hq = false: the 1x chain (ADAA order 1); hq = true: the oversampled chain (ADAA order 2, 4x path).
inline std::array<CalTable, shaper::numTypes> buildCalibration (bool hq)
{
    std::array<CalTable, shaper::numTypes> t;
    std::vector<std::thread> threads;
    for (int i = 1; i < shaper::numTypes; ++i)
        threads.emplace_back ([&t, i, hq] { t[(size_t) i] = detail::computeTable (i, hq); });
    t[0] = detail::computeTable (0, hq);
    for (auto& th : threads) th.join();
    return t;
}

inline const std::array<CalTable, shaper::numTypes>& calibration (bool hq)
{
    static const auto eco = buildCalibration (false);
    static const auto oversampled = buildCalibration (true);
    return hq ? oversampled : eco;
}

// Gain staging for one drive setting.
struct DriveSetting
{
    double gInDb = 0.0, gOutDb = 0.0;
    float lsTrim = 0.0f, hsTrim = 0.0f, biasScale = 1.0f;
};

inline DriveSetting driveSetting (const std::array<CalTable, shaper::numTypes>& cal, int type, double driveDb, double opLevelDb, double autoGain) noexcept
{
    // autoGain in 0 .. 1 blends the calibrated staging with a plain drive (+12 dB make-up, unity at 0 %).
    DriveSetting d;
    d.gInDb = driveDb;
    const double U = driveDb + opLevelDb;
    d.biasScale = (float) biasWeight (U);
    const auto& t = cal[(size_t) type];
    const double autoOut = lookup (t.c, U) + (opLevelDb - kCalRefLevel);
    d.gOutDb = -shaper::kDriveMinDb + (autoOut + shaper::kDriveMinDb) * autoGain;
    d.lsTrim = (float) (lookup (t.l, U) * autoGain);
    d.hsTrim = (float) (lookup (t.h, U) * autoGain);
    return d;
}
} // namespace sat
} // namespace nl

#pragma once

#include "../TestUtil.h"
#include "dsp/CompModule.h"
#include "dsp/DeEsserModule.h"
#include "dsp/GateModule.h"
#include <functional>
#include <juce_dsp/juce_dsp.h>

// Measurements shared by the dynamics tests (tools/tests/TestDynamics.cpp) and the `dynamics`
// measurement suite (tools/measure/MeasureDynamics.cpp). Tests assert thresholds on these
// numbers; the suite reports them.
namespace tool::dk
{
constexpr double kFs = 48000.0;
constexpr int kBlockSizes[] { 1, 7, 32, 37, 64, 511, 512, 2048 };

inline float lin (float db) { return juce::Decibels::decibelsToGain (db); }

template <class M, class Params>
std::unique_ptr<M> makeModule (const Params& p, double fs = kFs)
{
    auto m = std::make_unique<M>();
    m->prepare (fs, 4096);
    m->setParams (p);
    m->reset();
    return m;
}

template <class M>
juce::AudioBuffer<float> runModule (M& m, const juce::AudioBuffer<float>& in, int block, const juce::AudioBuffer<float>* key = nullptr)
{
    juce::ScopedNoDenormals nd;
    juce::AudioBuffer<float> out (in);
    const int n = in.getNumSamples();
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        float* io[2] { out.getWritePointer (0) + pos, out.getWritePointer (1) + pos };
        const float* k[2] { nullptr, nullptr };
        if (key != nullptr)
        {
            k[0] = key->getReadPointer (0) + pos;
            k[1] = key->getReadPointer (juce::jmin (1, key->getNumChannels() - 1)) + pos;
        }
        m.process (io, 2, len, key != nullptr ? k : nullptr);
    }
    return out;
}

inline float peakDbRange (const juce::AudioBuffer<float>& b, int from, int to, int ch = 0)
{
    float m = 0.0f;
    for (int i = from; i < juce::jmin (to, b.getNumSamples()); ++i) m = std::max (m, std::abs (b.getSample (ch, i)));
    return juce::Decibels::gainToDecibels (m, -200.0f);
}

inline double lufsOf (const juce::AudioBuffer<float>& b, double fs, int from)
{
    LoudnessMeter m;
    m.prepare (fs);
    std::vector<StereoSample> s;
    for (int i = from; i < b.getNumSamples(); ++i) s.push_back ({ b.getSample (0, i), b.getSample (1, i) });
    m.process (s.data(), s.size(), 2);
    return m.getIntegrated();
}

// 10-90% time of a monotone step response, in ms (linear interpolation between samples).
inline double riseMs (const std::vector<float>& y, float y0, float y1, double fs)
{
    auto cross = [&] (double frac)
    {
        for (size_t i = 0; i < y.size(); ++i)
        {
            const double f = (y[i] - y0) / (y1 - y0);
            if (f >= frac)
            {
                if (i == 0) return 0.0;
                const double fp = (y[i - 1] - y0) / (y1 - y0);
                return (double) i - 1.0 + (frac - fp) / std::max (1.0e-12, f - fp);
            }
        }
        return (double) y.size();
    };
    return (cross (0.9) - cross (0.1)) * 1000.0 / fs;
}

//==============================================================================
// COMP
//==============================================================================
inline CompParams withThresh (CompParams p, float thresh, float ratio) { p.thresh = thresh; p.ratio = ratio; return p; }

// Sample-by-sample gain-reduction trajectory (dB) after a DC step.
inline std::vector<float> compGrStep (CompModule& m, float dcBefore, float dcAfter, int settle, int n, float& y0)
{
    float l, r;
    float* io[2] { &l, &r };
    juce::ScopedNoDenormals nd;
    for (int i = 0; i < settle; ++i) { l = r = dcBefore; m.process (io, 2, 1, nullptr); }
    y0 = m.getGainReductionDb();
    std::vector<float> y ((size_t) n);
    for (int i = 0; i < n; ++i) { l = r = dcAfter; m.process (io, 2, 1, nullptr); y[(size_t) i] = m.getGainReductionDb(); }
    return y;
}

// Attack: a 10 dB step (-30 -> -20 dB) with the threshold at -40 dB, hard knee, 4:1.
inline double measureCompAttackMs (int type, double fs, float attackMs)
{
    CompParams cp;
    cp.type = type; cp.thresh = -40; cp.ratio = 4; cp.knee = 0; cp.attackMs = attackMs; cp.releaseMs = 2000;
    auto m = makeModule<CompModule> (cp, fs);
    float y0;
    const auto y = compGrStep (*m, lin (-30.0f), lin (-20.0f), (int) (fs * 1.5), (int) (fs * 0.001 * attackMs * 16 + 400), y0);
    return riseMs (y, y0, y.back(), fs);
}

// Release: 30 dB step down to a level below the threshold.
inline double measureCompReleaseMs (int type, double fs, float releaseMs, bool autoRel = false)
{
    CompParams cp;
    cp.type = type; cp.thresh = -40; cp.ratio = 4; cp.knee = 0; cp.attackMs = 1; cp.releaseMs = releaseMs; cp.autoRel = autoRel;
    auto m = makeModule<CompModule> (cp, fs);
    float y0;
    const auto y = compGrStep (*m, lin (-20.0f), lin (-50.0f), (int) (fs * 3.0), (int) (fs * 0.001 * releaseMs * 14 + 800), y0);
    return riseMs (y, y0, y.back(), fs);
}

// Release after `loudMs` of compression (type VCA unless given), 10-90% of the recovery in ms.
inline double measureCompAutoRelease (double loudMs, bool autoRel, int type = CompModule::VCA, double fs = kFs)
{
    CompParams cp;
    cp.type = type; cp.thresh = -30; cp.ratio = 4; cp.knee = 0; cp.attackMs = 1; cp.releaseMs = 100; cp.autoRel = autoRel;
    auto m = makeModule<CompModule> (cp, fs);
    float l, r;
    float* io[2] { &l, &r };
    juce::ScopedNoDenormals nd;
    for (int i = 0; i < (int) (fs * loudMs * 0.001); ++i) { l = r = lin (-10.0f); m.get()->process (io, 2, 1, nullptr); }
    const float y0 = m->getGainReductionDb();
    std::vector<float> y;
    for (int i = 0; i < (int) (fs * 8.0); ++i) { l = r = lin (-60.0f); m->process (io, 2, 1, nullptr); y.push_back (m->getGainReductionDb()); }
    return riseMs (y, y0, 0.0f, fs);
}

inline double specCompGain (double L, double T, double R, double W)
{
    const double over = L - T;
    if (2.0 * over < -W) return 0.0;
    if (W > 0.0 && 2.0 * std::abs (over) <= W) return (1.0 / R - 1.0) * (over + 0.5 * W) * (over + 0.5 * W) / (2.0 * W);
    return (1.0 / R - 1.0) * over;
}

// Largest deviation (dB, signed) of the steady-state static curve from the knee formula.
inline double staticCurveError (int type, float knee, float ratio, float thresh)
{
    CompParams cp;
    cp.type = type; cp.thresh = thresh; cp.ratio = ratio; cp.knee = knee;
    auto m = makeModule<CompModule> (cp);
    double worst = 0.0;
    for (float L = -60.0f; L <= 0.0f; L += 3.0f)
    {
        auto in = sine (kFs, 1000.0, lin (L), 36000);
        auto out = runModule (*m, in, 256);
        const double meas = peakDbRange (out, 33600, 36000) - L;
        const double err = meas - specCompGain (L, thresh, ratio, knee);
        if (std::abs (err) > std::abs (worst)) worst = err;
    }
    return worst;
}

// Loudness error (LU) of pink noise at the Operating Level through the compressor with auto make-up.
inline double makeupErrorLu (int type, float ratio, float thresh, double fs, float attackMs = 10.0f, float releaseMs = 150.0f)
{
    CompParams cp;
    cp.type = type; cp.ratio = ratio; cp.thresh = thresh; cp.makeup = true; cp.opLevelDb = -18.0f; cp.attackMs = attackMs; cp.releaseMs = releaseMs;
    auto m = makeModule<CompModule> (cp, fs);
    const int n = (int) (fs * 8.0);
    auto in = pinkNoise (n, -18.0f, 7);
    auto out = runModule (*m, in, 512);
    return lufsOf (out, fs, (int) (fs * 3.0)) - lufsOf (in, fs, (int) (fs * 3.0));
}

//==============================================================================
// Spectra
//==============================================================================
inline std::vector<double> powerSpectrum (const juce::AudioBuffer<float>& b, int ch, int from, int N, int win = 0)
{
    if (win <= 0) win = N;
    int order = 0;
    while ((1 << order) < N) ++order;
    juce::dsp::FFT fft (order);
    std::vector<float> d ((size_t) 2 * (size_t) N, 0.0f);
    for (int i = 0; i < win && from + i < b.getNumSamples(); ++i)
        d[(size_t) i] = b.getSample (ch, from + i) * (0.5f - 0.5f * (float) std::cos (2.0 * kPiD * i / win));
    fft.performRealOnlyForwardTransform (d.data(), true);
    std::vector<double> p ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) p[(size_t) k] = (double) d[(size_t) 2 * k] * d[(size_t) 2 * k] + (double) d[(size_t) 2 * k + 1] * d[(size_t) 2 * k + 1];
    return p;
}

// Energy outside the true harmonics of a bin-exact tone, relative to the total (dB).
inline double aliasDb (const juce::AudioBuffer<float>& b, int ch, int from, int N, int bin)
{
    const auto p = powerSpectrum (b, ch, from, N);
    double total = 0.0, alias = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        total += p[(size_t) k];
        bool harmonic = false;
        for (int h = 1; h * bin < N / 2; ++h) harmonic |= std::abs (k - h * bin) <= 3;
        if (! harmonic && k > 4) alias += p[(size_t) k];
    }
    return 10.0 * std::log10 (alias / total + 1.0e-30);
}

inline double harmonicDb (const juce::AudioBuffer<float>& b, int ch, int from, int N, int bin, int h)
{
    const auto p = powerSpectrum (b, ch, from, N);
    double f1 = 0.0, fh = 0.0;
    for (int k = -3; k <= 3; ++k) { f1 += p[(size_t) (bin + k)]; if (h * bin + k < N / 2) fh += p[(size_t) (h * bin + k)]; }
    return 10.0 * std::log10 (fh / f1 + 1.0e-30);
}

// Switching splatter: energy above `cutoffHz` in a Hann-windowed window, relative to the energy of
// the carrier band (|f - f0| <= max (0.4 f0, 150 Hz)), in dB.
inline float splatterDb (const juce::AudioBuffer<float>& b, int ch, int from, int n, double fs, double f0, double cutoffHz)
{
    const int N = 8192;
    const auto p = powerSpectrum (b, ch, from, N, n); // window of n samples, zero padded
    double carrier = 0.0, spl = 0.0;
    const double band = std::max (0.4 * f0, 150.0);
    for (int k = 1; k <= N / 2; ++k)
    {
        const double f = k * fs / N;
        if (std::abs (f - f0) <= band) carrier += p[(size_t) k];
        else if (f > cutoffHz) spl += p[(size_t) k];
    }
    return (float) (10.0 * std::log10 (spl / std::max (carrier, 1.0e-30) + 1.0e-30));
}

// Largest sample-to-sample step of the output over the window relative to the same measure of the
// input (the signal's own slope).
inline float stepRatio (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int ch, int from, int n)
{
    float so = 1.0e-9f, si = 1.0e-9f;
    for (int i = from + 1; i < from + n && i < out.getNumSamples(); ++i)
    {
        so = std::max (so, std::abs (out.getSample (ch, i) - out.getSample (ch, i - 1)));
        si = std::max (si, std::abs (in.getSample (ch, i) - in.getSample (ch, i - 1)));
    }
    return so / si;
}

//==============================================================================
// GATE
//==============================================================================
inline double specExpanderGain (double L, double T, double R, double W, double floorDb)
{
    const double under = T - L, s = R - 1.0;
    double g;
    if (2.0 * under < -W) g = 0.0;
    else if (W > 0.0 && 2.0 * std::abs (under) <= W) g = -s * (under + 0.5 * W) * (under + 0.5 * W) / (2.0 * W);
    else g = -s * under;
    return std::max (g, floorDb);
}

struct ExpanderSweep { double maxJump = 0.0, maxErr = 0.0, checksum = 0.0; };

// Steady-state transfer curve of the expander, swept upwards in 0.25 dB steps.
inline ExpanderSweep expanderSweep (float ratio, float knee, float hyst)
{
    GateParams gp;
    gp.type = GateModule::Expander; gp.thresh = -40; gp.ratio = ratio; gp.knee = knee; gp.floorDb = -60; gp.hyst = hyst;
    gp.attackMs = 1; gp.releaseMs = 5; gp.holdMs = 0;
    auto m = makeModule<GateModule> (gp);
    ExpanderSweep r;
    double prev = 0.0;
    bool first = true;
    for (float L = -75.0f; L <= -20.0f; L += 0.25f)
    {
        auto in = sine (kFs, 1000.0, lin (L), 2400);
        auto out = runModule (*m, in, 128);
        const double g = peakDbRange (out, 1920, 2400) - L;
        r.maxErr = std::max (r.maxErr, std::abs (g - specExpanderGain (L, -40.0, ratio, knee, -60.0)));
        if (! first) r.maxJump = std::max (r.maxJump, std::abs (g - prev));
        r.checksum += g;
        prev = g;
        first = false;
    }
    r.checksum /= 221.0;
    return r;
}

// Energy loss (dB, positive = lost) in the first 5 ms of a kick with a click, gate closed before.
inline float kickLossDb (int lookaheadChoice)
{
    GateParams gp;
    gp.thresh = -40; gp.hyst = 4; gp.floorDb = -80; gp.attackMs = 0.5f; gp.holdMs = 100; gp.releaseMs = 200; gp.lookahead = lookaheadChoice;
    auto m = makeModule<GateModule> (gp);
    const int k0 = 48000, n = 48000 + 9600;
    juce::AudioBuffer<float> in (2, n);
    in.clear();
    juce::Random rng (3);
    for (int i = 0; i < n - k0; ++i)
    {
        const double t = i / kFs;
        const float v = (float) (0.8 * std::exp (-t / 0.0008) * (rng.nextFloat() * 2.0f - 1.0f) + 0.6 * std::sin (2.0 * kPiD * 60.0 * t) * std::exp (-t / 0.06));
        in.setSample (0, k0 + i, v); in.setSample (1, k0 + i, v);
    }
    auto out = runModule (*m, in, 64);
    const int lat = m->latency(), w = (int) (0.005 * kFs);
    return rmsDbRange (in, k0, k0 + w) - rmsDbRange (out, k0 + lat, k0 + lat + w);
}

struct OpeningClick { float splatterDb, stepRatio; };

// A closed gate (-60 dB) opened by an external key, passing a steady 1 kHz tone.
inline OpeningClick gateOpeningClick (float attackMs)
{
    GateParams gp;
    gp.ext = true; gp.thresh = -30; gp.floorDb = -60; gp.attackMs = attackMs; gp.holdMs = 50; gp.releaseMs = 100;
    auto m = makeModule<GateModule> (gp);
    const int n = 48000, onset = 24000;
    auto main = sine (kFs, 1000.0, lin (-10.0f), n);
    juce::AudioBuffer<float> key (2, n);
    key.clear();
    for (int i = onset; i < n; ++i)
    {
        const float v = lin (-10.0f) * (float) std::sin (2.0 * kPiD * 100.0 * (i - onset) / kFs);
        key.setSample (0, i, v); key.setSample (1, i, v);
    }
    auto out = runModule (*m, main, 128, &key);
    return { splatterDb (out, 0, onset - 600, 2400, kFs, 1000.0, 8000.0), stepRatio (out, main, 0, onset - 600, 2400) };
}

//==============================================================================
// DE-ESS
//==============================================================================
struct Vocal
{
    juce::AudioBuffer<float> buf;
    std::vector<std::pair<int, int>> sibilants;
};

// Synthetic vocal: a voiced harmonic tone with a syllable envelope plus 150 ms bursts of 4 kHz
// high-passed noise every 500 ms.
inline Vocal vocalLike (double fs, double seconds, int seed)
{
    Vocal v;
    const int n = (int) (fs * seconds);
    v.buf.setSize (2, n);
    juce::Random rng (seed);
    auto hp = makeSvf (SvfType::HighPass, fs, 4000.0f, 0.7071f);
    SvfState st;
    double phase = 0.0;
    std::vector<float> voiced ((size_t) n), noise ((size_t) n);
    double vAcc = 0.0, nAcc = 0.0, nCount = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        double s = 0.0;
        for (int h = 1; h <= 26; ++h) s += std::sin (phase * h) / h;
        phase += 2.0 * kPiD * 140.0 / fs;
        const double env = 0.55 + 0.45 * std::sin (2.0 * kPiD * 2.5 * t);
        voiced[(size_t) i] = (float) (s * env);
        vAcc += voiced[(size_t) i] * voiced[(size_t) i];
        const float w = st.process (rng.nextFloat() * 2.0f - 1.0f, hp);
        const double tt = std::fmod (t, 0.5);
        double burst = 0.0;
        if (tt >= 0.25 && tt < 0.40) burst = std::min ({ 1.0, (tt - 0.25) / 0.005, (0.40 - tt) / 0.005 });
        noise[(size_t) i] = (float) (w * burst);
        if (burst > 0.99) { nAcc += (double) noise[(size_t) i] * noise[(size_t) i]; nCount += 1.0; }
    }
    const float gv = lin (-22.0f) / (float) std::sqrt (vAcc / n), gn = lin (-14.0f) / (float) std::sqrt (nAcc / std::max (1.0, nCount));
    for (int i = 0; i < n; ++i)
    {
        const float x = voiced[(size_t) i] * gv + noise[(size_t) i] * gn;
        v.buf.setSample (0, i, x); v.buf.setSample (1, i, x);
    }
    for (double t0 = 0.25; t0 + 0.4 < seconds; t0 += 0.5) v.sibilants.push_back ({ (int) ((t0 + 0.03) * fs), (int) ((t0 + 0.13) * fs) });
    return v;
}

struct DsSpectrum { float grDb, outsideDb; };

// Gain reduction in the sibilants and the change of the 100 Hz - 1.5 kHz level (outside a 6 kHz
// high-shelf band) during them, for Split (0) or Wide (1).
inline DsSpectrum deEssSpectrum (int mode)
{
    DsParams dp;
    dp.relative = false;
    dp.band[0].mode = mode; dp.band[0].type = 3; dp.band[0].freq = 6000; dp.band[0].thresh = -30; dp.band[0].range = 12;
    dp.band[1].on = false;
    auto m = makeModule<DeEsserModule> (dp);
    const auto voc = vocalLike (kFs, 3.0, 5);
    juce::AudioBuffer<float> out (voc.buf);
    float gr = 0.0f;
    int grCount = 0;
    for (int pos = 0; pos < out.getNumSamples(); pos += 64)
    {
        const int len = juce::jmin (64, out.getNumSamples() - pos);
        float* io[2] { out.getWritePointer (0) + pos, out.getWritePointer (1) + pos };
        m->process (io, 2, len, nullptr);
        for (const auto& s : voc.sibilants)
            if (pos >= s.first && pos < s.second) { gr += m->getGainReductionDb (0); ++grCount; }
    }
    double pin = 0.0, pout = 0.0;
    const int N = 2048;
    for (const auto& s : voc.sibilants)
        for (int f = s.first; f + N <= s.second; f += N / 2)
        {
            const auto a = powerSpectrum (voc.buf, 0, f, N), b = powerSpectrum (out, 0, f, N);
            for (int k = (int) (100.0 * N / kFs); k <= (int) (1500.0 * N / kFs); ++k) { pin += a[(size_t) k]; pout += b[(size_t) k]; }
        }
    return { gr / (float) std::max (1, grCount), (float) (10.0 * std::log10 (pout / pin)) };
}

inline std::vector<float> dsGrStep (DeEsserModule& m, float ampBefore, float ampAfter, double freq, int settle, int n, float& y0)
{
    float l, r;
    float* io[2] { &l, &r };
    juce::ScopedNoDenormals nd;
    int i = 0;
    for (; i < settle; ++i) { l = r = ampBefore * (float) std::sin (2.0 * kPiD * freq * i / kFs); m.process (io, 2, 1, nullptr); }
    y0 = m.getGainReductionDb (0);
    std::vector<float> y ((size_t) n);
    for (int k = 0; k < n; ++k, ++i) { l = r = ampAfter * (float) std::sin (2.0 * kPiD * freq * i / kFs); m.process (io, 2, 1, nullptr); y[(size_t) k] = m.getGainReductionDb (0); }
    return y;
}

inline double measureDsAttackMs (int mode, float attackMs, float releaseMs)
{
    DsParams dp;
    dp.relative = false; dp.attackMs = attackMs; dp.releaseMs = releaseMs;
    dp.band[0].mode = mode; dp.band[0].type = 3; dp.band[0].freq = 6000; dp.band[0].thresh = -40; dp.band[0].range = 24;
    dp.band[1].on = false;
    auto m = makeModule<DeEsserModule> (dp);
    float y0;
    const auto y = dsGrStep (*m, lin (-30.0f), lin (-20.0f), 8000.0, 48000, (int) (kFs * 0.001 * attackMs * 16 + 400), y0);
    return riseMs (y, y0, y.back(), kFs);
}

inline double measureDsReleaseMs (float releaseMs)
{
    DsParams dp;
    dp.relative = false; dp.attackMs = 0.5f; dp.releaseMs = releaseMs;
    dp.band[0].type = 3; dp.band[0].freq = 6000; dp.band[0].thresh = -40; dp.band[0].range = 24;
    dp.band[1].on = false;
    auto m = makeModule<DeEsserModule> (dp);
    float y0;
    const auto y = dsGrStep (*m, lin (-20.0f), lin (-50.0f), 8000.0, 48000, (int) (kFs * 0.001 * releaseMs * 14 + 800), y0);
    return riseMs (y, y0, 0.0f, kFs);
}

//==============================================================================
// Determinism helpers
//==============================================================================
// Voice-like program with sibilants, noise and level changes so that every module is active.
inline juce::AudioBuffer<float> programMaterial (int n, int seed)
{
    const auto voc = vocalLike (kFs, n / kFs + 0.5, seed);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (seed + 1);
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i / kFs);
        const float lvl = 0.25f + 0.75f * (0.5f + 0.5f * (float) std::sin (2.0 * kPiD * 0.9 * t));
        const float x = voc.buf.getSample (0, i) * lvl * 3.0f + 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
        b.setSample (0, i, x);
        b.setSample (1, i, 0.8f * x + 0.01f * (rng.nextFloat() * 2.0f - 1.0f));
    }
    return b;
}

// Automation that every block size in kBlockSizes observes inside the same control tick.
// renderWithAutomation applies an event at the start of the block that contains it, which for
// block sizes that do not divide the event position (7, 37, 511) is up to bs-1 samples early.
// A parameter change can only be identical across block sizes if all of them see it within the
// same 32-sample tick interval, so the events sit at multiples of 2048 where that holds for
// every block size. Each event moves every listed parameter to a new random value.
inline std::vector<int> safeEventSamples (int maxSample)
{
    std::vector<int> e;
    for (int m = 1; m * 2048 < maxSample; ++m)
    {
        bool ok = true;
        for (int bs : kBlockSizes) ok &= (m * 2048 - (m * 2048 / bs) * bs) < kControlBlock;
        if (ok) e.push_back (m * 2048);
    }
    return e;
}

inline std::vector<AutomationEvent> randomAutomation (const std::vector<int>& params, int maxSample, int seed)
{
    std::vector<AutomationEvent> ev;
    juce::Random rng (seed);
    for (int sample : safeEventSamples (maxSample))
        for (int p : params)
        {
            const auto& d = paramDef (p);
            float v = d.minV + rng.nextFloat() * (d.maxV - d.minV);
            if (d.kind == ParamDef::Choice) v = (float) juce::roundToInt (v);
            if (d.kind == ParamDef::Bool) v = rng.nextBool() ? 1.0f : 0.0f;
            // ST <-> DUAL is the modules' business; the M/S matrix around a module belongs to the strip.
            if (p == P::compMode || p == P::gateMode || p == P::dsMode) v = std::min (1.0f, v);
            ev.push_back ({ sample, p, v });
        }
    return ev;
}
} // namespace tool::dk

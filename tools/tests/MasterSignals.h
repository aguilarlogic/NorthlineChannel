#pragma once

// Test signals and analysis helpers for the master section (limiter and MeterHub).
// Shared by tools/tests/TestMaster.cpp and tools/measure/MeasureMaster.cpp.

#include "../TestUtil.h"
#include "dsp/MasterSection.h"
#include <complex>
#include <functional>
#include <limits>

namespace tool::master
{
using Buf = juce::AudioBuffer<float>;

inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
inline double linToDb (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

// A level in dBFS (or silence when -inf) held for a duration.
struct Seg { double db; double seconds; };

// Stereo in-phase 1 kHz sine, peak level in dBFS. Tones in a sequence are phase continuous.
inline Buf tones (double fs, const std::vector<Seg>& segs, double freq = 1000.0)
{
    int total = 0;
    for (auto& s : segs) total += (int) std::llround (s.seconds * fs);
    Buf b (2, total);
    b.clear();
    int pos = 0;
    for (auto& s : segs)
    {
        const int n = (int) std::llround (s.seconds * fs);
        if (std::isfinite (s.db))
        {
            const double a = dbToLin (s.db);
            for (int i = 0; i < n; ++i)
            {
                const float v = (float) (a * std::sin (2.0 * kPiD * freq * (double) (pos + i) / fs));
                b.setSample (0, pos + i, v);
                b.setSample (1, pos + i, v);
            }
        }
        pos += n;
    }
    return b;
}

constexpr double kSilence = -std::numeric_limits<double>::infinity();

inline Buf concat (const std::vector<Buf>& parts)
{
    int total = 0;
    for (auto& p : parts) total += p.getNumSamples();
    Buf b (2, total);
    int pos = 0;
    for (auto& p : parts)
    {
        for (int c = 0; c < 2; ++c) b.copyFrom (c, pos, p, c, 0, p.getNumSamples());
        pos += p.getNumSamples();
    }
    return b;
}

// Runs a buffer through a MeterHub in blocks of `block` samples.
inline void feed (MeterHub& hub, const Buf& b, int block, int from = 0, int to = -1)
{
    if (to < 0 || to > b.getNumSamples()) to = b.getNumSamples(); // never read past the buffer
    const float* ch[2] { b.getReadPointer (0), b.getReadPointer (1) };
    for (int pos = from; pos < to; pos += block)
    {
        const int len = std::min (block, to - pos);
        const float* p[2] { ch[0] + pos, ch[1] + pos };
        hub.process (p, 2, len);
    }
}

inline std::unique_ptr<MeterHub> makeHub (double fs, int maxBlock = 512)
{
    auto h = std::make_unique<MeterHub>();
    h->prepare (fs, maxBlock);
    return h;
}

// Reference true peak: 16x Kaiser-sinc interpolation with 64 taps per phase (far more accurate than
// the 4x meter). dB re full scale; channel 0.
inline double refTruePeakDb (const Buf& b, int ch = 0, int from = 0)
{
    constexpr int P = 16, T = 64;
    static std::vector<std::vector<double>> h;
    if (h.empty())
    {
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 60; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double beta = 9.0, b0 = bessel0 (beta);
        h.assign (P, std::vector<double> (T));
        for (int p = 0; p < P; ++p)
        {
            double sum = 0;
            for (int j = 0; j < T; ++j)
            {
                const double u = (double) p / P + (T / 2 - 1) - j;
                const double s = std::abs (u) < 1e-12 ? 1.0 : std::sin (kPiD * u) / (kPiD * u);
                const double r = u / (T / 2.0);
                h[(size_t) p][(size_t) j] = s * (std::abs (r) >= 1 ? 0.0 : bessel0 (beta * std::sqrt (1 - r * r)) / b0);
                sum += h[(size_t) p][(size_t) j];
            }
            for (auto& v : h[(size_t) p]) v /= sum;
        }
    }
    const int n = b.getNumSamples();
    const float* x = b.getReadPointer (ch);
    double best = 0.0, sampleMax = 0.0;
    for (int i = from; i < n; ++i) sampleMax = std::max (sampleMax, (double) std::abs (x[i]));
    const double gate = 0.4 * sampleMax; // only interpolate near the large samples
    for (int k = std::max (from, T); k < n - T; ++k)
    {
        double local = 0;
        for (int d = -2; d <= 2; ++d) local = std::max (local, (double) std::abs (x[k + d]));
        if (local < gate) continue;
        for (int p = 0; p < P; ++p)
        {
            double a = 0;
            for (int j = 0; j < T; ++j) a += h[(size_t) p][(size_t) j] * x[k - (T / 2 - 1) + j];
            best = std::max (best, std::abs (a));
        }
    }
    return linToDb (best);
}

// The 4x BS.1770 meter on an offline buffer (from the MeterHub).
inline double meterTruePeakDb (const Buf& b, double fs, int block = 512)
{
    auto hub = makeHub (fs);
    feed (*hub, b, block);
    return hub->read().truePeakDb;
}

// Magnitude of the DFT bin of x[start .. start+N) at `bin` (rectangular window; use coherent lengths).
inline double dftMag (const float* x, int N, int bin)
{
    double re = 0, im = 0;
    for (int i = 0; i < N; ++i)
    {
        const double ph = 2.0 * kPiD * (double) bin * i / N;
        re += x[i] * std::cos (ph);
        im -= x[i] * std::sin (ph);
    }
    return std::sqrt (re * re + im * im) * 2.0 / N;
}

// THD (dB re fundamental), harmonics 2..10. The window holds an integer number of periods:
// fundamentalBin = freq * N / fs must be an integer.
inline double thdDb (const Buf& b, int start, int N, int fundamentalBin)
{
    const float* x = b.getReadPointer (0) + start;
    const double f = dftMag (x, N, fundamentalBin);
    double h = 0;
    for (int k = 2; k <= 10 && k * fundamentalBin < N / 2; ++k)
    {
        const double m = dftMag (x, N, k * fundamentalBin);
        h += m * m;
    }
    return linToDb (std::sqrt (h) / f);
}

// Limiter test material, all stereo, normalised to a stated peak level.
inline Buf scaledPeak (Buf b, double peakDb)
{
    float pk = 0;
    for (int c = 0; c < 2; ++c) pk = std::max (pk, b.getMagnitude (c, 0, b.getNumSamples()));
    b.applyGain ((float) (dbToLin (peakDb) / std::max (pk, 1e-9f)));
    return b;
}

// Pink noise, decorrelated channels, peak at `peakDb`.
inline Buf pinkStereo (int n, double peakDb, int seed = 3)
{
    auto a = pinkNoise (n, -20.0f, seed), c = pinkNoise (n, -20.0f, seed + 100);
    Buf b (2, n);
    b.copyFrom (0, 0, a, 0, 0, n);
    b.copyFrom (1, 0, c, 0, 0, n);
    return scaledPeak (b, peakDb);
}

// Linear-phase FIR low-pass (Kaiser, 127 taps) in place: real programme material is band-limited by the
// converter's anti-alias filter, white noise up to Nyquist is not a realistic inter-sample-peak test.
inline void lowpassInPlace (Buf& b, double cutoffRatio)
{
    constexpr int taps = 127, half = taps / 2;
    auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 60; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    std::vector<double> h ((size_t) taps);
    double sum = 0;
    for (int j = 0; j < taps; ++j)
    {
        const double u = j - half, r = u / (half + 1);
        const double s = std::abs (u) < 1e-12 ? 2.0 * cutoffRatio : std::sin (2.0 * kPiD * cutoffRatio * u) / (kPiD * u);
        h[(size_t) j] = s * bessel0 (8.0 * std::sqrt (1 - r * r)) / bessel0 (8.0);
        sum += h[(size_t) j];
    }
    const int n = b.getNumSamples();
    std::vector<float> tmp ((size_t) n);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            double a = 0;
            for (int j = 0; j < taps; ++j)
            {
                const int k = i + j - half;
                if (k >= 0 && k < n) a += h[(size_t) j] * x[k];
            }
            tmp[(size_t) i] = (float) (a / sum);
        }
        std::copy (tmp.begin(), tmp.end(), b.getWritePointer (c));
    }
}

// Drum-like material: a decaying noise burst with a 50 Hz body, hits at irregular spacing,
// band-limited to 0.4 fs.
inline Buf drums (double fs, double seconds, double peakDb)
{
    const int n = (int) (seconds * fs);
    Buf b (2, n);
    b.clear();
    juce::Random rng (11);
    int pos = (int) (0.05 * fs);
    int step = 0;
    while (pos < n)
    {
        const double amp = (step % 4 == 0) ? 1.0 : 0.6;
        const int len = (int) (0.18 * fs);
        for (int i = 0; i < len && pos + i < n; ++i)
        {
            const double t = (double) i / fs;
            const double env = std::exp (-t / 0.03);
            const double body = std::sin (2.0 * kPiD * 50.0 * t) * std::exp (-t / 0.08);
            const double click = (rng.nextDouble() * 2.0 - 1.0) * std::exp (-t / 0.004);
            const float v = (float) (amp * (0.6 * body + 0.5 * click + 0.3 * env * (rng.nextDouble() * 2.0 - 1.0)));
            b.addSample (0, pos + i, v);
            b.addSample (1, pos + i, v * (i % 2 ? 0.9f : 1.0f));
        }
        pos += (int) ((0.27 + 0.11 * (step % 3)) * fs);
        ++step;
    }
    lowpassInPlace (b, 0.4);
    return scaledPeak (b, peakDb);
}

// Logarithmic sine sweep 20 Hz to 20 kHz (or 0.45 fs), amplitude `peakDb`.
inline Buf sweep (double fs, double seconds, double peakDb)
{
    const int n = (int) (seconds * fs);
    Buf b (2, n);
    const double f0 = 20.0, f1 = std::min (20000.0, 0.45 * fs), k = std::log (f1 / f0);
    const double a = dbToLin (peakDb);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double ph = 2.0 * kPiD * f0 * seconds / k * (std::exp (t / seconds * k) - 1.0);
        const float v = (float) (a * std::sin (ph));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

//==============================================================================
// A MasterSection driven the way the strip does it: setParams every block, then process.
struct Rig
{
    MasterSection ms;
    ParamSnapshot snap = ParamSnapshot::defaults();
    double fs = 48000.0;

    Rig (double sampleRate, int mode, bool hq, bool on, double ceilingDb = -0.3, double releaseMs = 100.0, int maxBlock = 2048) : fs (sampleRate)
    {
        set (mode, hq, on, ceilingDb, releaseMs);
        ms.prepare (fs, maxBlock);
        ms.reset (snap);
    }

    void set (int mode, bool hq, bool on, double ceilingDb, double releaseMs)
    {
        snap.v[(size_t) P::limMode] = (float) mode;
        snap.v[(size_t) P::quality] = hq ? 1.0f : 0.0f;
        snap.v[(size_t) P::limOn] = on ? 1.0f : 0.0f;
        snap.v[(size_t) P::limCeiling] = (float) ceilingDb;
        snap.v[(size_t) P::limRelease] = (float) releaseMs;
    }
    void setOn (bool on) { snap.v[(size_t) P::limOn] = on ? 1.0f : 0.0f; }
    void setCeiling (double db) { snap.v[(size_t) P::limCeiling] = (float) db; }
    void setRelease (double releaseMs) { snap.v[(size_t) P::limRelease] = (float) releaseMs; }

    // Processes `in` in blocks; `events` (sorted by sample) are applied before the block that
    // contains their sample, like the host would.
    struct Event { int sample; std::function<void (Rig&)> fn; };
    Buf run (const Buf& in, int block, const std::vector<Event>& events = {})
    {
        const int n = in.getNumSamples();
        Buf out (2, n);
        Buf work (2, block);
        size_t next = 0;
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = std::min (block, n - pos);
            while (next < events.size() && events[next].sample < pos + len) events[next++].fn (*this);
            for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, c, pos, len);
            ms.setParams (snap);
            float* io[2] { work.getWritePointer (0), work.getWritePointer (1) };
            ms.process (io, 2, len);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, work, c, 0, len);
        }
        return out;
    }
};

inline double maxAbs (const Buf& b, int from = 0, int to = -1, int ch = 0)
{
    if (to < 0) to = b.getNumSamples();
    double m = 0;
    for (int i = from; i < to; ++i) m = std::max (m, (double) std::abs (b.getSample (ch, i)));
    return m;
}

inline bool allFinite (const Buf& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

struct ModeCfg { int mode; bool hq; const char* name; };
inline const std::vector<ModeCfg>& allModes()
{
    static const std::vector<ModeCfg> m { { 0, false, "Safety Clip Eco" }, { 0, true, "Safety Clip HQ" }, { 1, false, "Brickwall" }, { 2, false, "True Peak" } };
    return m;
}

//==============================================================================
// EBU Tech 3341 true-peak signals 15..23 (stereo). Returns {signal, nominal dBTP}.
struct TpCase { juce::String name; Buf signal; double nominalDb; };

inline std::vector<TpCase> ebuTruePeakCases (double fs)
{
    std::vector<TpCase> out;
    const int n = (int) (fs * 0.5), fade = (int) (fs * 0.01);
    auto taper = [&] (Buf& b)
    {
        for (int i = 0; i < fade; ++i)
        {
            const float g = (float) (0.5 - 0.5 * std::cos (kPiD * i / fade));
            for (int c = 0; c < 2; ++c)
            {
                b.setSample (c, i, b.getSample (c, i) * g);
                b.setSample (c, n - 1 - i, b.getSample (c, n - 1 - i) * g);
            }
        }
    };
    auto simple = [&] (const char* name, double fRatio, double amp, double phaseDeg, double nominal)
    {
        Buf b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = (float) (amp * std::sin (2.0 * kPiD * fRatio * i + phaseDeg * kPiD / 180.0));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        taper (b);
        out.push_back ({ name, std::move (b), nominal });
    };
    simple ("15 fs/4, 0.50 FFS, 0 deg", 0.25, 0.5, 0.0, -6.0);
    simple ("16 fs/4, 0.50 FFS, 45 deg", 0.25, 0.5, 45.0, -6.0);
    simple ("17 fs/6, 0.50 FFS, 60 deg", 1.0 / 6.0, 0.5, 60.0, -6.0);
    simple ("18 fs/8, 0.50 FFS, 67.5 deg", 1.0 / 8.0, 0.5, 67.5, -6.0);
    simple ("19 fs/4, 1.41 FFS, 45 deg", 0.25, 1.41, 45.0, 3.0);

    // 20..23: synthesised at 4 fs, one period of fs/4 at amplitude 1.0 inside an fs/6 tone at 0.5,
    // low-pass filtered (Kaiser-windowed sinc) and decimated by 4 with offset 0..3.
    const int n4 = n * 4;
    std::vector<double> x4 ((size_t) n4);
    const int burstStart = n4 / 2 - (n4 / 2) % 16 + 2; // peak of the burst sits on a 4 fs sample
    double phase6 = 0.0;
    for (int i = 0; i < n4; ++i)
    {
        const bool inBurst = i >= burstStart && i < burstStart + 16;
        if (inBurst) x4[(size_t) i] = std::sin (kPiD / 2.0 + 2.0 * kPiD * (i - burstStart) / 16.0);
        else x4[(size_t) i] = 0.5 * std::sin (phase6);
        phase6 += 2.0 * kPiD / 24.0;
    }
    const int taps = 255, half = taps / 2;
    std::vector<double> lp ((size_t) taps);
    {
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 60; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double fc = 0.5 / 4.0, beta = 10.0, b0 = bessel0 (beta); // cutoff fs/2, as a fraction of 4 fs
        double sum = 0;
        for (int j = 0; j < taps; ++j)
        {
            const double u = j - half;
            const double s = std::abs (u) < 1e-12 ? 2.0 * fc : std::sin (2.0 * kPiD * fc * u) / (kPiD * u);
            const double r = u / half;
            lp[(size_t) j] = s * bessel0 (beta * std::sqrt (1 - r * r)) / b0;
            sum += lp[(size_t) j];
        }
        for (auto& v : lp) v /= sum;
    }
    for (int off = 0; off < 4; ++off)
    {
        Buf b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const int centre = i * 4 + off;
            double a = 0;
            for (int j = 0; j < taps; ++j)
            {
                const int k = centre + j - half;
                if (k >= 0 && k < n4) a += lp[(size_t) j] * x4[(size_t) k];
            }
            b.setSample (0, i, (float) a);
            b.setSample (1, i, (float) a);
        }
        taper (b);
        // Our own anti-alias filter rings differently from the EBU file, so set the level that a
        // perfect meter reads as 0.0 dBTP (the nominal value of cases 20 to 23).
        b.applyGain ((float) dbToLin (-refTruePeakDb (b, 0)));
        out.push_back ({ "2" + juce::String (off) + " burst, offset " + juce::String (off), std::move (b), 0.0 });
    }
    return out;
}
} // namespace tool::master

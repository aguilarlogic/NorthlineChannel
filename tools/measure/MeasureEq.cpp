#include "../EqHelpers.h"
#include "Measure.h"
#include <array>
#include <chrono>

// EQ measurement suite: `NorthlineTool measure eq [--quick] [--out docs/measurements]`.
// Reports numbers only; the thresholds are asserted in tools/tests/TestEq.cpp. Definitions and
// method: docs/dsp/eq.md.

using namespace eqtest;
using tool::fmt;

namespace
{
using Clock = std::chrono::steady_clock;

double microsSince (Clock::time_point t0) noexcept
{
    return std::chrono::duration<double, std::micro> (Clock::now() - t0).count();
}

juce::StringArray cells (const std::vector<juce::String>& v)
{
    juce::StringArray s;
    for (const auto& x : v) s.add (x);
    return s;
}

void emitTable (tool::MeasureReport& r, const std::vector<juce::String>& header, const std::vector<std::vector<juce::String>>& rows)
{
    const auto h = cells (header);
    r.text << "| " << h.joinIntoString (" | ") << " |\n|";
    for (int i = 0; i < h.size(); ++i) r.text << "---|";
    r.text << "\n";
    for (const auto& row : rows) r.text << "| " << cells (row).joinIntoString (" | ") << " |\n";
    r.text << "\n";
}

// Error statistics over a set of cases (dB).
struct Acc
{
    double maxErr = 0.0, sumSq = 0.0, wFc = 0.0, wGain = 0.0, wQ = 0.0;
    long n = 0;

    void add (double err, double fc, double gain, double q) noexcept
    {
        sumSq += err * err;
        ++n;
        if (err > maxErr) { maxErr = err; wFc = fc; wGain = gain; wQ = q; }
    }
};

double combinedRms (const Acc& a, const Acc& b) noexcept
{
    const long n = a.n + b.n;
    return n > 0 ? std::sqrt ((a.sumSq + b.sumSq) / (double) n) : 0.0;
}

// Probes: log-spaced from 20 Hz to 0.9 Nyquist, plus points around fc out to its half-gain
// bandwidth, where the error peaks. `quick` thins both sets.
std::vector<double> probeFreqs (double fs, double fc, double qEff, bool quick)
{
    const double fmax = 0.9 * 0.5 * fs;
    const double rr = std::min (8.0, std::sqrt (1.0 + 0.25 / (qEff * qEff)) + 0.5 / qEff);
    const int nLog = quick ? 150 : 400, nNear = quick ? 25 : 81;
    std::vector<double> f;
    for (int i = 0; i < nLog; ++i) f.push_back (20.0 * std::pow (fmax / 20.0, i / (double) (nLog - 1)));
    for (int i = 0; i < nNear; ++i)
    {
        const double x = fc * std::pow (rr, 4.0 * i / (nNear - 1) - 2.0);
        if (x >= 20.0 && x <= fmax) f.push_back (x);
    }
    return f;
}

struct TypeDef
{
    const char* name;
    const char* shortName;
    int band, type;
    bool sweepQ, prop;
};

// Band 0 = low, 1 = mid, 3 = high. Shelves have a fixed Q per type; the parametric bell takes
// the user Q, the Proportional row adds the console Q law (eff. Q up to 25 at +-18 dB).
const TypeDef kTypes[] {
    { "Low shelf (Q 0.71)", "Low shelf", 0, 0, false, false },
    { "Low resonant shelf (Q 1.5)", "Low res. shelf", 0, 1, false, false },
    { "High shelf (Q 0.71)", "High shelf", 3, 0, false, false },
    { "High resonant shelf (Q 1.5)", "High res. shelf", 3, 1, false, false },
    { "Bell wide (Mid, Q 0.55)", "Bell wide", 1, 0, false, false },
    { "Bell narrow (Mid, Q 2.5)", "Bell narrow", 1, 1, false, false },
    { "Bell parametric (Q 0.2 to 10)", "Bell param", 1, 2, true, false },
    { "Bell parametric, Proportional", "Bell prop.", 1, 2, true, true },
};
constexpr int kNumTypes = 8;

struct TypeResult
{
    Acc inner, outer;         // new design: fc <= 0.4 fs, fc > 0.4 fs
    Acc oldInner, oldOuter;   // previous bilinear design (Clean only)
    double costSumUs = 0.0, costMaxUs = 0.0;
    long designs = 0, stage[5] {};
};

// One band (Mid, 1 kHz, bell Q 1) with a dynamic gain; every other band off.
EqParams dynOnly (float staticGainDb, float thresh, float range, int speed, bool ext)
{
    EqParams p;
    for (auto& b : p.band) b.on = false;
    auto& b = p.band[1];
    b.on = true;
    b.type = 2;
    b.freq = 1000.0f;
    b.gain = staticGainDb;
    b.q = 1.0f;
    b.dyn = true;
    b.dynThresh = thresh;
    b.dynRange = range;
    b.dynSpeed = speed;
    b.dynExt = ext;
    return p;
}

struct Steady
{
    double gainDb = 0.0; // steady output gain of the probe tone
    double dynDb = 0.0;  // dynamic gain of the band at the end of the run
};

// Steady state of a probe tone (freq, ampDb) through a module, optionally with a 1 kHz key tone.
Steady steadyState (double fs, const EqParams& p, double freq, double ampDb, const double* keyDb)
{
    const int n = (int) (fs * 1.6);
    const float amp = juce::Decibels::decibelsToGain ((float) ampDb);
    auto l = sineVec (fs, freq, amp, n), r = l;
    std::vector<float> kl, kr;
    if (keyDb != nullptr)
    {
        kl = sineVec (fs, 1000.0, juce::Decibels::decibelsToGain ((float) *keyDb), n);
        kr = kl;
    }
    EqModule m;
    m.prepare (fs);
    m.setParams (p);
    runEq (m, l, r, 256, {}, keyDb != nullptr ? &kl : nullptr, keyDb != nullptr ? &kr : nullptr);
    Steady s;
    s.gainDb = 20.0 * std::log10 (rmsOf (l, n / 2, n) / (amp / std::sqrt (2.0)));
    s.dynDb = m.getDynGainDb (1);
    return s;
}

struct Timing
{
    double attackMs = 0.0, releaseMs = 0.0;
};

// Attack: from a 0 dBFS, 1 kHz tone to 63 % of the dynamic range (range -12 dB, threshold -30).
// Release: from the end of the tone back to 1/e of the range. Measured per control tick.
Timing dynTiming (double fs, int speed)
{
    const int pre = (int) (0.1 * fs) / 32 * 32, on = (int) (0.5 * fs) / 32 * 32, off = (int) (2.5 * fs) / 32 * 32;
    const int n = pre + on + off;
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = pre; i < pre + on; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2.0 * kPiD * 1000.0 * i / fs);
    EqModule m;
    m.prepare (fs);
    m.setParams (dynOnly (0.0f, -30.0f, -12.0f, speed, false));
    std::vector<float> dyn;
    for (int i = 0; i < n; i += 32)
    {
        float* io[2] { l.data() + i, r.data() + i };
        m.process (io, 2, 32, nullptr);
        dyn.push_back (m.getDynGainDb (1));
    }
    Timing t;
    const double range = -12.0;
    for (int c = pre / 32; c < (int) dyn.size(); ++c)
        if (dyn[(size_t) c] <= 0.632 * range) { t.attackMs = (c * 32 - pre) * 1000.0 / fs; break; }
    for (int c = (pre + on) / 32; c < (int) dyn.size(); ++c)
        if (dyn[(size_t) c] >= 0.368 * range) { t.releaseMs = (c * 32 - (pre + on)) * 1000.0 / fs; break; }
    return t;
}

// Four static bands (a typical setting), with the static gains offset by `bias` dB.
EqParams fourBands (bool dyn, float bias)
{
    EqParams p;
    const float freq[4] { 120.0f, 1000.0f, 4000.0f, 9000.0f }, gain[4] { 3.0f, -4.0f, 2.0f, -3.0f }, q[4] { 0.7f, 1.0f, 2.0f, 0.7f };
    const int type[4] { 0, 2, 2, 0 };
    for (int b = 0; b < 4; ++b)
    {
        auto& bp = p.band[b];
        bp.type = type[b];
        bp.freq = freq[b];
        bp.gain = gain[b] + bias;
        bp.q = q[b];
        bp.dyn = dyn;
        bp.dynThresh = -40.0f;
        bp.dynRange = -6.0f;
        bp.dynSpeed = 1;
    }
    return p;
}

EqParams allOff()
{
    EqParams p;
    for (auto& b : p.band) b.on = false;
    return p;
}

// Processing time of one EQ instance as percent of one core. Best of three runs. Glide: the two
// parameter sets alternate every 1024 samples, so the smoothers and band designs run all the time.
double cpuPercent (double fs, const EqParams& a, const EqParams& b, bool glide, const std::vector<float>& src)
{
    const int n = (int) src.size(), block = 64;
    double best = 1.0e9;
    for (int rep = 0; rep < 3; ++rep)
    {
        EqModule m;
        m.prepare (fs);
        m.setParams (a);
        auto l = src, r = src;
        double tProc = 0.0;
        for (int i = 0; i < n; i += block)
        {
            const int len = std::min (block, n - i);
            const auto t0 = Clock::now();
            m.setParams (glide && ((i / 1024) & 1) ? b : a);
            float* io[2] { l.data() + i, r.data() + i };
            m.process (io, 2, len, nullptr);
            tProc += microsSince (t0) * 1.0e-6;
        }
        best = std::min (best, tProc);
    }
    return 100.0 * best / ((double) n / fs);
}
} // namespace

NL_MEASURE (eq, "EQ: static band accuracy against the analog prototype (all types, fc, gain, Q, fs), old vs new design, dynamic bands, display vs DSP, CPU")
{
    const std::vector<double> fsList = quick ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 };
    // Full run: every 1 dB from -18 to +18 (no gain is special); quick run: four gains.
    std::vector<double> gains;
    for (int g = -18; g <= 18; g += quick ? 12 : 1)
        if (g != 0) gains.push_back (g);
    const std::vector<double> qs = quick ? std::vector<double> { 0.5, 2.0, 10.0 } : std::vector<double> { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0 };
    const int nFc = quick ? 9 : 25;
    std::vector<double> fcs;
    for (int i = 0; i < nFc; ++i) fcs.push_back (20.0 * std::pow (1000.0, i / (double) (nFc - 1)));

    //==========================================================================
    // 1. Static bands against the analog prototype, over the grid.
    std::vector<std::vector<TypeResult>> res (fsList.size(), std::vector<TypeResult> ((size_t) kNumTypes));
    std::vector<std::array<double, kNumTypes>> maxByFc ((size_t) nFc, std::array<double, kNumTypes> {});
    for (size_t fsi = 0; fsi < fsList.size(); ++fsi)
    {
        const double fs = fsList[fsi];
        const bool withOld = fs < 90000.0;
        eqd::Context ctx;
        ctx.prepare (fs);
        for (int t = 0; t < kNumTypes; ++t)
        {
            const auto& td = kTypes[t];
            auto& R = res[fsi][(size_t) t];
            const std::vector<double> qList = td.sweepQ ? qs : std::vector<double> { 1.0 };
            for (int fi = 0; fi < nFc; ++fi)
            {
                const double fc = fcs[(size_t) fi];
                for (double gain : gains)
                    for (double qv : qList)
                    {
                        const auto bk = EqModule::kindOf (td.band, td.type, (float) qv);
                        const double qEff = EqModule::effectiveQ (bk, (float) gain, td.prop ? 1.0f : 0.0f);
                        const bool inner = fc <= 0.4 * fs;
                        const auto fr = probeFreqs (fs, fc, qEff, quick);

                        const auto t0 = Clock::now();
                        const auto des = eqd::designBand (ctx, eqd::Shape { bk.kind, fc, gain, qEff });
                        const double us = microsSince (t0);
                        R.costSumUs += us;
                        R.costMaxUs = std::max (R.costMaxUs, us);
                        ++R.designs;
                        ++R.stage[des.stage];

                        const auto c = eqd::toCoefs (des.p);
                        for (double f : fr)
                        {
                            const double err = std::abs (20.0 * std::log10 (std::max (1.0e-12, eqd::svfMagnitude (c, f, fs)))
                                                         - analogDb (bk.kind, gain, qEff, fc, f));
                            (inner ? R.inner : R.outer).add (err, fc, gain, qEff);
                            if (fs == 48000.0) maxByFc[(size_t) fi][(size_t) t] = std::max (maxByFc[(size_t) fi][(size_t) t], err);
                        }

                        if (withOld && ! td.prop)
                        {
                            const auto co = EqModule::bandCoefs (td.band, td.type, fs, (float) fc, (float) gain, (float) qEff);
                            for (double f : fr)
                            {
                                const double err = std::abs (20.0 * std::log10 (std::max (1.0e-12, eqd::svfMagnitude (co, f, fs)))
                                                             - analogDb (bk.kind, gain, qEff, fc, f));
                                (inner ? R.oldInner : R.oldOuter).add (err, fc, gain, qEff);
                            }
                        }
                    }
            }
        }
    }

    r.h2 ("1. Static bands against the analog prototype");
    r.para ("Magnitude error of the digital band against the analog RBJ prototype (same gain, Q and fc), over probes from 20 Hz "
            "to 0.9 Nyquist. Targets: shelves at all fc, and bells with fc <= 0.4 fs: 0.5 dB; bells with fc > 0.4 fs: 1.0 dB. "
            "Gains " + juce::String ((int) gains.size()) + " values (1 dB steps), Q " + juce::String ((int) qs.size()) + " values for the "
            "parametric bell, " + juce::String (nFc) + " centre frequencies, 20 Hz to 20 kHz.");
    {
        std::vector<std::vector<juce::String>> rows;
        for (size_t fsi = 0; fsi < fsList.size(); ++fsi)
            for (int t = 0; t < kNumTypes; ++t)
            {
                const auto& td = kTypes[t];
                const auto& R = res[fsi][(size_t) t];
                const bool shelf = td.band == 0 || td.band == 3;
                const bool pass = shelf ? std::max (R.inner.maxErr, R.outer.maxErr) <= 0.5
                                        : (R.inner.maxErr <= 0.5 && R.outer.maxErr <= 1.0);
                const Acc& w = R.inner.maxErr >= R.outer.maxErr ? R.inner : R.outer;
                // Dense re-check of the worst case: 20000 log probes plus 4000 around fc (the grid uses 400 + 81).
                const double fs = fsList[fsi];
                const double baseQ = td.prop ? w.wQ / (1.0 + std::abs (w.wGain) / 12.0) : w.wQ;
                const auto bk = EqModule::kindOf (td.band, td.type, (float) baseQ);
                eqd::Context dctx;
                dctx.prepare (fs);
                const auto dres = eqd::designBand (dctx, eqd::Shape { bk.kind, w.wFc, w.wGain, w.wQ });
                const auto dc = eqd::toCoefs (dres.p);
                const double fmax = 0.9 * 0.5 * fs, rr = std::min (8.0, std::sqrt (1.0 + 0.25 / (w.wQ * w.wQ)) + 0.5 / w.wQ);
                double dense = 0.0;
                for (int i = 0; i < 24000; ++i)
                {
                    const double f = i < 20000 ? 20.0 * std::pow (fmax / 20.0, i / 19999.0) : w.wFc * std::pow (rr, 4.0 * (i - 20000) / 3999.0 - 2.0);
                    if (f < 20.0 || f > fmax) continue;
                    dense = std::max (dense, std::abs (20.0 * std::log10 (std::max (1.0e-12, eqd::svfMagnitude (dc, f, fs))) - analogDb (bk.kind, w.wGain, w.wQ, w.wFc, f)));
                }
                rows.push_back ({ td.name, fmt (fs / 1000.0, 1), juce::String ((int) (R.inner.n + R.outer.n)),
                                  fmt (R.inner.maxErr, 3), fmt (R.outer.maxErr, 3), fmt (combinedRms (R.inner, R.outer), 3),
                                  juce::String (w.wFc, 0) + " Hz / " + fmt (w.wGain, 0) + " dB / Q " + fmt (w.wQ, 2),
                                  fmt (dense, 3), shelf ? juce::String ("0.5 (all fc)") : juce::String ("0.5 / 1.0"), pass ? juce::String ("pass") : juce::String ("FAIL") });
            }
        emitTable (r, { "Type", "fs (kHz)", "Cases", "Max, fc <= 0.4 fs (dB)", "Max, fc > 0.4 fs (dB)", "RMS, all (dB)",
                        "Worst case", "Dense check of worst case (dB)", "Target (dB)", "Verdict" }, rows);
    }

    r.h3 ("1a. Maximum error by centre frequency (48 kHz, all gains and Q)");
    {
        std::vector<juce::String> header { "fc (Hz)" };
        for (const auto& td : kTypes) header.push_back (td.shortName);
        std::vector<std::vector<juce::String>> rows;
        for (int fi = 0; fi < nFc; ++fi)
        {
            std::vector<juce::String> row { fmt (fcs[(size_t) fi], 0) };
            for (int t = 0; t < kNumTypes; ++t) row.push_back (fmt (maxByFc[(size_t) fi][(size_t) t], 3));
            rows.push_back (row);
        }
        emitTable (r, header, rows);
    }

    r.h3 ("1b. Previous bilinear design against the new one (Clean, same grid)");
    {
        std::vector<std::vector<juce::String>> rows;
        for (size_t fsi = 0; fsi < fsList.size(); ++fsi)
        {
            if (fsList[fsi] >= 90000.0) continue;
            for (int t = 0; t < kNumTypes; ++t)
            {
                const auto& td = kTypes[t];
                if (td.prop) continue;
                const auto& R = res[fsi][(size_t) t];
                rows.push_back ({ td.name, fmt (fsList[fsi] / 1000.0, 1), fmt (R.oldInner.maxErr, 2), fmt (R.inner.maxErr, 3),
                                  fmt (R.oldOuter.maxErr, 2), fmt (R.outer.maxErr, 3), fmt (combinedRms (R.oldInner, R.oldOuter), 2),
                                  fmt (combinedRms (R.inner, R.outer), 3) });
            }
        }
        emitTable (r, { "Type", "fs (kHz)", "Old max, fc <= 0.4 fs", "New max, fc <= 0.4 fs", "Old max, fc > 0.4 fs",
                        "New max, fc > 0.4 fs", "Old RMS", "New RMS" }, rows);
    }

    r.h3 ("1c. Design cost (pure function, per call at control rate)");
    {
        std::vector<std::vector<juce::String>> rows;
        for (int t = 0; t < kNumTypes; ++t)
        {
            TypeResult agg;
            for (size_t fsi = 0; fsi < fsList.size(); ++fsi)
            {
                const auto& R = res[fsi][(size_t) t];
                agg.costSumUs += R.costSumUs;
                agg.costMaxUs = std::max (agg.costMaxUs, R.costMaxUs);
                agg.designs += R.designs;
                for (int s = 0; s < 5; ++s) agg.stage[s] += R.stage[s];
            }
            const double dn = (double) agg.designs;
            rows.push_back ({ kTypes[t].name, juce::String ((int) agg.designs), fmt (agg.costSumUs / dn, 1), fmt (agg.costMaxUs, 0),
                              fmt (100.0 * agg.stage[1] / dn, 1), fmt (100.0 * agg.stage[2] / dn, 1), fmt (100.0 * agg.stage[3] / dn, 1), fmt (100.0 * agg.stage[4] / dn, 2) });
        }
        emitTable (r, { "Type", "Designs", "Mean (us)", "Max (us)", "Closed form (%)", "3-value refinement (%)", "5-value refinement (%)", "Fallback (%)" }, rows);
        r.para ("Stage 1 is the closed form; a stage above it means the closed form missed the tolerance on the solver grid and the "
                "refinement ran (only at parameter changes, never per tick when nothing moved).");
    }

    //==========================================================================
    // 2. Dynamic bands.
    r.h2 ("2. Dynamic bands");
    r.h3 ("2a. Gain law and steady state (48 kHz, 1 kHz tone, detection on the input, threshold -30 dBFS)");
    {
        const double fs = 48000.0;
        const EqParams cut = dynOnly (0.0f, -30.0f, -12.0f, 1, false), boost = dynOnly (0.0f, -30.0f, 9.0f, 1, false);
        std::vector<std::vector<juce::String>> rows;
        double worst = 0.0;
        for (double L : { -36.0, -30.0, -27.0, -24.0, -21.0, -18.0, -12.0, -6.0, 0.0 })
        {
            const double levelDb = L - 3.0103; // mean square of a sine
            const double expCut = eqd::dynGainDb ((float) levelDb, -30.0f, -12.0f), expBoost = eqd::dynGainDb ((float) levelDb, -30.0f, 9.0f);
            const double mCut = steadyState (fs, cut, 1000.0, L, nullptr).gainDb, mBoost = steadyState (fs, boost, 1000.0, L, nullptr).gainDb;
            worst = std::max (worst, std::max (std::abs (mCut - expCut), std::abs (mBoost - expBoost)));
            rows.push_back ({ fmt (L, 0), fmt (levelDb, 1), fmt (expCut, 2), fmt (mCut, 2), fmt (expBoost, 2), fmt (mBoost, 2) });
        }
        emitTable (r, { "Tone (dBFS)", "Band level (dB)", "Cut -12 dB: expected (dB)", "Cut: measured (dB)", "Boost +9 dB: expected (dB)", "Boost: measured (dB)" }, rows);
        r.para ("Worst |measured - expected| over these levels: " + fmt (worst, 3) + " dB. Expected uses the gain law in docs/dsp/eq.md "
                "and the band level of a sine (mean square of the band-passed tone, -3.01 dB at 0 dBFS).");
    }

    {
        // Detector gain at the band frequency: a unity band-pass makes the sine level exactly -3.01 dB.
        std::vector<juce::String> parts;
        for (double q : { 0.5, 1.0, 2.5 })
        {
            const auto rf = eqd::makeRegionFilter (eqd::Region::BandPass, 48000.0, 1000.0f, (float) q);
            parts.push_back ("Q " + fmt (q, 1) + ": " + fmt (20.0 * std::log10 (eqd::svfMagnitude (rf, 1000.0, 48000.0)), 4) + " dB");
        }
        {
            // Detector level of a steady 1 kHz sine at -24 dBFS: the mean square should read -27.01 dB.
            const double fs = 48000.0;
            const auto x = sineVec (fs, 1000.0, juce::Decibels::decibelsToGain (-24.0f), (int) (fs * 1.6));
            eqd::BandDetector d;
            d.setFilter (eqd::Region::BandPass, fs, 1000.0f, 1.0f);
            d.run (0, x.data(), (int) x.size());
            d.run (1, x.data(), (int) x.size());
            d.sanitize();
            {
                // The same detector sampled at control ticks (every 32 samples), as the module does.
                eqd::BandDetector t;
                t.setFilter (eqd::Region::BandPass, fs, 1000.0f, 1.0f);
                double sum = 0.0, lo = 1.0e9, hi = -1.0e9, tsum = 0.0;
                int nt = 0;
                for (int i = 0; i < (int) x.size(); i += 32)
                {
                    t.run (0, x.data() + i, 32);
                    t.run (1, x.data() + i, 32);
                    t.sanitize();
                    const double lv = t.levelDb (2, 0);
                    if (i > (int) (0.6 * fs)) { sum += lv; ++nt; lo = std::min (lo, lv); hi = std::max (hi, lv); tsum += eqd::dynGainDb ((float) lv, -30.0f, -12.0f); }
                }
                r.para ("Same detector sampled at control ticks after 0.6 s: mean " + fmt (sum / std::max (1, nt), 4) + " dB, range " + fmt (lo, 3) + " to " + fmt (hi, 3) + " dB (2 kHz ripple of the mean-square follower). Mean target gain " + fmt (tsum / std::max (1, nt), 4) + " dB.");
            }
            r.para ("Detector level of a steady 1 kHz sine at -24 dBFS: " + fmt (d.levelDb (2, 0), 4) + " dB (expected -27.0103 dB).");
        }
        r.para ("Detector band-pass gain at 1 kHz (48 kHz): " + juce::StringArray (parts.data(), (int) parts.size()).joinIntoString ("; ") + ".");
    }

    r.h3 ("2b. Speed (48 kHz, 0 dBFS tone burst, range -12 dB, threshold -30 dBFS)");
    {
        std::vector<std::vector<juce::String>> rows;
        const char* names[3] { "Fast", "Medium", "Slow" };
        for (int s = 0; s < 3; ++s)
        {
            const auto nominal = eqd::dynSpeed (s);
            const auto t = dynTiming (48000.0, s);
            const double steadyDb = steadyState (48000.0, dynOnly (0.0f, -30.0f, -12.0f, s, false), 1000.0, -24.0, nullptr).gainDb;
            rows.push_back ({ names[s], fmt (nominal.attackMs, 1), fmt (t.attackMs, 1), fmt (nominal.releaseMs, 1), fmt (t.releaseMs, 1), fmt (steadyDb, 2) });
        }
        emitTable (r, { "Speed", "Attack, nominal tau (ms)", "Attack to 63 % (ms)", "Release, nominal tau (ms)", "Release to 1/e (ms)", "Steady gain, 1 kHz at -24 dBFS (dB)" }, rows);
        r.para ("Expected steady gain at -24 dBFS: -2.99 dB (range -12 dB, threshold -30 dBFS). The attack is within one control tick (0.67 ms) of "
                "the nominal tau: a 0 dBFS tone crosses the knee in well under a tick. The release includes the time for the detector to fall "
                "below the knee (about 10 ms for a 2 ms mean-square follower). The steady gain at a tone at the band frequency is biased "
                "by the detector's 2 kHz ripple, see docs/dsp/eq.md.");
    }

    //==========================================================================
    // 3. Display against the DSP with a dynamic gain.
    r.h2 ("3. Display against the DSP with a dynamic gain (D13)");
    r.para ("The Mid band is a dynamic bell at 1 kHz (static gain 0, range -12 dB, threshold -30 dBFS). An external 1 kHz key tone holds "
            "it at a steady dynamic gain, which is read back from the module. Probe tones at -20 dBFS are measured through the module and "
            "compared with `EqModule::magnitudeDb` given that dynamic gain. The key levels sample the dynamic gain between the 1 dB nodes of the "
            "gain-axis table, so interpolation between nodes is included.");
    {
        const EqParams p = dynOnly (0.0f, -30.0f, -12.0f, 1, true);
        const std::vector<double> probes { 50.0, 200.0, 500.0, 800.0, 1000.0, 1200.0, 2000.0, 5000.0, 10000.0, 16000.0, 20000.0 };
        std::vector<std::vector<juce::String>> summary;
        std::vector<std::vector<juce::String>> detail;
        double worstAll = 0.0;
        for (double keyDb : { -27.0, -24.0, -21.0, -18.0, -15.0, -12.0 })
        {
            double worst[2] { 0.0, 0.0 }, dyn[2] { 0.0, 0.0 };
            for (double f : probes)
            {
                std::vector<juce::String> row { fmt (f, 0) };
                for (int k = 0; k < 2; ++k)
                {
                    const double fs = k == 0 ? 48000.0 : 96000.0;
                    const auto s = steadyState (fs, p, f, -20.0, &keyDb);
                    const float node[4] { 0.0f, (float) s.dynDb, 0.0f, 0.0f };
                    const double disp = EqModule::magnitudeDb (p, fs, f, 0, node);
                    const double err = std::abs (s.gainDb - disp);
                    worst[k] = std::max (worst[k], err);
                    dyn[k] = s.dynDb;
                    if (keyDb == -21.0) { row.push_back (fmt (s.gainDb, 3)); row.push_back (fmt (disp, 3)); row.push_back (fmt (err, 3)); }
                }
                if (keyDb == -21.0) detail.push_back (row);
            }
            worstAll = std::max (worstAll, std::max (worst[0], worst[1]));
            summary.push_back ({ fmt (keyDb, 0), fmt (dyn[0], 2), fmt (worst[0], 3), fmt (dyn[1], 2), fmt (worst[1], 3) });
        }
        emitTable (r, { "Key tone (dBFS)", "Dynamic gain, 48 kHz (dB)", "Worst display error, 48 kHz (dB)", "Dynamic gain, 96 kHz (dB)", "Worst display error, 96 kHz (dB)" }, summary);
        r.para ("Probe detail at key -21 dBFS (dynamic gain about -6 dB):");
        emitTable (r, { "Probe (Hz)", "DSP 48 kHz (dB)", "Display 48 kHz (dB)", "Error", "DSP 96 kHz (dB)", "Display 96 kHz (dB)", "Error" }, detail);
        r.para ("Worst display error over all key levels, probes and both rates: " + fmt (worstAll, 3) + " dB (target 0.1 dB, roadmap D13).");
    }

    //==========================================================================
    // 4. CPU.
    r.h2 ("4. CPU");
    r.para ("One EQ instance, stereo, 10 s of white noise at -20 dBFS RMS, 64-sample blocks with setParams every block, best of three "
            "runs, percent of one core. Lead's budget: four dynamic bands at 48 kHz <= 0.15 %.");
    {
        std::vector<std::vector<juce::String>> rows;
        const double seconds = quick ? 2.0 : 10.0;
        for (double fs : { 48000.0, 96000.0 })
        {
            if (quick && fs > 90000.0) continue;
            const auto src = noiseVec ((int) (fs * seconds), 0.173f, 7);
            const EqParams off = allOff(), st = fourBands (false, 0.0f), dy = fourBands (true, 0.0f);
            const juce::String tag = fs < 90000.0 ? "48 kHz" : "96 kHz";
            const double offPct = cpuPercent (fs, off, off, false, src);
            const double stPct = cpuPercent (fs, st, st, false, src);
            const double stGlide = cpuPercent (fs, fourBands (false, 0.0f), fourBands (false, 6.0f), true, src);
            const double dyPct = cpuPercent (fs, dy, dy, false, src);
            const double dyGlide = cpuPercent (fs, fourBands (true, 0.0f), fourBands (true, 6.0f), true, src);
            const auto verdict = [] (double pct) { return pct <= 0.15 ? juce::String ("within 0.15 %") : juce::String ("over 0.15 %"); };
            rows.push_back ({ "All four bands off", tag, fmt (offPct, 4), "-" });
            rows.push_back ({ "4 static bands, steady", tag, fmt (stPct, 4), "-" });
            rows.push_back ({ "4 static bands, gains gliding (6 dB every 1024 samples)", tag, fmt (stGlide, 4), "-" });
            rows.push_back ({ "4 dynamic bands, steady detection", tag, fmt (dyPct, 4), verdict (dyPct) });
            rows.push_back ({ "4 dynamic bands, gains gliding", tag, fmt (dyGlide, 4), verdict (dyGlide) });
        }
        emitTable (r, { "Configuration", "fs", "CPU (% of one core)", "Budget" }, rows);
    }

    r.para ("Thresholds for the same numbers are asserted in tools/tests/TestEq.cpp. Method, the gain law, the speeds and the "
            "exclusion rule for high-frequency bells are in docs/dsp/eq.md.");
}

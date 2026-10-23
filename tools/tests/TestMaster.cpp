// Master section: limiter (roadmap D2, 7.7) and processor-owned metering (D6).
#include "MasterSignals.h"
#include <thread>

using namespace nl;

namespace tool
{
using namespace master;

namespace
{
constexpr double kFs = 48000.0;

int expectedLookahead (double fs) { return std::max (2, (int) std::lround (0.0015 * fs)); }

int argMaxAbs (const Buf& b, int ch = 0)
{
    int idx = 0;
    float m = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > m) { m = std::abs (b.getSample (ch, i)); idx = i; }
    return idx;
}
} // namespace

//==============================================================================
// D2: latency follows lim_mode and quality only.
NL_TEST (master_latencyDependsOnModeAndQualityOnly)
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (auto& cfg : allModes())
        {
            Rig off (fs, cfg.mode, cfg.hq, false), on (fs, cfg.mode, cfg.hq, true);
            const int expected = cfg.mode == 0 ? (cfg.hq ? off.ms.latency() : 0) : expectedLookahead (fs);
            const juce::String what = juce::String (cfg.name) + " @ " + juce::String (fs, 0);
            check (off.ms.latency() == on.ms.latency() && off.ms.latency() == expected, what + ": latency identical with LIMIT off and on",
                   juce::String (off.ms.latency()) + " / " + juce::String (on.ms.latency()) + " samples");
            if (cfg.mode == 0 && cfg.hq) check (off.ms.latency() > 0, what + ": HQ Safety Clip reports its oversampler latency");
            // Toggling at runtime never changes the reported latency either.
            off.setOn (true);
            off.ms.setParams (off.snap);
            check (off.ms.latency() == expected, what + ": latency unchanged after switching LIMIT on");
        }
}

// D2: an impulse stays where it is whatever LIMIT does, in every mode.
NL_TEST (master_impulsePositionUnchangedByToggle)
{
    for (auto& cfg : allModes())
    {
        const int at = 1000;
        Buf imp (2, 6000);
        imp.clear();
        imp.setSample (0, at, 0.5f);
        imp.setSample (1, at, 0.5f);
        bool ok = true;
        juce::String detail;
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            Rig r (kFs, cfg.mode, cfg.hq, scenario == 1);
            std::vector<Rig::Event> ev;
            if (scenario == 2)
                for (int s = 100; s < 5000; s += 700) ev.push_back ({ s, [on = (s / 700) % 2 == 0] (Rig& g) { g.setOn (on); } });
            auto out = r.run (imp, 128, ev);
            const int pos = argMaxAbs (out), lat = r.ms.latency();
            const float v = out.getSample (0, pos);
            const bool good = pos == at + lat && std::abs (v - 0.5f) < 0.02f;
            ok &= good;
            detail << (scenario == 0 ? "off " : scenario == 1 ? "on " : "toggled ") << "peak at +" << (pos - at) << " (latency " << lat << ") " << juce::String (v, 3) << "; ";
        }
        check (ok, juce::String (cfg.name) + ": impulse position and level unchanged by LIMIT on/off/toggling", detail);
    }
}

// D2: the old bug replayed stale audio when the limiter was re-enabled into silence.
NL_TEST (master_reenableIntoSilence)
{
    for (auto& cfg : allModes())
    {
        Rig r (kFs, cfg.mode, cfg.hq, true, -1.0);
        auto loud = pinkStereo (9600, -6.0);
        const int silenceLen = 9600;
        Buf in (2, loud.getNumSamples() + 600 + silenceLen + silenceLen);
        in.clear();
        for (int c = 0; c < 2; ++c) in.copyFrom (c, 0, loud, c, 0, 9600);
        // 600 more samples of loud audio while the limiter is already off
        auto more = pinkStereo (600, -6.0, 9);
        for (int c = 0; c < 2; ++c) in.copyFrom (c, 9600, more, c, 0, 600);
        const int offAt = 9600, silenceStart = 9600 + 600, onAt = silenceStart + silenceLen;
        auto out = r.run (in, 256, { { offAt, [] (Rig& g) { g.setOn (false); } }, { onAt, [] (Rig& g) { g.setOn (true); } } });
        const int lat = r.ms.latency();
        const double tail = maxAbs (out, onAt, in.getNumSamples());
        const double flushed = maxAbs (out, silenceStart + lat + 64, onAt);
        check (linToDb (tail) <= -144.0 && linToDb (flushed) <= -144.0, juce::String (cfg.name) + ": re-enable into silence <= -144 dBFS",
               "after re-enable " + juce::String (linToDb (tail), 1) + " dBFS, while off " + juce::String (linToDb (flushed), 1) + " dBFS");
    }
}

// D2: switching a 100 Hz sine below the ceiling on and off leaves no discontinuity.
NL_TEST (master_toggleIsClickFree)
{
    for (auto& cfg : allModes())
    {
        const int n = 48000;
        auto in = sine (kFs, 100.0, 0.25f, n);
        Rig r (kFs, cfg.mode, cfg.hq, true);
        std::vector<Rig::Event> ev;
        for (int s = 3000, k = 0; s < n - 4000; s += 3777, ++k) ev.push_back ({ s, [on = (k % 2 == 1)] (Rig& g) { g.setOn (on); } });
        auto out = r.run (in, 100, ev);
        const int lat = r.ms.latency();
        double resid = 0, maxD2out = 0, maxD2ref = 0;
        for (int i = lat + 200; i < n; ++i)
        {
            const double ref = in.getSample (0, i - lat);
            resid = std::max (resid, std::abs ((double) out.getSample (0, i) - ref));
            if (i > lat + 202)
            {
                const double d2 = out.getSample (0, i) - 2.0 * out.getSample (0, i - 1) + out.getSample (0, i - 2);
                const double r2 = in.getSample (0, i - lat) - 2.0 * in.getSample (0, i - lat - 1) + in.getSample (0, i - lat - 2);
                maxD2out = std::max (maxD2out, std::abs (d2));
                maxD2ref = std::max (maxD2ref, std::abs (r2));
            }
        }
        // The derivative test applies where wet and dry are identical; the HQ oversampler's own
        // passband ripple (steady, about -75 dB) is covered by the residual criterion.
        const bool derivOk = (cfg.mode == 0 && cfg.hq) || maxD2out <= 1.5 * maxD2ref + 1e-7;
        check (linToDb (resid) <= -60.0 && derivOk, juce::String (cfg.name) + ": on/off toggling a 100 Hz sine has no discontinuity above -60 dB",
              "residual " + juce::String (linToDb (resid), 1) + " dBFS, max 2nd difference " + juce::String (maxD2out / maxD2ref, 3) + "x the signal's own");
    }
}

// Determinism: identical output for every host block size, under automation of on/off,
// ceiling and release (events on multiples of 2048).
NL_TEST (master_blockSizeIndependence)
{
    const int n = 16384;
    auto in = pinkStereo (n, 6.0, 5);
    for (auto& cfg : allModes())
    {
        std::vector<Rig::Event> ev {
            { 2048, [] (Rig& g) { g.setCeiling (-6.0); } },
            { 4096, [] (Rig& g) { g.setOn (false); } },
            { 6144, [] (Rig& g) { g.setOn (true); g.setRelease (30.0); } },
            { 8192, [] (Rig& g) { g.setCeiling (-1.0); } },
            { 10240, [] (Rig& g) { g.setRelease (400.0); } },
            { 12288, [] (Rig& g) { g.setOn (false); } },
            { 14336, [] (Rig& g) { g.setOn (true); g.setCeiling (-12.0); } } };
        Rig ref (kFs, cfg.mode, cfg.hq, true);
        auto base = ref.run (in, 2048, ev);
        float worst = 0;
        int worstBlock = 0;
        for (int block : { 1, 7, 32, 37, 64, 511, 512 })
        {
            Rig r (kFs, cfg.mode, cfg.hq, true);
            auto out = r.run (in, block, ev);
            float d = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) d = std::max (d, std::abs (out.getSample (c, i) - base.getSample (c, i)));
            if (d > worst) { worst = d; worstBlock = block; }
        }
        check (worst <= 1.0e-6f, juce::String (cfg.name) + ": output independent of block size {1,7,32,37,64,511,512,2048} under automation",
              "max diff " + juce::String (worst, 9) + " (block " + juce::String (worstBlock) + ")");
    }
}

// Ceiling smoothing: about 20 ms (one-pole in dB).
NL_TEST (master_ceilingIsSmoothed)
{
    Rig r (kFs, 1, false, true, -1.0);
    const int n = 48000, stepAt = 20000;
    auto in = sine (kFs, 1000.0, 1.0f, n);
    auto out = r.run (in, 64, { { stepAt, [] (Rig& g) { g.setCeiling (-13.0); } } });
    const int lat = r.ms.latency();
    // Peak per millisecond, in dB.
    std::vector<double> lvl;
    for (int s = stepAt + lat; s + 48 <= n; s += 48) lvl.push_back (linToDb (maxAbs (out, s, s + 48)));
    const double target = -1.0 - 12.0 * 0.632;
    int idx = -1;
    for (size_t i = 0; i < lvl.size(); ++i)
        if (lvl[i] <= target) { idx = (int) i; break; }
    bool monotone = true;
    for (size_t i = 1; i < lvl.size() && i < 200; ++i) monotone &= lvl[i] <= lvl[i - 1] + 0.05;
    check (idx >= 15 && idx <= 26 && monotone, "ceiling step -1 -> -13 dB reaches 63% after about 20 ms and moves monotonically",
           "63% point after " + juce::String (idx) + " ms, final " + juce::String (lvl.back(), 2) + " dBFS");
    check (std::abs (lvl.back() + 13.0) < 0.1, "ceiling settles on the target", juce::String (lvl.back(), 3) + " dBFS");
}

// Changing the (non-automatable) mode: finite, no big jump, latency follows.
NL_TEST (master_modeChangeIsSafe)
{
    const int n = 30000;
    auto in = sine (kFs, 200.0, 0.25f, n);
    for (auto& from : allModes())
        for (auto& to : allModes())
        {
            if (from.mode == to.mode && from.hq == to.hq) continue;
            Rig r (kFs, from.mode, from.hq, true);
            const int latFrom = r.ms.latency();
            auto out = r.run (in, 256, { { 10000, [to] (Rig& g) { g.set (to.mode, to.hq, true, -0.3, 100.0); } } });
            float maxStep = 0;
            for (int i = 1; i < n; ++i) maxStep = std::max (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            const int latTo = r.ms.latency();
            const bool ok = allFinite (out) && maxStep < 0.02f && latTo != latFrom + 12345;
            if (! ok || (from.mode == 0 && to.mode == 1))
                check (ok, juce::String (from.name) + " -> " + to.name + ": finite and click-free", "max step " + juce::String (maxStep, 4) + ", latency " + juce::String (latFrom) + " -> " + juce::String (latTo));
        }
}

NL_TEST (master_nonFiniteInputIsHarmless)
{
    for (auto& cfg : allModes())
    {
        Rig r (kFs, cfg.mode, cfg.hq, true);
        auto in = pinkStereo (20000, -3.0);
        in.setSample (0, 5000, std::numeric_limits<float>::quiet_NaN());
        in.setSample (1, 5001, std::numeric_limits<float>::infinity());
        in.setSample (0, 9000, -std::numeric_limits<float>::infinity());
        auto out = r.run (in, 128);
        auto& hub = r.ms.meters();
        const auto s = hub.read();
        check (allFinite (out) && std::isfinite (s.momentary) && std::isfinite (s.integrated) && std::isfinite (s.truePeakDb),
               juce::String (cfg.name) + ": NaN/Inf input leaves limiter and meters finite");
    }
}

//==============================================================================
// 7.7 Safety Clip.
NL_TEST (master_safetyClip)
{
    const double ceilDb = -1.0, ceil = dbToLin (ceilDb);
    {
        Rig r (kFs, 0, false, true, ceilDb);
        check (r.ms.latency() == 0, "Safety Clip Eco: zero latency");
        auto quiet = sine (kFs, 1000.0, (float) (0.6 * ceil), 20000);
        auto out = r.run (quiet, 256);
        float d = 0;
        for (int i = 0; i < quiet.getNumSamples(); ++i) d = std::max (d, std::abs (out.getSample (0, i) - quiet.getSample (0, i)));
        check (d == 0.0f, "Safety Clip Eco: bit-exact transparent below the knee (0.6 x ceiling)", "max diff " + juce::String (d, 9));

        auto hot = sine (kFs, 1000.0, (float) (2.0 * ceil), 20000);
        out = r.run (hot, 256);
        const double pk = maxAbs (out, 2000);
        check (pk <= ceil * 1.0000001 && pk > 0.9 * ceil, "Safety Clip Eco: +6 dB over the ceiling stays under it without collapsing", juce::String (linToDb (pk), 3) + " dBFS (ceiling " + juce::String (ceilDb, 1) + ")");

        auto burst = scaledPeak (pinkStereo (48000, 0.0), 12.0); // very hot noise
        out = r.run (burst, 100);
        check (maxAbs (out) <= ceil * 1.0000001 && maxAbs (out, 0, -1, 1) <= ceil * 1.0000001, "Safety Clip Eco: never exceeds the ceiling on +13 dB noise",
              juce::String (linToDb (std::max (maxAbs (out), maxAbs (out, 0, -1, 1))), 3) + " dBFS");
    }

    // Aliasing: 5 kHz tone 9 dB over the ceiling. Naive reference = the same transfer function without ADAA.
    {
        const int N = 8192, bin = 853;
        const double f0 = bin * kFs / N;
        auto aliasDb = [&] (const Buf& b, int from)
        {
            double total = 0, alias = 0;
            std::vector<double> w ((size_t) N);
            for (int k = 1; k < N / 2; ++k)
            {
                std::complex<double> acc = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const double win = 0.5 - 0.5 * std::cos (2.0 * kPiD * i / N);
                    acc += win * b.getSample (0, from + i) * std::polar (1.0, -2.0 * kPiD * k * i / N);
                }
                const double e = std::norm (acc);
                total += e;
                bool harmonic = false;
                for (int h = 1; h * bin < N / 2; ++h) harmonic |= std::abs (k - h * bin) <= 3;
                if (! harmonic && k > 4) alias += e;
            }
            return 10.0 * std::log10 (alias / total + 1e-30);
        };
        auto in = sine (kFs, f0, (float) (dbToLin (9.0) * ceil), N * 3);
        Buf naive (2, N * 3);
        const double k = (double) Limiter::kKnee;
        for (int i = 0; i < N * 3; ++i)
        {
            const double u = in.getSample (0, i) / ceil, a = std::abs (u);
            const double y = (a <= k ? a : k + (1 - k) * std::tanh ((a - k) / (1 - k))) * (u < 0 ? -1 : 1);
            naive.setSample (0, i, (float) (y * ceil));
        }
        Rig eco (kFs, 0, false, true, ceilDb), hq (kFs, 0, true, true, ceilDb);
        auto oe = eco.run (in, 256), oh = hq.run (in, 256);
        const double aN = aliasDb (naive, 2 * N), aE = aliasDb (oe, 2 * N), aH = aliasDb (oh, 2 * N + hq.ms.latency());
        check (aE < aN - 3.0, "Safety Clip Eco (ADAA) aliases at least 3 dB less than the naive soft clipper (5 kHz, +9 dB over)",
               "naive " + juce::String (aN, 1) + " dB, Eco " + juce::String (aE, 1) + " dB re total");
        // The HQ ceiling guard (after the decimator) costs some oversampling gain when the clipper is hard at +9 dB;
        // the guard is idle at +3 dB, where the 6 dB oversampling benefit is checked.
        check (aH < aN - 3.0, "Safety Clip HQ aliases at least 3 dB less than the naive soft clipper (+9 dB over, ceiling guard engaged)",
               "HQ " + juce::String (aH, 1) + " dB re total");
        check (maxAbs (oh, 2 * N) <= ceil * 1.0000001, "Safety Clip HQ never exceeds the ceiling at the sample level (5 kHz, +9 dB over)",
               juce::String (linToDb (maxAbs (oh, 2 * N)), 3) + " dBFS");
        {
            auto in3 = sine (kFs, f0, (float) (dbToLin (3.0) * ceil), N * 3);
            Rig eco3 (kFs, 0, false, true, ceilDb), hq3 (kFs, 0, true, true, ceilDb);
            const double e3 = aliasDb (eco3.run (in3, 256), 2 * N), h3 = aliasDb (hq3.run (in3, 256), 2 * N + hq3.ms.latency());
            check (h3 < e3 - 6.0, "Safety Clip HQ (4x oversampled) aliases at least 6 dB less than Eco at +3 dB over (guard idle)",
                   "HQ " + juce::String (h3, 1) + " dB, Eco " + juce::String (e3, 1) + " dB re total");
        }
    }

    // HQ: exact integer latency, linear phase (impulse centre of gravity), transparent below the knee.
    {
        Rig r (kFs, 0, true, true, ceilDb);
        const int lat = r.ms.latency();
        Buf imp (2, 2000);
        imp.clear();
        imp.setSample (0, 500, 0.1f);
        imp.setSample (1, 500, 0.1f);
        auto out = r.run (imp, 64);
        double sw = 0, swx = 0;
        for (int i = 0; i < 2000; ++i)
        {
            const double v = out.getSample (0, i);
            sw += v * v;
            swx += v * v * (i - 500);
        }
        const double centroid = swx / sw;
        check (std::abs (centroid - lat) < 0.01 && argMaxAbs (out) == 500 + lat, "Safety Clip HQ: reported latency equals the true (linear-phase) delay",
               "reported " + juce::String (lat) + ", impulse centroid +" + juce::String (centroid, 4) + " samples");
        float worst = 0;
        for (double f : { 100.0, 1000.0, 5000.0, 12000.0 })
        {
            Rig q (kFs, 0, true, true, ceilDb);
            auto s = sine (kFs, f, 0.3f, 20000);
            auto o = q.run (s, 256);
            float d = 0;
            for (int i = 4000; i < 20000; ++i) d = std::max (d, std::abs (o.getSample (0, i) - s.getSample (0, i - lat)));
            worst = std::max (worst, d);
        }
        check (linToDb (worst) < -60.0, "Safety Clip HQ: below the knee the chain matches delayed dry (100 Hz to 12 kHz)", juce::String (linToDb (worst), 1) + " dBFS");
    }
}

//==============================================================================
// 7.7 Brickwall and True Peak.
NL_TEST (master_brickwallHoldsCeiling)
{
    const double ceilDb = -1.0, ceil = dbToLin (ceilDb);
    std::vector<std::pair<const char*, Buf>> sigs;
    sigs.emplace_back ("pink +10 dB", pinkStereo (96000, ceilDb + 10.0));
    sigs.emplace_back ("drums +10 dB", drums (kFs, 3.0, ceilDb + 10.0));
    sigs.emplace_back ("sweep +10 dB", sweep (kFs, 4.0, ceilDb + 10.0));
    for (auto& [name, sig] : sigs)
    {
        Rig r (kFs, 1, false, true, ceilDb);
        auto out = r.run (sig, 128);
        const double pk = std::max (maxAbs (out, 4800), maxAbs (out, 4800, -1, 1));
        check (pk <= ceil * 1.00005, juce::String ("Brickwall holds the sample ceiling: ") + name, juce::String (linToDb (pk), 4) + " dBFS (ceiling " + juce::String (ceilDb, 1) + ")");
    }
}

NL_TEST (master_truePeakModeHoldsCeiling)
{
    const double ceilDb = -1.0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<std::pair<const char*, Buf>> sigs;
        sigs.emplace_back ("pink +10 dB", pinkStereo ((int) (fs * 2), ceilDb + 10.0));
        sigs.emplace_back ("drums +10 dB", drums (fs, 3.0, ceilDb + 10.0));
        sigs.emplace_back ("sine sweep +10 dB", sweep (fs, 4.0, ceilDb + 10.0));
        for (auto& [name, sig] : sigs)
        {
            Rig r (fs, 2, false, true, ceilDb);
            auto out = r.run (sig, 128);
            // The whole output, from the first sample: slicing would add a step at the cut that rings in any interpolator.
            const double meter = std::max (meterTruePeakDb (out, fs), -200.0);
            const double ref = std::max (refTruePeakDb (out, 0), refTruePeakDb (out, 1));
            check (meter <= ceilDb + 0.1 && ref <= ceilDb + 0.1, "True Peak mode " + juce::String (fs, 0) + " Hz, " + name + ": output <= ceiling + 0.1 dBTP",
                   "BS.1770 4x meter " + juce::String (meter, 3) + ", 16x reference " + juce::String (ref, 3) + " dBTP (ceiling " + juce::String (ceilDb, 1) + ")");
        }
    }
    // A full-scale fs/4 sine at 45 degrees has samples at 0.707 of its true peak: only True Peak mode catches it.
    {
        const double ceilDb2 = -3.0;
        Buf s (2, 48000);
        for (int i = 0; i < 48000; ++i)
        {
            // true peak 2 dB over the ceiling, but the samples (0.707 of it) are 1 dB under it
            const float v = (float) (dbToLin (ceilDb2 + 2.0) * std::sin (2.0 * kPiD * 0.25 * i + kPiD / 4.0));
            s.setSample (0, i, v);
            s.setSample (1, i, v);
        }
        Rig bw (kFs, 1, false, true, ceilDb2), tpm (kFs, 2, false, true, ceilDb2);
        auto ob = bw.run (s, 256), ot = tpm.run (s, 256);
        const double tb = refTruePeakDb (ob, 0, 9600), tt = refTruePeakDb (ot, 0, 9600);
        check (tt <= ceilDb2 + 0.1 && tb > ceilDb2 + 1.0, "fs/4 sine at 45 deg: True Peak mode meets the ceiling, Brickwall exceeds it by its inter-sample peak",
               "True Peak " + juce::String (tt, 2) + ", Brickwall " + juce::String (tb, 2) + " dBTP, ceiling " + juce::String (ceilDb2, 1));
    }
}

NL_TEST (master_thdOverCeiling)
{
    for (double fs : { 44100.0, 48000.0 })
        for (int mode : { 1, 2 })
        {
            const double ceilDb = -6.0;
            const int N = (int) (fs * 0.1), total = (int) (fs * 1.0);
            auto in = sine (fs, 1000.0, (float) dbToLin (ceilDb + 3.0), total);
            Rig r (fs, mode, false, true, ceilDb);
            auto out = r.run (in, 256);
            const double thd = thdDb (out, total - N - 1000, N, 100);
            const double level = linToDb (maxAbs (out, total - N));
            check (thd <= -40.0, juce::String (mode == 1 ? "Brickwall" : "True Peak") + " " + juce::String (fs, 0) + " Hz: 1 kHz sine 3 dB over the ceiling has THD <= -40 dB after settling",
                   "THD " + juce::String (thd, 1) + " dB, output peak " + juce::String (level, 2) + " dBFS");
        }
}

// Program-dependent release: short gain reduction recovers much faster than sustained gain reduction.
NL_TEST (master_releaseIsProgramDependent)
{
    auto recoveryMs = [] (double burstSeconds)
    {
        const double fs = kFs;
        const int pre = (int) (0.3 * fs), burst = (int) (burstSeconds * fs), post = (int) (3.0 * fs);
        Buf in (2, pre + burst + post);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double a = (i >= pre && i < pre + burst) ? 4.0 * 0.5 : 0.25; // 12 dB over the ceiling, then well under
            const float v = (float) (a * std::sin (2.0 * kPiD * 1000.0 * i / fs));
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }
        Rig r (fs, 1, false, true, -6.0, 100.0);
        auto out = r.run (in, 128);
        const int lat = r.ms.latency();
        // Gain of the quiet tone after the burst, per ms.
        const double ref = 0.25;
        std::vector<double> g;
        for (int s = pre + burst + lat; s + 48 <= in.getNumSamples(); s += 48) g.push_back (maxAbs (out, s, s + 48) / ref);
        const double g0 = g[2], gEnd = std::min (1.0, g.back());
        for (size_t i = 0; i < g.size(); ++i)
            if (g[i] >= g0 + 0.632 * (gEnd - g0)) return (double) i;
        return 3000.0;
    };
    const double fast = recoveryMs (0.004), slow = recoveryMs (1.5);
    check (slow > 2.0 * fast, "release: sustained gain reduction recovers more than 2x slower than a transient (100 ms base)",
           "63% recovery after a 4 ms burst: " + juce::String (fast, 0) + " ms, after a 1.5 s burst: " + juce::String (slow, 0) + " ms");
}

NL_TEST (master_oversAndGainReductionHistory)
{
    // Overs counting on the hub: 7 samples above the threshold.
    {
        auto hub = makeHub (kFs);
        Buf b (2, 48000);
        b.clear();
        for (int k = 0; k < 7; ++k) b.setSample (0, 2000 + 1000 * k, 0.9f);
        b.setSample (0, 9500, 0.4f);
        hub->setOverThreshold (0.5f, false);
        feed (*hub, b, 100);
        check (hub->read().overs == 7, "MeterHub counts samples above the over threshold", juce::String ((int) hub->read().overs) + " overs");
        hub->resetIntegrated();
        feed (*hub, b, 100, 0, 1000);
        check (hub->read().overs == 0, "resetIntegrated clears the overs counter");
    }
    // Brickwall and True Peak produce no overs on hot material; the GR history shows the limiting.
    for (int mode : { 1, 2 })
    {
        Rig r (kFs, mode, false, true, -1.0);
        auto in = pinkStereo (48000 * 3, 9.0);
        r.run (in, 256);
        const auto s = r.ms.meters().read();
        float hist[MeterHub::kGrHistoryPoints];
        const int got = r.ms.meters().readGrHistory (hist, MeterHub::kGrHistoryPoints);
        float mn = 0;
        for (int i = 0; i < got; ++i) mn = std::min (mn, hist[i]);
        check (s.overs == 0 && got == 300 && mn < -3.0f && mn > -40.0f, juce::String (mode == 1 ? "Brickwall" : "True Peak") + ": no overs on +10 dB pink noise, GR history has 10 ms points",
               juce::String ((int) s.overs) + " overs, " + juce::String (got) + " history points, deepest " + juce::String (mn, 1) + " dB");
    }
    // Block-level gain reduction reporting.
    {
        Rig r (kFs, 1, false, true, -6.0);
        auto in = sine (kFs, 1000.0, 1.0f, 9600);
        r.run (in, 4800);
        check (r.ms.getGainReductionDb() < -4.0f && r.ms.getGainReductionDb() > -8.0f, "getGainReductionDb reports about -6 dB for a full-scale sine into a -6 dB ceiling",
               juce::String (r.ms.getGainReductionDb(), 2) + " dB");
        Rig off (kFs, 1, false, false, -6.0);
        off.run (in, 4800);
        check (off.ms.getGainReductionDb() == 0.0f, "no gain reduction reported while LIMIT is off");
    }
}

//==============================================================================
// D6: BS.1770-4 K-weighting, EBU Tech 3341 / 3342.
NL_TEST (master_meterKWeighting)
{
    auto hub = makeHub (48000.0);
    const auto& s1 = hub->kStage (0);
    const auto& s2 = hub->kStage (1);
    const bool ok = std::abs (s1.b0 - 1.53512485958697) < 1e-6 && std::abs (s1.b1 + 2.69169618940638) < 1e-6 && std::abs (s1.b2 - 1.19839281085285) < 1e-6
                    && std::abs (s1.a1 + 1.69065929318241) < 1e-6 && std::abs (s1.a2 - 0.73248077421585) < 1e-6
                    && std::abs (s2.a1 + 1.99004745483398) < 1e-6 && std::abs (s2.a2 - 0.99007225036621) < 1e-6;
    check (ok, "K-weighting coefficients match BS.1770-4 Tables 1 and 2 at 48 kHz");
    // The design generalises: 1 kHz gain is the same at every rate (tolerance 0.02 dB).
    double ref = 0, ref10 = 0;
    bool same = true;
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        BiquadD a, b;
        KWeighting::design (fs, a, b);
        auto mag = [&] (double f)
        {
            const std::complex<double> z = std::polar (1.0, 2.0 * kPiD * f / fs), zi = 1.0 / z;
            auto h = [&] (const BiquadD& q) { return (q.b0 + q.b1 * zi + q.b2 * zi * zi) / (1.0 + q.a1 * zi + q.a2 * zi * zi); };
            return 20.0 * std::log10 (std::abs (h (a) * h (b)));
        };
        const double g = mag (1000.0), g10 = mag (10000.0);
        if (ref == 0) { ref = g; ref10 = g10; }
        same &= std::abs (g - ref) < 0.03 && std::abs (g10 - ref10) < 0.1;
    }
    check (same && std::abs (ref - 0.70) < 0.05, "K-weighting designed for 44.1 / 48 / 88.2 / 96 / 192 kHz has the same response at 1 kHz (+0.70 dB) and 10 kHz",
           "1 kHz " + juce::String (ref, 3) + " dB, 10 kHz " + juce::String (ref10, 3) + " dB");
}

NL_TEST (master_ebu3341_stationary)
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        struct C { const char* n; double level; };
        for (auto c : { C { "case 1 (-23 dBFS)", -23.0 }, C { "case 2 (-33 dBFS)", -33.0 } })
        {
            auto hub = makeHub (fs);
            feed (*hub, tones (fs, { { c.level, 20.0 } }), 480);
            const auto s = hub->read();
            check (std::abs (s.momentary - c.level) <= 0.1 && std::abs (s.shortTerm - c.level) <= 0.1 && std::abs (s.integrated - c.level) <= 0.1,
                   "EBU 3341 " + juce::String (c.n) + " @ " + juce::String (fs, 0) + " Hz: M, S, I = " + juce::String (c.level, 1) + " +-0.1 LUFS",
                   "M " + juce::String (s.momentary, 2) + ", S " + juce::String (s.shortTerm, 2) + ", I " + juce::String (s.integrated, 2));
        }
    }
    // BS.1770 calibration: a -18 dBFS-peak 1 kHz stereo sine reads -18 LUFS (Tech 3341 2.9).
    auto hub = makeHub (kFs);
    feed (*hub, tones (kFs, { { -18.0, 10.0 } }), 512);
    check (std::abs (hub->read().integrated + 18.0) <= 0.1, "calibration: 1 kHz stereo sine at -18 dBFS peak reads -18.0 LUFS", juce::String (hub->read().integrated, 2));
}

NL_TEST (master_ebu3341_gating)
{
    struct C { const char* n; std::vector<Seg> segs; };
    const std::vector<C> cases {
        { "case 3: 10 s -36, 60 s -23, 10 s -36", { { -36, 10 }, { -23, 60 }, { -36, 10 } } },
        { "case 4: -72, -36, -23 (60 s), -36, -72", { { -72, 10 }, { -36, 10 }, { -23, 60 }, { -36, 10 }, { -72, 10 } } },
        { "case 5: 20 s -26, 20.1 s -20, 20 s -26", { { -26, 20 }, { -20, 20.1 }, { -26, 20 } } } };
    for (auto& c : cases)
    {
        auto hub = makeHub (kFs);
        feed (*hub, tones (kFs, c.segs), 480);
        const double i = hub->read().integrated;
        check (std::abs (i + 23.0) <= 0.1, "EBU 3341 " + juce::String (c.n) + ": I = -23.0 +-0.1 LUFS", juce::String (i, 3));
    }
}

NL_TEST (master_ebu3341_timeVarying)
{
    // Case 9: (1.34 s at -20, 1.66 s at -30) x5 -> S = -23.0, constant after 3 s.
    {
        std::vector<Seg> segs;
        for (int k = 0; k < 5; ++k) { segs.push_back ({ -20, 1.34 }); segs.push_back ({ -30, 1.66 }); }
        auto b = tones (kFs, segs);
        auto hub = makeHub (kFs);
        double worst = 0;
        for (int pos = 0; pos + 480 <= b.getNumSamples(); pos += 480)
        {
            feed (*hub, b, 480, pos, pos + 480);
            if (pos > (int) (3.1 * kFs)) worst = std::max (worst, std::abs (hub->read().shortTerm + 23.0));
        }
        check (worst <= 0.1, "EBU 3341 case 9: short-term loudness -23.0 +-0.1 LUFS, constant after 3 s", "max deviation " + juce::String (worst, 3) + " LU");
    }
    // Case 12: (0.18 s at -20, 0.22 s at -30) x25 -> M = -23.0, constant after 1 s.
    {
        std::vector<Seg> segs;
        for (int k = 0; k < 25; ++k) { segs.push_back ({ -20, 0.18 }); segs.push_back ({ -30, 0.22 }); }
        auto b = tones (kFs, segs);
        auto hub = makeHub (kFs);
        double worst = 0;
        for (int pos = 0; pos + 96 <= b.getNumSamples(); pos += 96)
        {
            feed (*hub, b, 96, pos, pos + 96);
            if (pos > (int) (1.0 * kFs)) worst = std::max (worst, std::abs (hub->read().momentary + 23.0));
        }
        check (worst <= 0.1, "EBU 3341 case 12: momentary loudness -23.0 +-0.1 LUFS, constant after 1 s", "max deviation " + juce::String (worst, 3) + " LU");
    }
}

NL_TEST (master_ebu3341_maxima)
{
    bool ok10 = true, ok13 = true;
    double w10 = 0, w13 = 0;
    for (int i = 0; i < 20; ++i)
    {
        // Case 10 (file based, one file per i): i*0.15 s silence, 3 s at -23, 1 s silence.
        auto hub = makeHub (kFs);
        feed (*hub, tones (kFs, { { kSilence, i * 0.15 }, { -23, 3.0 }, { kSilence, 1.0 } }), 480);
        const double d = std::abs (hub->read().maxShortTerm + 23.0);
        w10 = std::max (w10, d);
        ok10 &= d <= 0.1;
        // Case 13: i*20 ms silence, 400 ms at -23, 1 s silence.
        auto hub2 = makeHub (kFs);
        feed (*hub2, tones (kFs, { { kSilence, i * 0.02 }, { -23, 0.4 }, { kSilence, 1.0 } }), 480);
        const double d2 = std::abs (hub2->read().maxMomentary + 23.0);
        w13 = std::max (w13, d2);
        ok13 &= d2 <= 0.1;
    }
    check (ok10, "EBU 3341 case 10: max short-term = -23.0 +-0.1 LUFS for each of 20 segments", "worst " + juce::String (w10, 3) + " LU");
    check (ok13, "EBU 3341 case 13: max momentary = -23.0 +-0.1 LUFS for each of 20 segments", "worst " + juce::String (w13, 3) + " LU");

    // Cases 11 and 14 (live): one long file, 20 successive maxima.
    {
        std::vector<Seg> segs;
        // i*0.15 s silence, 3 s at -38+i dBFS, (3 - i*0.15) s silence
        for (int i = 0; i < 20; ++i) { segs.push_back ({ kSilence, i * 0.15 }); segs.push_back ({ -38.0 + i, 3.0 }); segs.push_back ({ kSilence, 3.0 - i * 0.15 }); }
        auto b = tones (kFs, segs);
        auto hub = makeHub (kFs);
        double worst = 0;
        int pos = 0;
        for (int i = 0; i < 20; ++i)
        {
            const int len = (int) std::llround ((i * 0.15 + 3.0 + (3.0 - i * 0.15)) * kFs);
            feed (*hub, b, 480, pos, pos + len);
            pos += len;
            worst = std::max (worst, std::abs (hub->read().maxShortTerm - (-38.0 + i)));
        }
        check (worst <= 0.1, "EBU 3341 case 11 (live): successive max short-term = -38, -37, ... -19 +-0.1 LUFS", "worst " + juce::String (worst, 3) + " LU");
    }
    {
        std::vector<Seg> segs;
        // i*20 ms silence, 400 ms at -38+i dBFS, (400 - i*20) ms silence
        for (int i = 0; i < 20; ++i) { segs.push_back ({ kSilence, i * 0.02 }); segs.push_back ({ -38.0 + i, 0.4 }); segs.push_back ({ kSilence, 0.4 - i * 0.02 }); }
        auto b = tones (kFs, segs);
        auto hub = makeHub (kFs);
        double worst = 0;
        int pos = 0;
        for (int i = 0; i < 20; ++i)
        {
            const int len = (int) std::llround ((i * 0.02 + 0.4 + (0.4 - i * 0.02)) * kFs);
            feed (*hub, b, 480, pos, pos + len);
            pos += len;
            worst = std::max (worst, std::abs (hub->read().maxMomentary - (-38.0 + i)));
        }
        check (worst <= 0.1, "EBU 3341 case 14 (live): successive max momentary = -38, -37, ... -19 +-0.1 LUFS", "worst " + juce::String (worst, 3) + " LU");
    }
}

NL_TEST (master_ebu3341_truePeak)
{
    for (double fs : { 44100.0, 48000.0 })
    {
        bool allOk = true;
        juce::String detail;
        for (auto& c : ebuTruePeakCases (fs))
        {
            const double tp = meterTruePeakDb (c.signal, fs);
            const double ref = refTruePeakDb (c.signal, 0);
            const bool ok = tp >= c.nominalDb - 0.4 && tp <= c.nominalDb + 0.2;
            // The nominal value of the synthesised cases depends on our own anti-alias filter, so
            // they must also agree with the accurate reference.
            const bool vsRef = tp >= ref - 0.4 && tp <= ref + 0.2;
            allOk &= ok && vsRef;
            detail << c.name.substring (0, 2) << ": " << juce::String (tp, 2) << " (ref " << juce::String (ref, 2) << ")  ";
        }
        check (allOk, "EBU 3341 cases 15-23 @ " + juce::String (fs, 0) + " Hz: true peak within +0.2/-0.4 dB of the expected value", detail);
    }
}

NL_TEST (master_ebu3342_loudnessRange)
{
    struct C { const char* n; std::vector<Seg> segs; double lra; };
    const std::vector<C> cases {
        { "case 1: -20 / -30", { { -20, 20 }, { -30, 20 } }, 10 },
        { "case 2: -20 / -15", { { -20, 20 }, { -15, 20 } }, 5 },
        { "case 3: -40 / -20", { { -40, 20 }, { -20, 20 } }, 20 },
        { "case 4: -50 / -35 / -20 / -35 / -50", { { -50, 20 }, { -35, 20 }, { -20, 20 }, { -35, 20 }, { -50, 20 } }, 15 } };
    for (auto& c : cases)
    {
        auto segs = c.segs;
        segs.push_back ({ kSilence, 1.5 }); // flush the 3 s window, as the document asks for file measurements
        // The silence adds -inf short-term values, which the absolute gate removes.
        auto hub = makeHub (kFs);
        feed (*hub, tones (kFs, segs), 480);
        const double lra = hub->read().lra;
        check (std::abs (lra - c.lra) <= 1.0, "EBU 3342 " + juce::String (c.n) + ": LRA = " + juce::String (c.lra, 0) + " +-1 LU", juce::String (lra, 2) + " LU");
        check (std::abs (lra - c.lra) <= 0.25, "EBU 3342 " + juce::String (c.n) + ": LRA within 0.25 LU (own target)", juce::String (lra, 3) + " LU");
    }
}

NL_TEST (master_meterOfflineRealtimeEquivalence)
{
    // A programme with level changes, silence and a loud transient section.
    const double fs = 48000.0;
    std::vector<Buf> parts;
    parts.push_back (tones (fs, { { -30, 7.3 }, { -20, 11.1 }, { kSilence, 2.0 }, { -26, 9.7 } }));
    auto pink = pinkStereo ((int) (fs * 15), -6.0, 21);
    pink.applyGain (0.5f);
    parts.push_back (pink);
    parts.push_back (drums (fs, 8.0, -1.0));
    auto prog = concat (parts);
    MeterSnapshot ref;
    bool first = true, same = true;
    for (int block : { 64, 4096, 1, 777 })
    {
        auto hub = makeHub (fs, 4096);
        feed (*hub, prog, block);
        const auto s = hub->read();
        if (first) { ref = s; first = false; continue; }
        same &= s.integrated == ref.integrated && s.lra == ref.lra && s.truePeakDb == ref.truePeakDb && s.maxMomentary == ref.maxMomentary
                && s.maxShortTerm == ref.maxShortTerm && s.samplePeakDb == ref.samplePeakDb && s.momentary == ref.momentary && s.shortTerm == ref.shortTerm
                && s.vuDb == ref.vuDb && s.correlation == ref.correlation;
    }
    // One huge block ("offline render") as well.
    {
        auto hub = makeHub (fs, 4096);
        feed (*hub, prog, prog.getNumSamples());
        const auto s = hub->read();
        same &= s.integrated == ref.integrated && s.lra == ref.lra && s.truePeakDb == ref.truePeakDb;
    }
    check (same, "offline and realtime blocks (1 / 64 / 777 / 4096 / whole file) give identical integrated, LRA, true peak, maxima, VU and correlation",
           "I " + juce::String (ref.integrated, 3) + " LUFS, LRA " + juce::String (ref.lra, 2) + " LU, TP " + juce::String (ref.truePeakDb, 2) + " dBTP");
}

NL_TEST (master_vuBallistics)
{
    auto hub = makeHub (kFs);
    const float amp = (float) dbToLin (-14.0);
    const int n = (int) (2.0 * kFs);
    Buf b = sine (kFs, 1000.0, amp, n);
    std::vector<double> v;
    for (int pos = 0; pos + 32 <= n; pos += 32)
    {
        feed (*hub, b, 32, pos, pos + 32);
        v.push_back (hub->read().vuDb);
    }
    const double expectedDb = -14.0 - 3.0103; // RMS of the sine
    const double final = v.back();
    // Linear response curve.
    double peak = 0, tp99 = -1;
    const double fin = std::pow (10.0, final / 20.0);
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double lin = std::pow (10.0, v[i] / 20.0);
        peak = std::max (peak, lin);
        if (tp99 < 0 && lin >= 0.99 * fin) tp99 = (double) (i + 1) * 32.0 / kFs;
    }
    const double overshoot = peak / fin - 1.0;
    check (std::abs (final - expectedDb) < 0.05, "VU reads the RMS of a sine (rectified average, calibrated)", juce::String (final, 3) + " dB, expected " + juce::String (expectedDb, 3));
    check (std::abs (tp99 - 0.3) <= 0.03, "VU step response reaches 99% in 300 ms +-10%", juce::String (tp99 * 1000.0, 1) + " ms");
    check (overshoot >= 0.010 && overshoot <= 0.015, "VU step response overshoots by 1.0 to 1.5%", juce::String (overshoot * 100.0, 2) + " %");
}

NL_TEST (master_meterLifecycle)
{
    auto hub = makeHub (kFs);
    auto b = tones (kFs, { { -23, 10 } });
    feed (*hub, b, 512);
    const double i1 = hub->read().integrated;
    // A second hub fed the same: nothing else (editor, reads) influences the result.
    for (int k = 0; k < 1000; ++k) (void) hub->read();
    check (hub->read().integrated == i1, "reading the snapshot does not disturb the measurement");

    // Pause while stopped.
    hub->setPauseIntegrationWhenStopped (true);
    hub->setTransportPlaying (false);
    feed (*hub, tones (kFs, { { -10, 5 } }), 512);
    check (hub->read().integrated == i1 && hub->read().integrating == 0 && hub->read().momentary > -12.0,
           "integration pauses while the transport is stopped (momentary still follows)", "I " + juce::String (hub->read().integrated, 2));
    hub->setTransportPlaying (true);
    feed (*hub, tones (kFs, { { -10, 5 } }), 512);
    check (hub->read().integrated > i1 + 1.0 && hub->read().integrating == 1, "integration resumes when the transport plays", "I " + juce::String (hub->read().integrated, 2));

    // Reset on play.
    hub->setResetIntegrationOnPlay (true);
    hub->setTransportPlaying (false);
    feed (*hub, tones (kFs, { { -40, 1 } }), 512);
    hub->setTransportPlaying (true);
    feed (*hub, tones (kFs, { { -30, 3 } }), 512);
    check (std::abs (hub->read().integrated + 30.0) < 0.2, "reset-on-play restarts integration at the transport start", "I " + juce::String (hub->read().integrated, 2));

    // UI reset: visible at once, applied by the audio thread at the next block.
    hub->resetIntegrated();
    const auto s = hub->read();
    check (s.integrated < -199.0 && s.truePeakDb < -199.0 && s.maxMomentary < -199.0, "resetIntegrated shows through read() immediately");
    feed (*hub, tones (kFs, { { -23, 1 } }), 512);
    check (hub->read().maxShortTerm < -26.0 && std::abs (hub->read().integrated + 23.0) < 0.2, "...and the audio thread restarts integration from the reset point",
           "I " + juce::String (hub->read().integrated, 2) + ", max S " + juce::String (hub->read().maxShortTerm, 2));

    // PLR.
    auto h2 = makeHub (kFs);
    feed (*h2, tones (kFs, { { -23, 10 } }), 512);
    const auto p = h2->read();
    check (std::abs (p.plr - (p.truePeakDb - p.integrated)) < 1e-9 && std::abs (p.plr - 0.0) < 0.6, "PLR = true peak max - integrated loudness (sine: about 0 LU)", juce::String (p.plr, 2) + " LU");
}

// Seqlock: a reader on another thread always sees a consistent snapshot.
NL_TEST (master_snapshotIsConsistentAcrossThreads)
{
    auto hub = makeHub (kFs);
    auto prog = concat ({ tones (kFs, { { -30, 3 }, { -12, 3 } }), pinkStereo ((int) (kFs * 4), -3.0) });
    std::atomic<bool> done { false };
    std::atomic<int> bad { 0 }, reads { 0 };
    std::thread reader ([&]
    {
        uint32_t lastSeq = 0;
        while (! done.load())
        {
            const auto s = hub->read();
            ++reads;
            bool consistent = true;
            if (s.integrated > -199.0 && s.truePeakDb > -199.0) consistent &= std::abs (s.plr - ((double) s.truePeakDb - s.integrated)) < 1e-9;
            consistent &= s.sequence >= lastSeq;
            lastSeq = s.sequence;
            if (! consistent) ++bad;
        }
    });
    for (int pass = 0; pass < 200 && reads.load() < 20000; ++pass)
    {
        hub->reset();
        feed (*hub, prog, 64);
    }
    done = true;
    reader.join();
    check (bad.load() == 0 && reads.load() > 100, "seqlock: reader on another thread never sees a torn snapshot", juce::String (reads.load()) + " reads, " + juce::String (bad.load()) + " inconsistent");
}

NL_TEST (master_processingDoesNotAllocate)
{
    for (auto& cfg : allModes())
    {
        Rig r (kFs, cfg.mode, cfg.hq, true, -3.0);
        auto in = pinkStereo (4800, 3.0);
        r.run (in, 128); // warm-up
        Buf work (2, 128);
        float hist[MeterHub::kGrHistoryPoints];
        allocationCount = 0;
        countAllocations = true;
        for (int b = 0; b < 60; ++b)
        {
            if (b == 10) r.setOn (false);
            if (b == 20) { r.setOn (true); r.setCeiling (-8.0); }
            if (b == 30) r.ms.meters().resetIntegrated();
            if (b == 40) r.set (cfg.mode, ! cfg.hq, true, -3.0, 100.0); // quality switch while running
            if (b == 50) r.set (cfg.mode, cfg.hq, true, -3.0, 100.0);
            for (int c = 0; c < 2; ++c) std::copy_n (in.getReadPointer (c) + b * 64, 64, work.getWritePointer (c));
            r.ms.setParams (r.snap);
            float* io[2] { work.getWritePointer (0), work.getWritePointer (1) };
            r.ms.process (io, 2, 64);
            (void) r.ms.meters().read();
            (void) r.ms.meters().readGrHistory (hist, MeterHub::kGrHistoryPoints);
        }
        countAllocations = false;
        check (allocationCount.load() == 0, juce::String (cfg.name) + ": limiter and meters do not allocate while processing", juce::String (allocationCount.load()) + " allocations");
    }
}
} // namespace tool

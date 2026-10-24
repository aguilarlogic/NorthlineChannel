// Measurement suite "master": limiter modes, true-peak behaviour, loudness conformance, MeterHub cost.
#include "Measure.h"
#include "BenchTimer.h"
#include "../tests/MasterSignals.h"

using namespace nl;

namespace tool
{
using namespace master;

namespace
{
juce::String pf (bool ok) { return ok ? "pass" : "FAIL"; }

// Thread CPU time (see BenchTimer.h), so other processes on the machine do not count.
double seconds()
{
    return CpuClock::threadSeconds();
}

// CPU cost of a callable processing `audioSeconds` of audio, as a percentage of one core (best of `runs`).
template <typename Fn>
double cpuPercent (double audioSeconds, int runs, Fn&& fn)
{
    juce::ScopedNoDenormals noDenormals;
    fn(); // warm-up
    double best = 1.0e9;
    for (int i = 0; i < runs; ++i)
    {
        const double t0 = seconds();
        fn();
        best = std::min (best, seconds() - t0);
    }
    return 100.0 * best / audioSeconds;
}
} // namespace

NL_MEASURE (master, "Limiter modes (latency, true peak, THD, release), EBU 3341/3342 conformance table, MeterHub CPU")
{
    const double ceilDb = -1.0;

    //--------------------------------------------------------------------------
    r.h2 ("Limiter latency");
    r.para ("Reported latency in samples. It depends on the mode and the quality switch only, never on LIMIT on/off (verified in `master_latencyDependsOnModeAndQualityOnly`).");
    r.table ({ "Mode", "44.1 kHz", "48 kHz", "96 kHz", "192 kHz" });
    for (auto& cfg : allModes())
    {
        juce::StringArray cells;
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Rig off (fs, cfg.mode, cfg.hq, false), on (fs, cfg.mode, cfg.hq, true);
            cells.add (juce::String (off.ms.latency()) + (off.ms.latency() == on.ms.latency() ? "" : " (differs when on!)"));
        }
        r.row ({ cfg.name, cells[0], cells[1], cells[2], cells[3] });
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Output peak with the input driven 10 dB over the ceiling (-1 dB)");
    r.para ("Three levels of peak measurement on the whole output: sample peak, the BS.1770-4 4x meter (the definition of dBTP, what `MeterHub` reads) and an independent 16x reference with 64 taps per phase (stricter). "
            "True Peak mode keeps 0.1 dB of margin, so the 4x meter reads at most ceiling - 0.1 dB.");
    r.table ({ "Mode", "Signal", "Sample peak (dBFS)", "4x meter (dBTP)", "16x reference (dBTP)", "Deepest GR (dB)", "Overs" });
    for (double fs : { 48000.0, 96000.0 })
    {
        std::vector<std::pair<juce::String, Buf>> sigs;
        sigs.emplace_back ("pink", pinkStereo ((int) (fs * (quick ? 2 : 5)), ceilDb + 10.0));
        sigs.emplace_back ("drums (band-limited)", drums (fs, quick ? 3.0 : 8.0, ceilDb + 10.0));
        sigs.emplace_back ("sine sweep 20 Hz - 0.45 fs", sweep (fs, quick ? 3.0 : 8.0, ceilDb + 10.0));
        for (auto& cfg : allModes())
            for (auto& [name, sig] : sigs)
            {
                Rig rig (fs, cfg.mode, cfg.hq, true, ceilDb);
                auto out = rig.run (sig, 256);
                float hist[MeterHub::kGrHistoryPoints];
                const int got = rig.ms.meters().readGrHistory (hist, MeterHub::kGrHistoryPoints);
                float deepest = 0;
                for (int i = 0; i < got; ++i) deepest = std::min (deepest, hist[i]);
                r.row ({ juce::String (cfg.name) + " @ " + juce::String (fs / 1000.0, 1) + " kHz", name,
                         fmt (linToDb (std::max (maxAbs (out), maxAbs (out, 0, -1, 1))), 2), fmt (meterTruePeakDb (out, fs), 2),
                         fmt (std::max (refTruePeakDb (out, 0), refTruePeakDb (out, 1)), 2), fmt (deepest, 1),
                         juce::String ((int) rig.ms.meters().read().overs) });
            }
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Inter-sample peaks: fs/4 sine at 45 degrees, true peak 2 dB over a -3 dB ceiling");
    r.para ("The samples of this signal sit 1 dB under the ceiling while the waveform exceeds it by 2 dB.");
    r.table ({ "Mode", "Output true peak, 16x reference (dBTP)", "Verdict" });
    {
        Buf s (2, 48000);
        for (int i = 0; i < 48000; ++i)
        {
            const float v = (float) (dbToLin (-3.0 + 2.0) * std::sin (2.0 * kPiD * 0.25 * i + kPiD / 4.0));
            s.setSample (0, i, v);
            s.setSample (1, i, v);
        }
        for (auto& cfg : allModes())
        {
            Rig rig (48000.0, cfg.mode, cfg.hq, true, -3.0);
            const double tp = refTruePeakDb (rig.run (s, 256), 0, 9600);
            r.row ({ cfg.name, fmt (tp, 2), tp <= -3.0 + 0.1 ? "within ceiling + 0.1 dB" : "over the ceiling (mode does not detect inter-sample peaks)" });
        }
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Distortion with the input 3 dB over the ceiling (-6 dB), steady sine");
    r.para ("THD of harmonics 2 to 10 relative to the fundamental, measured over 100 ms after 0.9 s of settling at 48 kHz. Brickwall and True Peak hold a steady gain, so a sine is essentially undistorted; the Safety Clip is a waveshaper and distorts by design.");
    r.table ({ "Mode", "100 Hz", "1 kHz", "5 kHz" });
    for (auto& cfg : allModes())
    {
        juce::StringArray cells;
        for (double f : { 100.0, 1000.0, 5000.0 })
        {
            const int N = 4800, total = 48000;
            Rig rig (48000.0, cfg.mode, cfg.hq, true, -6.0);
            auto in = sine (48000.0, f, (float) dbToLin (-3.0), total);
            auto out = rig.run (in, 256);
            cells.add (fmt (thdDb (out, total - N - 500, N, (int) (f * 0.1)), 1) + " dB");
        }
        r.row ({ cfg.name, cells[0], cells[1], cells[2] });
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Gain reduction and program-dependent release");
    r.para ("Brickwall, ceiling -6 dB, release knob = base time. A burst 12 dB over the ceiling is followed by a quiet 1 kHz tone; the time for the tone to recover 63% of its gain is read from the output envelope. "
            "Short bursts use the fast stage (base/4), sustained limiting the slow stage (4 x base), blended by how long the reduction has lasted.");
    r.table ({ "Release (ms)", "Burst 4 ms", "Burst 50 ms", "Burst 300 ms", "Burst 1.5 s" });
    for (double rel : { 30.0, 100.0, 300.0 })
    {
        juce::StringArray cells;
        for (double burst : { 0.004, 0.05, 0.3, 1.5 })
        {
            const double fs = 48000.0;
            const int pre = (int) (0.3 * fs), blen = (int) (burst * fs), post = (int) (4.0 * fs);
            Buf in (2, pre + blen + post);
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const double a = (i >= pre && i < pre + blen) ? 2.0 : 0.25;
                const float v = (float) (a * std::sin (2.0 * kPiD * 1000.0 * i / fs));
                in.setSample (0, i, v);
                in.setSample (1, i, v);
            }
            Rig rig (fs, 1, false, true, -6.0, rel);
            auto out = rig.run (in, 128);
            const int lat = rig.ms.latency();
            std::vector<double> g;
            for (int s = pre + blen + lat; s + 48 <= in.getNumSamples(); s += 48) g.push_back (maxAbs (out, s, s + 48) / 0.25);
            const double g0 = g[2], gEnd = std::min (1.0, g.back());
            int idx = -1;
            for (size_t i = 0; i < g.size(); ++i)
                if (g[i] >= g0 + 0.632 * (gEnd - g0)) { idx = (int) i; break; }
            cells.add (idx < 0 ? juce::String ("> 4000 ms") : juce::String (idx) + " ms");
        }
        r.row ({ fmt (rel, 0), cells[0], cells[1], cells[2], cells[3] });
    }
    r.endTable();

    r.table ({ "Ceiling", "Sine at the ceiling, GR (dB)", "Sine 6 dB over the ceiling, GR (dB)" });
    for (double c : { -1.0, -6.0, -12.0 })
    {
        juce::StringArray cells;
        for (double drive : { 0.0, 6.0 })
        {
            Rig rig (48000.0, 1, false, true, c);
            auto in = sine (48000.0, 1000.0, (float) dbToLin (c + drive), 24000);
            rig.run (in, 4800);
            cells.add (fmt (rig.ms.getGainReductionDb(), 2));
        }
        r.row ({ fmt (c, 0) + " dB", cells[0], cells[1] });
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Loudness conformance (MeterHub)");
    r.para ("EBU Tech 3341 minimum-requirement signals (stereo cases generated here; cases 6 to 8 need 5.0-channel or authentic programme material and are not covered) and Tech 3342 LRA cases 1 to 4. "
            "Tolerance +-0.1 LU for loudness, +-1 LU for LRA (the measured value is also within 0.01 LU in these synthetic cases), true peak +0.2/-0.4 dB.");
    r.table ({ "Case", "Expected", "Measured", "Tolerance", "Result" });
    auto cell = [&] (const juce::String& name, double expected, double measured, double tolLow, double tolHigh, const juce::String& unit)
    {
        const bool ok = measured >= expected - tolLow && measured <= expected + tolHigh;
        r.row ({ name, fmt (expected, 1) + " " + unit, fmt (measured, 2) + " " + unit, "+" + fmt (tolHigh, 1) + " / -" + fmt (tolLow, 1), pf (ok) });
    };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const juce::String at = " @ " + fmt (fs / 1000.0, 1) + " kHz";
        {
            auto hub = makeHub (fs);
            feed (*hub, tones (fs, { { -23, 20 } }), 480);
            auto s = hub->read();
            cell ("3341 #1 M" + at, -23.0, s.momentary, 0.1, 0.1, "LUFS");
            cell ("3341 #1 S" + at, -23.0, s.shortTerm, 0.1, 0.1, "LUFS");
            cell ("3341 #1 I" + at, -23.0, s.integrated, 0.1, 0.1, "LUFS");
        }
        {
            auto hub = makeHub (fs);
            feed (*hub, tones (fs, { { -33, 20 } }), 480);
            cell ("3341 #2 I" + at, -33.0, hub->read().integrated, 0.1, 0.1, "LUFS");
        }
        if (fs != 48000.0 && quick) continue;
    }
    {
        const double fs = 48000.0;
        auto run = [&] (std::vector<Seg> segs) { auto h = makeHub (fs); feed (*h, tones (fs, segs), 480); return h->read(); };
        cell ("3341 #3 I (-36 / -23 / -36)", -23.0, run ({ { -36, 10 }, { -23, 60 }, { -36, 10 } }).integrated, 0.1, 0.1, "LUFS");
        cell ("3341 #4 I (-72 / -36 / -23 / -36 / -72)", -23.0, run ({ { -72, 10 }, { -36, 10 }, { -23, 60 }, { -36, 10 }, { -72, 10 } }).integrated, 0.1, 0.1, "LUFS");
        cell ("3341 #5 I (-26 / -20 / -26)", -23.0, run ({ { -26, 20 }, { -20, 20.1 }, { -26, 20 } }).integrated, 0.1, 0.1, "LUFS");

        std::vector<Seg> s9;
        for (int k = 0; k < 5; ++k) { s9.push_back ({ -20, 1.34 }); s9.push_back ({ -30, 1.66 }); }
        {
            auto b = tones (fs, s9);
            auto hub = makeHub (fs);
            double lo = 1e9, hi = -1e9;
            for (int pos = 0; pos + 480 <= b.getNumSamples(); pos += 480)
            {
                feed (*hub, b, 480, pos, pos + 480);
                if (pos > (int) (3.1 * fs)) { lo = std::min (lo, hub->read().shortTerm); hi = std::max (hi, hub->read().shortTerm); }
            }
            cell ("3341 #9 S min after 3 s", -23.0, lo, 0.1, 0.1, "LUFS");
            cell ("3341 #9 S max after 3 s", -23.0, hi, 0.1, 0.1, "LUFS");
        }
        std::vector<Seg> s12;
        for (int k = 0; k < 25; ++k) { s12.push_back ({ -20, 0.18 }); s12.push_back ({ -30, 0.22 }); }
        {
            auto b = tones (fs, s12);
            auto hub = makeHub (fs);
            double lo = 1e9, hi = -1e9;
            for (int pos = 0; pos + 96 <= b.getNumSamples(); pos += 96)
            {
                feed (*hub, b, 96, pos, pos + 96);
                if (pos > (int) (1.0 * fs)) { lo = std::min (lo, hub->read().momentary); hi = std::max (hi, hub->read().momentary); }
            }
            cell ("3341 #12 M min after 1 s", -23.0, lo, 0.1, 0.1, "LUFS");
            cell ("3341 #12 M max after 1 s", -23.0, hi, 0.1, 0.1, "LUFS");
        }
        double w10 = 0, w13 = 0;
        for (int i = 0; i < 20; ++i)
        {
            w10 = std::max (w10, std::abs (run ({ { kSilence, i * 0.15 }, { -23, 3.0 }, { kSilence, 1.0 } }).maxShortTerm + 23.0));
            w13 = std::max (w13, std::abs (run ({ { kSilence, i * 0.02 }, { -23, 0.4 }, { kSilence, 1.0 } }).maxMomentary + 23.0));
        }
        cell ("3341 #10 max S, worst of 20 segments", -23.0, -23.0 + w10, 0.1, 0.1, "LUFS (worst deviation shown)");
        cell ("3341 #13 max M, worst of 20 segments", -23.0, -23.0 + w13, 0.1, 0.1, "LUFS (worst deviation shown)");
        {
            std::vector<Seg> s11;
            for (int i = 0; i < 20; ++i) { s11.push_back ({ kSilence, i * 0.15 }); s11.push_back ({ -38.0 + i, 3.0 }); s11.push_back ({ kSilence, 3.0 - i * 0.15 }); }
            auto b = tones (fs, s11);
            auto hub = makeHub (fs);
            double worst = 0;
            int pos = 0;
            for (int i = 0; i < 20; ++i)
            {
                const int len = (int) std::llround (6.0 * fs);
                feed (*hub, b, 480, pos, pos + len);
                pos += len;
                worst = std::max (worst, std::abs (hub->read().maxShortTerm - (-38.0 + i)));
            }
            cell ("3341 #11 live max S, worst of 20 values", 0.0, worst, 0.1, 0.1, "LU (deviation)");
            std::vector<Seg> s14;
            for (int i = 0; i < 20; ++i) { s14.push_back ({ kSilence, i * 0.02 }); s14.push_back ({ -38.0 + i, 0.4 }); s14.push_back ({ kSilence, 0.4 - i * 0.02 }); }
            auto b14 = tones (fs, s14);
            auto hub14 = makeHub (fs);
            worst = 0;
            pos = 0;
            for (int i = 0; i < 20; ++i)
            {
                const int len = (int) std::llround (0.8 * fs);
                feed (*hub14, b14, 480, pos, pos + len);
                pos += len;
                worst = std::max (worst, std::abs (hub14->read().maxMomentary - (-38.0 + i)));
            }
            cell ("3341 #14 live max M, worst of 20 values", 0.0, worst, 0.1, 0.1, "LU (deviation)");
        }
        for (double tfs : { 44100.0, 48000.0 })
            for (auto& c : ebuTruePeakCases (tfs))
                cell ("3341 #" + c.name.substring (0, 2) + " true peak @ " + fmt (tfs / 1000.0, 1) + " kHz", c.nominalDb, meterTruePeakDb (c.signal, tfs), 0.4, 0.2, "dBTP");

        struct L { const char* n; std::vector<Seg> segs; double lra; };
        const std::vector<L> lras {
            { "3342 #1 LRA (-20 / -30)", { { -20, 20 }, { -30, 20 }, { kSilence, 1.5 } }, 10 },
            { "3342 #2 LRA (-20 / -15)", { { -20, 20 }, { -15, 20 }, { kSilence, 1.5 } }, 5 },
            { "3342 #3 LRA (-40 / -20)", { { -40, 20 }, { -20, 20 }, { kSilence, 1.5 } }, 20 },
            { "3342 #4 LRA (-50 / -35 / -20 / -35 / -50)", { { -50, 20 }, { -35, 20 }, { -20, 20 }, { -35, 20 }, { -50, 20 }, { kSilence, 1.5 } }, 15 } };
        for (auto& l : lras) cell (l.n, l.lra, run (l.segs).lra, 1.0, 1.0, "LU");
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Offline / realtime equivalence");
    {
        const double fs = 48000.0;
        auto prog = concat ({ tones (fs, { { -30, 7.3 }, { -20, 11.1 }, { kSilence, 2.0 }, { -26, 9.7 } }), drums (fs, 8.0, -1.0) });
        r.table ({ "Block size", "Integrated (LUFS)", "LRA (LU)", "True peak (dBTP)", "Max momentary", "Max short-term" });
        for (int block : { 1, 64, 512, 4096, prog.getNumSamples() })
        {
            auto hub = makeHub (fs, 4096);
            feed (*hub, prog, block);
            const auto s = hub->read();
            r.row ({ block == prog.getNumSamples() ? juce::String ("whole file") : juce::String (block), fmt (s.integrated, 4), fmt (s.lra, 3), fmt (s.truePeakDb, 3), fmt (s.maxMomentary, 4), fmt (s.maxShortTerm, 4) });
        }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("CPU cost");
    r.para ("Percentage of one core (thread CPU time, best of several runs), stereo, 64-sample blocks, loud pink noise, flush-to-zero on. The MeterHub target is 0.05% at 48 kHz stereo.");
    r.table ({ "Configuration", "48 kHz", "96 kHz" });
    {
        const double secs = quick ? 5.0 : 20.0;
        const int runs = quick ? 3 : 5;
        auto hubCost = [&] (double fs, int channels, float peakDb)
        {
            auto sig = pinkStereo ((int) (fs * secs), peakDb);
            auto hub = makeHub (fs, 64);
            const float* ch[2] { sig.getReadPointer (0), sig.getReadPointer (1) };
            return cpuPercent (secs, runs, [&]
            {
                for (int pos = 0; pos + 64 <= sig.getNumSamples(); pos += 64)
                {
                    const float* p[2] { ch[0] + pos, ch[1] + pos };
                    hub->process (p, channels, 64);
                }
            });
        };
        r.row ({ "MeterHub, stereo, peaks at -1 dBFS", fmt (hubCost (48000.0, 2, -1.0f), 3) + " %", fmt (hubCost (96000.0, 2, -1.0f), 3) + " %" });
        r.row ({ "MeterHub, stereo, peaks at -20 dBFS", fmt (hubCost (48000.0, 2, -20.0f), 3) + " %", fmt (hubCost (96000.0, 2, -20.0f), 3) + " %" });
        r.row ({ "MeterHub, mono, peaks at -1 dBFS", fmt (hubCost (48000.0, 1, -1.0f), 3) + " %", fmt (hubCost (96000.0, 1, -1.0f), 3) + " %" });

        auto masterCost = [&] (double fs, const ModeCfg& cfg, bool on, float peakDb)
        {
            auto sig = pinkStereo ((int) (fs * secs), peakDb);
            Rig rig (fs, cfg.mode, cfg.hq, on, -1.0, 100.0, 64);
            Buf work (2, 64);
            return cpuPercent (secs, runs, [&]
            {
                for (int pos = 0; pos + 64 <= sig.getNumSamples(); pos += 64)
                {
                    for (int c = 0; c < 2; ++c) std::copy_n (sig.getReadPointer (c) + pos, 64, work.getWritePointer (c));
                    rig.ms.setParams (rig.snap);
                    float* io[2] { work.getWritePointer (0), work.getWritePointer (1) };
                    rig.ms.process (io, 2, 64);
                }
            });
        };
        for (auto& cfg : allModes())
        {
            r.row ({ juce::String ("MasterSection (limiter + MeterHub), ") + cfg.name + ", LIMIT on, +10 dB drive", fmt (masterCost (48000.0, cfg, true, 9.0f), 3) + " %", fmt (masterCost (96000.0, cfg, true, 9.0f), 3) + " %" });
            r.row ({ juce::String ("MasterSection, ") + cfg.name + ", LIMIT off", fmt (masterCost (48000.0, cfg, false, -6.0f), 3) + " %", fmt (masterCost (96000.0, cfg, false, -6.0f), 3) + " %" });
        }
    }
    r.endTable();
}
} // namespace tool

#include "../PreHarness.h"
#include "Measure.h"
#include "dsp/SatEngine.h"
#include <chrono>

// PRE measurements: saturation alias and staging, distortion, filter and Weight response,
// switching splatter, latency and CPU. Numbers only; thresholds live in tools/tests/TestPre.cpp.

using namespace tool::pre;

namespace
{
const char* typeName (int t)
{
    static const char* names[] { "Odd", "Even", "Heavy", "Crush", "Tape", "Tube", "Transformer" };
    return names[t];
}

// Stimulus level for alias: -6 dBFS peak, the level used by the saturation test.
constexpr double kAliasPeak = 0.5;

// Writes one Markdown table row from a list of cells.
void rowOf (tool::MeasureReport& r, const juce::StringArray& cells)
{
    r.text << "| " << cells.joinIntoString (" | ") << " |\n";
}
} // namespace

//==============================================================================
NL_MEASURE (pre, "PRE: saturation alias, staging, distortion, filters, Weight, switching, latency and CPU")
{
    //--------------------------------------------------------------------------
    // Calibration tables are built on first use (prepare), so time them first in this process.
    {
        const auto t0 = std::chrono::steady_clock::now();
        (void) nl::sat::calibration (false);
        (void) nl::sat::calibration (true);
        const auto t1 = std::chrono::steady_clock::now();
        r.h2 ("Calibration");
        r.para ("Building the gain-staging tables (Eco and HQ, all types, first use) took " + juce::String (std::chrono::duration<double, std::milli> (t1 - t0).count(), 0) + " ms.");
    }

    const std::vector<double> rates { 44100.0, 48000.0, 96000.0 };
    const std::vector<double> freqs { 1000.0, 5000.0, 10000.0 };
    const std::vector<int> drives { 0, 50, 100 };

    //--------------------------------------------------------------------------
    r.h2 ("Aliasing (ROADMAP section 1)");
    r.para ("Energy outside the true harmonics of a bin-exact sine, relative to total energy, in dB. "
            "Input: sine at -6 dBFS peak, 8192-point Blackman analysis after 0.25 s of warm-up. "
            "Targets: Eco <= -40 dB (Odd, Even, Heavy) and <= -30 dB (Crush); HQ <= -80 dB (Odd, Even) and <= -70 dB (Heavy, Crush).");
    r.table ({ "Type", "Drive", "Quality", "1k 44.1", "1k 48", "1k 96", "5k 44.1", "5k 48", "5k 96", "10k 44.1", "10k 48", "10k 96" });
    for (int t = 0; t < 7; ++t)
        for (int d : drives)
            for (int hq = 0; hq < 2; ++hq)
            {
                if (quick && d == 50) continue;
                juce::StringArray row { typeName (t), juce::String (d) + " %", hq ? "HQ" : "Eco" };
                for (double f : freqs)
                    for (double fs : rates)
                    {
                        if (quick && fs != 48000.0) { row.add ("-"); continue; }
                        const auto p = satParams (t, (float) d, hq == 1, true);
                        const double f0 = binExact (f, fs, 8192);
                        const size_t warm = (size_t) (0.25 * fs);
                        const auto x = sineVec (fs, f0, kAliasPeak, warm + 8192);
                        const auto y = render (p, fs, x);
                        row.add (juce::String (aliasDb (y, warm, fs, f0), 1));
                    }
                rowOf (r, row);
            }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("Gain staging at the Operating Level (-18 dBFS RMS, auto gain on)");
    r.para ("Change in RMS level (output minus input, dB), worst case over sines at 100 Hz, 1 kHz, 5 kHz and pink noise. "
            "Target: within +-0.5 dB for every type and drive. Also the small-signal gain at 0 % drive (target 0 +- 0.1 dB).");
    for (int hq = 0; hq < 2; ++hq)
    {
        r.h3 (hq ? "HQ (4x)" : "Eco (1x)");
        std::vector<int> stepDrives;
        for (int d = 0; d <= 100; d += quick ? 50 : 10) stepDrives.push_back (d);
        juce::StringArray head { "Type" };
        for (int d : stepDrives) head.add (juce::String (d) + " %");
        head.add ("Small signal, 0 %");
        r.text << "| " << head.joinIntoString (" | ") << " |\n|";
        for (int i = 0; i < head.size(); ++i) r.text << "---|";
        r.text << "\n";

        const double fs = 48000.0;
        const size_t warm = (size_t) (0.3 * fs), len = (size_t) (0.7 * fs);
        for (int t = 0; t < 7; ++t)
        {
            juce::StringArray row { typeName (t) };
            for (int d : stepDrives)
            {
                double worst = 0.0;
                const auto p = satParams (t, (float) d, hq == 1, true);
                for (int s = 0; s < 4; ++s)
                {
                    const auto x = s < 3 ? sineRmsVec (fs, binExact (s == 0 ? 100.0 : s == 1 ? 1000.0 : 5000.0, fs, 4096), kOpLevel, warm + len)
                                         : pinkVec (warm + len, kOpLevel, 7);
                    const auto y = render (p, fs, x);
                    worst = std::max (worst, std::abs (rmsDbOf (y, warm, warm + len) - rmsDbOf (x, warm, warm + len)));
                }
                row.add (juce::String (worst, 2));
            }
            row.add (juce::String (gainDb (satParams (t, 0.0f, hq == 1, true), fs, binExact (1000.0, fs, 4096), -40.0), 2));
            rowOf (r, row);
        }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("Harmonic distortion versus level (auto gain on, 1 kHz sine, 48 kHz, Eco)");
    {
        const std::vector<double> levels { -40.0, -30.0, -24.0, -18.0, -12.0, -6.0 };
        juce::StringArray head { "Type", "Drive" };
        for (double l : levels) head.add (juce::String (l, 0) + " dBFS RMS");
        r.text << "| " << head.joinIntoString (" | ") << " |\n|";
        for (int i = 0; i < head.size(); ++i) r.text << "---|";
        r.text << "\n";
        const double fs = 48000.0, f = binExact (1000.0, fs, 16384);
        const size_t warm = (size_t) (0.3 * fs), len = 16384;
        for (int t = 0; t < 7; ++t)
            for (int d : { 0, 100 })
            {
                juce::StringArray row { typeName (t), juce::String (d) + " %" };
                for (double l : levels)
                {
                    const auto p = satParams (t, (float) d, false, true);
                    const auto x = sineRmsVec (fs, f, l, warm + len);
                    const auto y = render (p, fs, x);
                    row.add (juce::String (thdDb (y, warm, fs, f), 1));
                }
                rowOf (r, row);
            }
        r.endTable();
        r.para ("THD in dB (20 log of the harmonic amplitude ratio, harmonics 2 to 10).");
    }

    //--------------------------------------------------------------------------
    r.h2 ("Filter response error (Butterworth prototype, cut-off 1 kHz, 48 kHz)");
    {
        const double fs = 48000.0, fc = 1000.0;
        r.table ({ "Slope", "Low-pass max |error| (dB), 20 Hz to 0.5 fc", "High-pass max |error| (dB), 2 fc to 16 kHz" });
        for (int s = 0; s < 6; ++s)
        {
            const int order = s == 0 ? 1 : s == 1 ? 2 : s == 2 ? 3 : s == 3 ? 4 : s == 4 ? 6 : 8;
            double errLP = 0.0, errHP = 0.0;
            for (int k = 0; k < 16; ++k)
            {
                const double fLP = 20.0 * std::pow (0.5 * fc / 20.0, k / 15.0);
                const double fHP = 2.0 * fc * std::pow (16000.0 / (2.0 * fc), k / 15.0);
                auto analytic = [&] (double f, bool hp)
                {
                    const double w = std::tan (nl::kPiD * f / fs) / std::tan (nl::kPiD * fc / fs);
                    const double m = std::pow (w, 2.0 * order);
                    return hp ? -10.0 * std::log10 (1.0 + 1.0 / m) : -10.0 * std::log10 (1.0 + m);
                };
                PreParams lp; lp.lpOn = true; lp.lpFreq = (float) fc; lp.lpSlope = s; lp.lpRes = 0.7071f;
                PreParams hp; hp.hpOn = true; hp.hpFreq = (float) fc; hp.hpSlope = s; hp.hpRes = 0.7071f;
                errLP = std::max (errLP, std::abs (gainDb (lp, fs, fLP) - analytic (fLP, false)));
                errHP = std::max (errHP, std::abs (gainDb (hp, fs, fHP) - analytic (fHP, true)));
            }
            static const int slopeDb[6] { 6, 12, 18, 24, 36, 48 };
            r.row ({ juce::String (slopeDb[s]) + " dB/oct", juce::String (errLP, 3), juce::String (errHP, 3) });
        }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("Weight (bump at the frequency, 48 kHz)");
    {
        const double fs = 48000.0;
        r.table ({ "Weight freq", "Track HP", "Amount", "Measured gain at bump (dB)" });
        for (double f : { 40.0, 72.0, 120.0 })
            for (int amt = 0; amt < 3; ++amt)
            {
                PreParams p; p.weightOn = true; p.weightFreq = (float) f; p.weightAmt = amt;
                const double g = gainDb (p, fs, binExact (f, fs, 16384), -30.0);
                r.row ({ juce::String (f, 0) + " Hz", "off", juce::String (2 * (amt + 1)) + " dB", juce::String (g, 2) });
            }
        {
            // Weight contribution alone: gain with Weight on minus gain with Weight off, HP 80 Hz (12 dB/oct) on.
            PreParams on; on.hpOn = true; on.hpFreq = 80.0f; on.hpSlope = 1; on.weightOn = true; on.weightTrack = true; on.weightAmt = 2;
            PreParams off = on; off.weightOn = false;
            const double f = binExact (80.0 * std::sqrt (2.0), fs, 16384);
            const double bump = gainDb (on, fs, f, -30.0) - gainDb (off, fs, f, -30.0);
            r.row ({ juce::String (f, 1) + " Hz (HP 80, track)", "on", "6 dB", juce::String (bump, 2) });
        }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("Switching splatter (-60 dB target, sine carrier re 50 ms window, 48 kHz)");
    {
        const double fs = 48000.0, f0 = binExact (1000.0, fs, 4096);
        const size_t n = (size_t) (1.0 * fs), t0 = (size_t) (0.5 * fs);
        auto test = [&] (const char* name, PreParams before, PreParams after, double peak, bool hq)
        {
            before.hq = after.hq = hq;
            const auto x = sineVec (fs, f0, peak, n);
            const auto y = renderAutomated (before, fs, x, 512, { { t0, after } });
            r.row ({ name, hq ? "HQ" : "Eco", juce::String (splatterDb (y, t0 - (size_t) (0.025 * fs), fs, f0), 1) });
        };
        r.table ({ "Change", "Quality", "Splatter (dB)" });
        for (int hq = 0; hq < 2; ++hq)
        {
            test ("Type Odd -> Tube, drive 0 %", satParams (0, 0.0f), satParams (5, 0.0f), 0.1, hq == 1);
            test ("Type Tape -> Transformer, drive 0 %", satParams (4, 0.0f), satParams (6, 0.0f), 0.1, hq == 1);
            test ("Auto gain off -> on, drive 100 %, -40 dBFS", satParams (2, 100.0f, false, false), satParams (2, 100.0f, false, true), 0.0141, hq == 1);
            {
                PreParams a, b; a.hpOn = b.hpOn = true; a.hpSlope = 1; b.hpSlope = 3;
                test ("HP slope 12 -> 24 dB/oct", a, b, 0.1, hq == 1);
                PreParams c = b; c.hpSlope = 5;
                test ("HP slope 24 -> 48 dB/oct", b, c, 0.1, hq == 1);
            }
            {
                PreParams a, b; b.weightOn = true; b.weightAmt = 2;
                test ("Weight off -> on (+6 dB)", a, b, 0.1, hq == 1);
                PreParams c = b; c.weightAmt = 0;
                test ("Weight +6 -> +2 dB", b, c, 0.1, hq == 1);
            }
        }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("Latency");
    r.table ({ "Quality", "Latency (samples)", "Latency (ms at 48 kHz)" });
    {
        for (int hq = 0; hq < 2; ++hq)
        {
            nl::PreModule m;
            PreParams p; p.hq = hq == 1;
            m.prepare (48000.0, 512);
            m.setParams (p);
            m.reset();
            r.row ({ hq ? "HQ" : "Eco", juce::String (m.latency()), juce::String (1000.0 * m.latency() / 48000.0, 3) });
        }
    }
    r.endTable();

    //--------------------------------------------------------------------------
    r.h2 ("PRE CPU per configuration (stereo, 48 kHz, 512-sample blocks, % of one core)");
    {
        const double fs = 48000.0;
        const size_t n = (size_t) fs;
        const auto x = pinkVec (n, -18.0, 3);
        auto cpu = [&] (const PreParams& p)
        {
            // Times process() only: prepare() and the parameter setup run outside the timed loop.
            auto m = std::make_unique<nl::PreModule>();
            m->prepare (fs, 512);
            m->setParams (p);
            m->reset();
            double best = 1.0e9;
            for (int rep = 0; rep < (quick ? 1 : 3); ++rep)
            {
                std::vector<float> l = x, rr = x;
                const auto t0 = std::chrono::steady_clock::now();
                for (size_t pos = 0; pos < n; pos += 512)
                {
                    const int len = (int) std::min<size_t> (512, n - pos);
                    float* ch[2] { l.data() + pos, rr.data() + pos };
                    m->process (ch, 2, len);
                }
                const auto t1 = std::chrono::steady_clock::now();
                best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            }
            return 100.0 * best / ((double) n / fs); // percent of one core for real-time audio
        };
        r.table ({ "Configuration", "CPU (%)" });
        r.row ({ "Bypassed (PRE on, nothing enabled)", juce::String (cpu (PreParams {}), 3) });
        for (int hq = 0; hq < 2; ++hq)
        {
            const char* q = hq ? "HQ" : "Eco";
            r.row ({ juce::String (q) + ", saturation Odd 50 %", juce::String (cpu (satParams (0, 50.0f, hq == 1)), 3) });
            r.row ({ juce::String (q) + ", saturation Crush 100 %", juce::String (cpu (satParams (3, 100.0f, hq == 1)), 3) });
            r.row ({ juce::String (q) + ", saturation Tube 100 %", juce::String (cpu (satParams (5, 100.0f, hq == 1)), 3) });
            PreParams all = satParams (6, 100.0f, hq == 1);
            all.hpOn = true; all.hpSlope = 5; all.lpOn = true; all.lpSlope = 5; all.weightOn = true; all.weightAmt = 2; all.satTone = 40.0f;
            r.row ({ juce::String (q) + ", everything on (48 dB HP and LP, Weight, tone)", juce::String (cpu (all), 3) });
        }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("Harmonic profile (1 kHz sine at -18 dBFS RMS, auto gain on, HQ, 48 kHz)");
    {
        const double fs = 48000.0, f = binExact (1000.0, fs, 16384);
        const size_t warm = (size_t) (0.3 * fs), len = 16384;
        r.table ({ "Type", "Drive", "H2 (dB)", "H3 (dB)", "H4 (dB)", "H5 (dB)" });
        for (int t = 0; t < 7; ++t)
            for (int d : { 50, 100 })
            {
                const auto y = render (satParams (t, (float) d, true), fs, sineRmsVec (fs, f, kOpLevel, warm + len));
                r.row ({ typeName (t), juce::String (d) + " %", juce::String (harmonicDb (y, warm, fs, f, 2), 1),
                         juce::String (harmonicDb (y, warm, fs, f, 3), 1), juce::String (harmonicDb (y, warm, fs, f, 4), 1),
                         juce::String (harmonicDb (y, warm, fs, f, 5), 1) });
            }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("ADAA order at 1x (Odd, Even; raw chain without calibration, 48 kHz)");
    r.para ("Order 1 (with its droop correction) is used in Eco. Order 2 has the zero (1 + z^-1 + z^-2) at fs/3, so it droops "
            "to -9.5 dB at 12 kHz and to a null at 16 kHz; no FIR can correct that in the passband. Order 2 is therefore used "
            "only at 4x (HQ), where its null sits above the passband. Alias: 5 kHz at the drive gain of 100 %, relative to total. "
            "Passband: small signal, re 1 kHz. Order 2 is shown uncorrected.");
    {
        const double fs = 48000.0;
        auto raw = [&] (int type, int order, double gainDbIn, const std::vector<float>& x)
        {
            nl::sat::Coefs c;
            nl::sat::DroopFir droop;
            if (order == 1) droop = nl::sat::makeDroopFir (1, fs, fs);
            else { droop.taps = 1; droop.h[0] = 1.0f; }
            c.configure (type, fs, order, droop);
            auto st = std::make_unique<nl::sat::State>();
            st->reset (c);
            nl::sat::Ramp rp;
            rp.g = std::pow (10.0, gainDbIn / 20.0);
            std::vector<float> y (x.size());
            for (size_t p = 0; p < x.size(); p += 32)
            {
                nl::sat::updateBias (c, *st);
                nl::sat::process (*st, c, x.data() + p, y.data() + p, (int) std::min<size_t> (32, x.size() - p), rp);
            }
            return y;
        };
        auto levelDb = [&] (const std::vector<float>& y, double f)
        {
            const double a = fitPeak (y, 12000, 12000 + 16384, fs, f) / std::sqrt (2.0) / std::pow (10.0, -40.0 / 20.0);
            return 20.0 * std::log10 (std::max (a, 1.0e-12));
        };
        r.table ({ "Type", "Order", "Alias 5 kHz, 100 % (dB)", "4 kHz re 1 kHz (dB)", "8 kHz re 1 kHz (dB)", "16 kHz re 1 kHz (dB)" });
        for (int t : { (int) nl::shaper::Odd, (int) nl::shaper::Even })
            for (int order : { 1, 2 })
            {
                const double f0 = binExact (5000.0, fs, 8192);
                const auto ya = raw (t, order, nl::shaper::driveDb (t, 100.0f), sineVec (fs, f0, 0.5, 8192 + 12000));
                const double alias = aliasDb (ya, 12000, fs, f0);
                const double ff = binExact (1000.0, fs, 16384);
                const auto ys = raw (t, order, 0.0, sineRmsVec (fs, ff, -40.0, 12000 + 16384));
                const double g1 = levelDb (ys, ff);
                std::vector<juce::String> cells { typeName (t), juce::String (order), juce::String (alias, 1) };
                for (double f : { 4000.0, 8000.0, 16000.0 })
                {
                    const double fb = binExact (f, fs, 16384);
                    const auto yb = raw (t, order, 0.0, sineRmsVec (fs, fb, -40.0, 12000 + 16384));
                    cells.push_back (juce::String (levelDb (yb, fb) - g1, 2));
                }
                juce::StringArray row;
                for (auto& c : cells) row.add (c);
                rowOf (r, row);
            }
        r.endTable();
    }

    //--------------------------------------------------------------------------
    r.h2 ("Tape: level-dependent HF loss and head bump (Eco, 48 kHz)");
    {
        const double fs = 48000.0;
        r.table ({ "Input level (dBFS RMS)", "Drive", "10 kHz re 1 kHz (dB)" });
        for (double lvl : { -40.0, -30.0, -24.0, -18.0, -12.0 })
        {
            const auto p = satParams (nl::shaper::Tape, 100.0f);
            const double hf = gainDb (p, fs, 10000.0, lvl) - gainDb (p, fs, 1000.0, lvl);
            r.row ({ juce::String (lvl, 0), "100 %", juce::String (hf, 2) });
        }
        const auto flat = satParams (nl::shaper::Tape, 0.0f);
        r.row ({ "-40 (small signal)", "0 %", juce::String (gainDb (flat, fs, 10000.0, -40.0) - gainDb (flat, fs, 1000.0, -40.0), 2) });
        r.row ({ "head bump 60 Hz re 1 kHz, -40", "0 %", juce::String (gainDb (flat, fs, binExact (60.0, fs, 16384), -40.0) - gainDb (flat, fs, 1000.0, -40.0), 2) });
        r.endTable();
    }
}

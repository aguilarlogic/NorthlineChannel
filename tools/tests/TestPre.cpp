#include "PreHarness.h"

// PRE acceptance tests (ROADMAP section 7.2, DEVELOPMENT.md rules 1 to 5). Thresholds live here;
// the `pre` measurement suite reports the numbers behind them. Run with `NorthlineTool test pre_`.
using namespace tool::pre;
using tool::check;

namespace
{
const char* typeName (int t)
{
    static const char* names[] { "Odd", "Even", "Heavy", "Crush", "Tape", "Tube", "Transformer" };
    return names[t];
}

constexpr double kFs = 48000.0;
constexpr int kNumTypes = nl::shaper::numTypes;
constexpr int kHqLat = nl::sat::SatSection::kHqLatency;

// Sum of harmonic powers (2..15) of f0 split at `split` Hz: returns 10 log10 (above / below).
double harmonicHighLowDb (const std::vector<float>& y, size_t from, double f0, double split)
{
    const int N = 16384;
    const double f = binExact (f0, kFs, N);
    const int bin = (int) std::lround (f * N / kFs);
    const auto P = powerSpectrum (y, from, N);
    auto band = [&] (int center)
    {
        double s = 0.0;
        for (int k = center - 3; k <= center + 3; ++k) if (k >= 0 && k <= N / 2) s += P[(size_t) k];
        return s;
    };
    double hi = 0.0, lo = 0.0;
    for (int h = 2; h <= 15 && h * bin <= N / 2; ++h)
    {
        const double fh = h * f;
        (fh >= split ? hi : lo) += band (h * bin);
    }
    return 10.0 * std::log10 (hi / std::max (lo, 1.0e-30));
}
} // namespace

//==============================================================================
NL_TEST (pre_stagingWithinHalfDb)
{
    const size_t warm = (size_t) (0.3 * kFs), len = (size_t) (0.5 * kFs);
    const double freqs[3] { 100.0, 1000.0, 5000.0 };
    for (int t = 0; t < kNumTypes; ++t)
    {
        double worst = 0.0;
        juce::String where;
        for (int d = 0; d <= 100; d += 10)
            for (int s = 0; s < 4; ++s)
            {
                const auto x = s < 3 ? sineRmsVec (kFs, binExact (freqs[s], kFs, 4096), kOpLevel, warm + len)
                                     : pinkVec (warm + len, kOpLevel, 7);
                const auto y = render (satParams (t, (float) d), kFs, x);
                const double dl = std::abs (rmsDbOf (y, warm, warm + len) - rmsDbOf (x, warm, warm + len));
                if (dl > worst)
                {
                    worst = dl;
                    where = juce::String (d) + " % drive, " + (s < 3 ? juce::String (freqs[s], 0) + " Hz" : juce::String ("pink"));
                }
            }
        check (worst <= 0.5, juce::String (typeName (t)) + ": level change within 0.5 dB at the operating level, 0 to 100 % drive",
               "worst " + juce::String (worst, 2) + " dB (" + where + ")");
    }
}

NL_TEST (pre_zeroDriveNearClean)
{
    const size_t warm = (size_t) (0.3 * kFs), N = 16384;
    const double f = binExact (1000.0, kFs, N);
    for (int t = 0; t < kNumTypes; ++t)
    {
        const double small = gainDb (satParams (t, 0.0f), kFs, 1000.0, -40.0);
        check (std::abs (small) <= 0.1, juce::String (typeName (t)) + ": small-signal gain at 0 % drive is 0 +- 0.1 dB",
               juce::String (small, 3) + " dB");
        const auto y = render (satParams (t, 0.0f), kFs, sineRmsVec (kFs, f, kOpLevel, warm + N));
        const double thd = thdDb (y, warm, kFs, f, (int) N);
        check (thd <= -70.0, juce::String (typeName (t)) + ": THD at the operating level, 0 % drive, is -70 dB or better",
               juce::String (thd, 1) + " dB");
    }
}

NL_TEST (pre_driveLouderWithoutAutoGain)
{
    const size_t warm = (size_t) (0.3 * kFs), len = (size_t) (0.5 * kFs);
    const auto x = sineRmsVec (kFs, binExact (1000.0, kFs, 4096), kOpLevel, warm + len);
    for (int t = 0; t < kNumTypes; ++t)
    {
        const auto lo = render (satParams (t, 0.0f, false, false), kFs, x);
        const auto hi = render (satParams (t, 100.0f, false, false), kFs, x);
        const double gain = rmsDbOf (hi, warm, warm + len) - rmsDbOf (lo, warm, warm + len);
        check (gain >= 3.0, juce::String (typeName (t)) + ": with auto gain off, drive makes the output louder (0 to 100 %)",
               juce::String (gain, 1) + " dB");
    }
}

NL_TEST (pre_aliasEco)
{
    // Targets (ROADMAP section 1): <= -40 dB for every type except Crush (<= -30 dB), 5 kHz sine, full drive.
    const size_t warm = (size_t) (0.25 * kFs);
    const double f0 = binExact (5000.0, kFs, 8192);
    for (int t = 0; t < kNumTypes; ++t)
    {
        const auto y = render (satParams (t, 100.0f), kFs, sineVec (kFs, f0, 0.5, warm + 8192));
        const double alias = aliasDb (y, warm, kFs, f0);
        const double target = t == nl::shaper::Crush ? -30.0 : -40.0;
        check (alias <= target, juce::String (typeName (t)) + ": Eco alias at 5 kHz, full drive, is at or below " + juce::String (target, 0) + " dB",
               juce::String (alias, 1) + " dB");
    }
}

NL_TEST (pre_aliasHq)
{
    // Targets (ROADMAP section 1): <= -80 dB (Odd, Even) and <= -70 dB (Heavy, Crush and the new types).
    const size_t warm = (size_t) (0.25 * kFs);
    const double f0 = binExact (5000.0, kFs, 8192);
    for (int t = 0; t < kNumTypes; ++t)
    {
        const auto y = render (satParams (t, 100.0f, true), kFs, sineVec (kFs, f0, 0.5, warm + 8192));
        const double alias = aliasDb (y, warm, kFs, f0);
        const double target = (t == nl::shaper::Odd || t == nl::shaper::Even) ? -80.0 : -70.0;
        check (alias <= target, juce::String (typeName (t)) + ": HQ alias at 5 kHz, full drive, is at or below " + juce::String (target, 0) + " dB",
               juce::String (alias, 1) + " dB");
    }
}

NL_TEST (pre_adaaPassbandFlat)
{
    // The droop of ADAA is corrected before the shaper, so the saturation path is flat to 16 kHz at 48 kHz.
    for (int t = 0; t < kNumTypes; ++t)
        for (int d : { 0, 50 })
            for (bool hq : { false, true })
            {
                // Tape's dynamic HF loss is level-dependent by design; the calibration sets its HF trim at the
                // operating level, so quieter signals sit up to about 0.5 dB above it (checked separately).
                if (t == nl::shaper::Tape) continue;
                const auto p = satParams (t, (float) d, hq);
                const double ref = gainDb (p, kFs, 1000.0, -40.0);
                const double err = std::max (std::abs (gainDb (p, kFs, 10000.0, -40.0) - ref), std::abs (gainDb (p, kFs, 16000.0, -40.0) - ref));
                check (err <= 0.3, juce::String (typeName (t)) + (hq ? " HQ" : " Eco") + ", " + juce::String (d) + " % drive: flat +-0.3 dB to 16 kHz",
                       juce::String (err, 3) + " dB");
            }
}

NL_TEST (pre_latencyAndDelay)
{
    for (bool hq : { false, true })
    {
        PreModule m;
        m.prepare (kFs, 512);
        PreParams p = satParams (0, 30.0f, hq);
        m.setParams (p);
        m.reset();
        const int want = hq ? kHqLat : 0;
        check (m.latency() == want, juce::String ("latency with ") + (hq ? "HQ" : "Eco") + " is " + juce::String (want) + " samples",
               juce::String (m.latency()));
        p.satOn = false;
        m.setParams (p);
        m.reset();
        check (m.latency() == want, "latency does not depend on saturation on/off", juce::String (m.latency()));
    }

    // HQ with everything off: the audio is still delayed by the HQ latency (dry path).
    const auto x = pinkVec (4096, -20.0, 5);
    PreParams off;
    off.hq = true;
    const auto y = render (off, kFs, x);
    const int L = kHqLat;
    double diff = 0.0;
    for (size_t i = (size_t) L; i < x.size(); ++i) diff = std::max (diff, std::abs ((double) y[i] - (double) x[i - (size_t) L]));
    check (diff <= 1.0e-6, "HQ with saturation off is the input delayed by the HQ latency", "max diff " + juce::String (diff, 9));
}

NL_TEST (pre_mixLatencyAligned)
{
    // Parallel mix is phase-coherent with the oversampled path: at 0 % drive, 50 % and 100 % mix give the
    // input delayed by the HQ latency, with no phase offset at 5 kHz (a sub-sample misalignment would comb).
    const size_t n = 16384, from = 4096;
    const double f = binExact (5000.0, kFs, 8192);
    const auto x = sineVec (kFs, f, 0.1, n);
    for (float mix : { 50.0f, 100.0f })
    {
        auto p = satParams (0, 0.0f, true);
        p.satMix = mix;
        const auto y = render (p, kFs, x);
        // complex amplitudes (C = b - i a for a sin + b cos) of the output and of the delayed input
        auto complexAmp = [&] (const std::vector<float>& v, size_t shift)
        {
            double ss = 0, cc = 0, sc = 0, sy = 0, cy = 0;
            for (size_t i = from; i < n; ++i)
            {
                const double s = std::sin (2.0 * nl::kPiD * f * (double) i / kFs), c = std::cos (2.0 * nl::kPiD * f * (double) i / kFs);
                const double yv = v[i - shift];
                ss += s * s; cc += c * c; sc += s * c; sy += s * yv; cy += c * yv;
            }
            const double det = ss * cc - sc * sc;
            const double a = (sy * cc - cy * sc) / det, b = (cy * ss - sy * sc) / det;
            return std::complex<double> (b, -a);
        };
        const auto ratio = complexAmp (y, 0) / complexAmp (x, (size_t) kHqLat);
        const double levelDb = 20.0 * std::log10 (std::abs (ratio));
        const double phaseDeg = std::arg (ratio) * 180.0 / nl::kPiD;
        check (std::abs (phaseDeg) <= 0.5 && std::abs (levelDb) <= 0.2,
               "HQ saturation at 0 % drive, mix " + juce::String (mix, 0) + " %: output is the input delayed by the latency (5 kHz)",
               juce::String (phaseDeg, 3) + " deg, " + juce::String (levelDb, 3) + " dB");
    }
}

NL_TEST (pre_determinismBlockSizes)
{
    // DEVELOPMENT rule 2: identical output (max diff <= 1e-6) for any host block split, under automation.
    // Every type, Eco and HQ, filters and Weight on, drive / tone / mix / frequency automated every 2048 samples.
    const size_t n = 16384;
    const auto x = pinkVec (n, -18.0, 11);
    const int blocks[] { 1, 7, 37, 64, 511, 2048 };
    double worst = 0.0;
    juce::String where;
    for (int t = 0; t < kNumTypes; ++t)
        for (bool hq : { false, true })
        {
            PreParams start = satParams (t, 30.0f, hq);
            start.hpOn = true; start.hpFreq = 80.0f; start.hpSlope = 2;
            start.lpOn = true; start.lpFreq = 12000.0f; start.lpSlope = 3;
            start.weightOn = true; start.weightAmt = 1; start.weightFreq = 60.0f;

            std::vector<Event> events;
            for (size_t at = 2048, k = 1; at < n; at += 2048, ++k)
            {
                PreParams e = start;
                e.drive = (float) (10.0 + 37.0 * (double) (k % 5));
                e.satTone = (float) (-80.0 + 40.0 * (double) (k % 5));
                e.satMix = (float) (40.0 + 15.0 * (double) (k % 4));
                e.hpFreq = (float) (30.0 + 40.0 * (double) (k % 3));
                e.lpFreq = (float) (4000.0 + 3000.0 * (double) (k % 4));
                e.lpSlope = (int) (k % 4);
                e.weightFreq = (float) (45.0 + 25.0 * (double) (k % 4));
                e.weightAmt = (int) (k % 3);
                events.push_back ({ at, e });
            }

            const auto ref = renderAutomated (start, kFs, x, 2048, events);
            for (int b : blocks)
            {
                const auto y = renderAutomated (start, kFs, x, b, events);
                double d = 0.0;
                for (size_t i = 0; i < n; ++i) d = std::max (d, std::abs ((double) y[i] - (double) ref[i]));
                if (d > worst) { worst = d; where = juce::String (typeName (t)) + (hq ? " HQ" : " Eco") + ", block " + juce::String (b); }
            }
        }
    check (worst <= 1.0e-6, "PRE output is identical across block sizes 1, 7, 37, 64, 511, 2048 (every type, Eco and HQ, automated)",
           "max diff " + juce::String (worst, 9) + " (" + where + ")");
}

NL_TEST (pre_clickFreeSwitching)
{
    // DEVELOPMENT rule 5: splatter <= -60 dB re the carrier in a 50 ms window around each switch.
    const double f0 = binExact (1000.0, kFs, 4096);
    const size_t n = (size_t) kFs, t0 = (size_t) (0.5 * kFs);
    auto run = [&] (const char* what, PreParams before, PreParams after, double peak, bool hq)
    {
        before.hq = after.hq = hq;
        const auto x = sineVec (kFs, f0, peak, n);
        const auto y = renderAutomated (before, kFs, x, 512, { { t0, after } });
        const double s = splatterDb (y, t0 - (size_t) (0.025 * kFs), kFs, f0);
        check (s <= -60.0, juce::String (what) + (hq ? " (HQ)" : " (Eco)") + ": splatter at or below -60 dB", juce::String (s, 1) + " dB");
    };
    for (bool hq : { false, true })
    {
        run ("Type Odd -> Tube, 0 % drive", satParams (0, 0.0f), satParams (5, 0.0f), 0.126, hq);
        run ("Type Tape -> Transformer, 0 % drive", satParams (4, 0.0f), satParams (6, 0.0f), 0.126, hq);
        run ("Auto gain off -> on, 100 % drive", satParams (2, 100.0f, false, false), satParams (2, 100.0f, false, true), 0.0141, hq);
        {
            PreParams a, b; a.hpOn = b.hpOn = true; a.hpSlope = 1; b.hpSlope = 3;
            run ("HP 12 -> 24 dB/oct", a, b, 0.126, hq);
            PreParams c = b; c.hpSlope = 5;
            run ("HP 24 -> 48 dB/oct", b, c, 0.126, hq);
        }
        {
            PreParams a, b; a.lpOn = b.lpOn = true; a.lpSlope = 2; b.lpSlope = 4;
            run ("LP 18 -> 36 dB/oct", a, b, 0.126, hq);
        }
        {
            PreParams a, b; b.weightOn = true; b.weightAmt = 2;
            run ("Weight off -> on (+6 dB)", a, b, 0.126, hq);
            PreParams c = b; c.weightAmt = 0;
            run ("Weight +6 -> +2 dB", b, c, 0.126, hq);
            PreParams d = b; d.weightFreq = 120.0f; d.weightAmt = 2;
            PreParams e = b; e.weightFreq = 40.0f;
            run ("Weight frequency 40 -> 120 Hz", e, d, 0.126, hq);
        }
    }
}

NL_TEST (pre_weightAmount)
{
    // Weight: +2 / +4 / +6 dB at the Weight frequency; with Track HP it sits half an octave above HP.
    for (double f : { 40.0, 72.0, 120.0 })
        for (int amt = 0; amt < 3; ++amt)
        {
            PreParams p; p.weightOn = true; p.weightFreq = (float) f; p.weightAmt = amt;
            const double g = gainDb (p, kFs, binExact (f, kFs, 16384), -30.0);
            check (std::abs (g - 2.0 * (amt + 1)) <= 0.1, juce::String ("Weight ") + juce::String (f, 0) + " Hz, +" + juce::String (2 * (amt + 1)) + " dB",
                   juce::String (g, 2) + " dB");
        }

    auto track = [&] (float hpFreq, double expectHz)
    {
        PreParams on; on.hpOn = true; on.hpFreq = hpFreq; on.hpSlope = 1; on.weightOn = true; on.weightTrack = true; on.weightAmt = 2;
        PreParams off = on; off.weightOn = false;
        const double f = binExact (expectHz, kFs, 16384);
        const double bump = gainDb (on, kFs, f, -30.0) - gainDb (off, kFs, f, -30.0);
        check (std::abs (bump - 6.0) <= 0.2, "Weight Track HP " + juce::String (hpFreq, 0) + " Hz: bump at " + juce::String (expectHz, 1) + " Hz is +6 dB",
               juce::String (bump, 2) + " dB");
    };
    track (80.0f, 80.0 * std::sqrt (2.0));  // half an octave above HP
    track (20.0f, 40.0);                    // clamped to the lower limit
    track (200.0f, 120.0);                  // clamped to the upper limit
}

NL_TEST (pre_filtersButterworth)
{
    // Response within +-0.1 dB of the Butterworth prototype (prewarped at the cut-off) in the passband.
    const double fc = 1000.0;
    const int orders[6] { 1, 2, 3, 4, 6, 8 };
    const int dbPerOct[6] { 6, 12, 18, 24, 36, 48 };
    for (int s = 0; s < 6; ++s)
        for (bool hp : { false, true })
        {
            double err = 0.0;
            for (int k = 0; k < 16; ++k)
            {
                const double f = hp ? 2.0 * fc * std::pow (16000.0 / (2.0 * fc), k / 15.0) : 20.0 * std::pow (0.5 * fc / 20.0, k / 15.0);
                const double w = std::tan (nl::kPiD * f / kFs) / std::tan (nl::kPiD * fc / kFs);
                const double m = std::pow (w, 2.0 * orders[s]);
                const double want = hp ? -10.0 * std::log10 (1.0 + 1.0 / m) : -10.0 * std::log10 (1.0 + m);
                PreParams p;
                if (hp) { p.hpOn = true; p.hpFreq = (float) fc; p.hpSlope = s; } else { p.lpOn = true; p.lpFreq = (float) fc; p.lpSlope = s; }
                err = std::max (err, std::abs (gainDb (p, kFs, f) - want));
            }
            check (err <= 0.1, juce::String (hp ? "HP " : "LP ") + juce::String (dbPerOct[s]) + " dB/oct: passband within 0.1 dB of Butterworth",
                   juce::String (err, 3) + " dB");
        }
}

NL_TEST (pre_resonanceShapesLastSection)
{
    // Resonance scales the Q of the last section for every slope with a resonant section.
    for (int s = 1; s < 6; ++s)
    {
        PreParams a, b;
        a.lpOn = b.lpOn = true; a.lpFreq = b.lpFreq = 1000.0f; a.lpSlope = b.lpSlope = s;
        b.lpRes = 1.414f;
        const double d = gainDb (b, kFs, 1000.0, -40.0) - gainDb (a, kFs, 1000.0, -40.0);
        check (d >= 2.0, "LP slope " + juce::String (s) + ": resonance 1.414 raises the cut-off gain by 2 dB or more",
               juce::String (d, 2) + " dB");
    }
}

NL_TEST (pre_toneCleanIsFlat)
{
    // Tone is a pre-tilt into the shaper with the exact inverse after it: a clean signal stays flat.
    for (float tone : { -100.0f, 100.0f })
    {
        PreParams p = satParams (0, 0.0f);
        p.satTone = tone;
        const double ref = gainDb (p, kFs, 1000.0, -40.0);
        for (double f : { 100.0, 10000.0 })
        {
            const double d = gainDb (p, kFs, f, -40.0) - ref;
            check (std::abs (d) <= 0.3, "tone " + juce::String (tone, 0) + " %: clean response at " + juce::String (f, 0) + " Hz is flat",
                   juce::String (d, 2) + " dB");
        }
    }
}

NL_TEST (pre_toneSteersHarmonics)
{
    // Positive tone moves harmonic energy up, negative tone down (500 Hz at full drive, HQ).
    const size_t warm = (size_t) (0.3 * kFs), len = 16384;
    const double f0 = binExact (500.0, kFs, 16384);
    const auto x = sineRmsVec (kFs, f0, kOpLevel, warm + len);
    auto p = satParams (0, 100.0f, true);
    p.satTone = 100.0f;
    const double up = harmonicHighLowDb (render (p, kFs, x), warm, f0, 2000.0);
    p.satTone = -100.0f;
    const double down = harmonicHighLowDb (render (p, kFs, x), warm, f0, 2000.0);
    check (up - down >= 6.0, "tone +100 % puts more harmonic energy above 2 kHz than -100 % (HQ, full drive)",
           juce::String (up, 1) + " dB vs " + juce::String (down, 1) + " dB");
}

NL_TEST (pre_characterTubeEvenRisesWithLevel)
{
    // Tube: envelope-driven bias, so the second harmonic rises with level.
    const size_t warm = (size_t) (0.3 * kFs), len = 16384;
    const double f0 = binExact (1000.0, kFs, 16384);
    const auto p = satParams (nl::shaper::Tube, 100.0f);
    const double quiet = harmonicDb (render (p, kFs, sineRmsVec (kFs, f0, -30.0, warm + len)), warm, kFs, f0, 2);
    const double loud = harmonicDb (render (p, kFs, sineRmsVec (kFs, f0, -12.0, warm + len)), warm, kFs, f0, 2);
    check (loud - quiet >= 6.0, "Tube: second harmonic rises with level (-30 to -12 dBFS, full drive)",
           juce::String (quiet, 1) + " dB -> " + juce::String (loud, 1) + " dB");
}

NL_TEST (pre_characterTransformerLowWeighted)
{
    // Transformer: low-frequency weighted saturation (pre-emphasis at 90 Hz), measured in HQ.
    const size_t warm = (size_t) (0.3 * kFs), len = 16384;
    const auto p = satParams (nl::shaper::Transformer, 100.0f, true);
    const double fLo = binExact (100.0, kFs, 16384), fHi = binExact (5000.0, kFs, 16384);
    const double lo = thdDb (render (p, kFs, sineRmsVec (kFs, fLo, kOpLevel, warm + len)), warm, kFs, fLo, 16384);
    const double hi = thdDb (render (p, kFs, sineRmsVec (kFs, fHi, kOpLevel, warm + len)), warm, kFs, fHi, 16384);
    check (lo - hi >= 3.0, "Transformer: low-frequency THD exceeds 5 kHz THD by 3 dB or more (HQ, full drive)",
           juce::String (lo, 1) + " dB vs " + juce::String (hi, 1) + " dB");
}

NL_TEST (pre_characterTapeHighFrequencyLossAndHeadBump)
{
    // Tape: high-frequency loss grows with the shaper level (dynamic shelf); a mild head bump near 60 Hz.
    const auto p = satParams (nl::shaper::Tape, 100.0f);
    auto hfRel = [&] (double rmsDb) { return gainDb (p, kFs, 10000.0, rmsDb) - gainDb (p, kFs, 1000.0, rmsDb); };
    const double quiet = hfRel (-40.0), loud = hfRel (-12.0);
    check (loud <= quiet - 0.8, "Tape: 10 kHz falls relative to 1 kHz as the level rises (-40 to -12 dBFS, full drive)",
           juce::String (quiet, 2) + " dB -> " + juce::String (loud, 2) + " dB");

    PreParams flat = satParams (nl::shaper::Tape, 0.0f);
    const double small = gainDb (flat, kFs, 10000.0, -40.0) - gainDb (flat, kFs, 1000.0, -40.0);
    check (std::abs (small) <= 0.5, "Tape at 0 % drive: small-signal response within 0.5 dB of flat to 10 kHz",
           juce::String (small, 2) + " dB");

    const double bump = gainDb (flat, kFs, binExact (60.0, kFs, 16384), -40.0) - gainDb (flat, kFs, 1000.0, -40.0);
    check (bump >= 1.0 && bump <= 2.0, "Tape: head bump of about +1.5 dB at 60 Hz",
           juce::String (bump, 2) + " dB");
}

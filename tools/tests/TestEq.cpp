#include "TestUtil.h"
#include "dsp/EqModule.h"
#include "../EqHelpers.h"
#include <complex>

// EQ acceptance tests: decramped design (roadmap 7.3), eq_char, dynamic bands, band solo,
// click-free discrete switches (D10), determinism (D3, D8) and the display (D13).

using namespace nl;

using namespace eqtest;

namespace
{
// Same for the whole processor (sample-accurate parameter events).
juce::AudioBuffer<float> renderSampleAccurate (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                               const std::vector<tool::AutomationEvent>& events, const juce::AudioBuffer<float>& sc)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    juce::AudioBuffer<float> work (4, block);
    juce::MidiBuffer midi;
    size_t next = 0;
    for (int pos = 0; pos < n; pos += block)
    {
        const int end = std::min (pos + block, n);
        int cur = pos;
        while (cur < end)
        {
            while (next < events.size() && events[next].sample <= cur) { tool::setParam (p, events[next].param, events[next].value); ++next; }
            const int stop = next < events.size() ? std::min (end, events[next].sample) : end;
            const int len = stop - cur;
            work.setSize (4, len, false, false, true);
            for (int c = 0; c < 2; ++c)
            {
                work.copyFrom (c, 0, in, c, cur, len);
                work.copyFrom (2 + c, 0, sc, c, cur, len);
            }
            p.processBlock (work, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, cur, work, c, 0, len);
            cur = stop;
        }
    }
    return out;
}
}

//==============================================================================
// 1. Decramped design: every band type against the analog prototype up to 0.9 * Nyquist.
NL_TEST (eq_designMatchesAnalog)
{
    struct Worst
    {
        double e = 0.0, fc = 0.0, gain = 0.0, q = 0.0;
        int count = 0;
        void note (double err, double f, double g, double q_) { ++count; if (err > e) { e = err; fc = f; gain = g; q = q_; } }
    };

    for (double fs : { 44100.0, 48000.0 })
    {
        eqd::Context ctx;
        ctx.prepare (fs);
        Worst shelves, bellsInner, bellsOuter;
        const double fmax = 0.9 * 0.5 * fs;
        for (int fi = 0; fi < 26; ++fi)
        {
            const double fc = 20.0 * std::pow (1000.0, fi / 25.0);
            for (double gain : { -18.0, -12.0, -6.0, -3.0, -1.0, 1.0, 3.0, 6.0, 12.0, 18.0 })
            {
                struct Case { int band, type; double q; };
                std::vector<Case> cases;
                for (double q : { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0 }) cases.push_back ({ EqModule::mid, 2, q });
                cases.push_back ({ EqModule::low, 0, 0.7071 });  cases.push_back ({ EqModule::low, 1, 1.5 });
                cases.push_back ({ EqModule::high, 0, 0.7071 }); cases.push_back ({ EqModule::high, 1, 1.5 });
                for (const auto& cs : cases)
                {
                    const auto bk = EqModule::kindOf (cs.band, cs.type, (float) cs.q);
                    const double qEff = EqModule::effectiveQ (bk, (float) gain, 0.0f);
                    const auto c = EqModule::designCoefs (ctx, cs.band, cs.type, (float) fc, (float) gain, (float) cs.q, 0.0f);
                    double worst = 0.0;
                    auto probe = [&] (double f)
                    {
                        if (f < 20.0 || f > fmax) return;
                        worst = std::max (worst, std::abs (20.0 * std::log10 (eqd::svfMagnitude (c, f, fs)) - analogDb (bk.kind, gain, qEff, fc, f)));
                    };
                    for (int i = 0; i < 300; ++i) probe (20.0 * std::pow (fmax / 20.0, i / 299.0));
                    const double rr = std::min (8.0, std::sqrt (1.0 + 0.25 / (qEff * qEff)) + 0.5 / qEff);
                    for (int i = -60; i <= 60; ++i) probe (fc * std::pow (rr, 2.0 * i / 60.0));
                    if (bk.kind != eqd::Kind::Bell) shelves.note (worst, fc, gain, qEff);
                    else (fc <= 0.4 * fs ? bellsInner : bellsOuter).note (worst, fc, gain, qEff);
                }
            }
        }
        const auto desc = [] (const Worst& w)
        {
            return juce::String (w.e, 3) + " dB (fc " + juce::String (w.fc, 0) + ", " + juce::String (w.gain, 0) + " dB, Q " + juce::String (w.q, 2) + ", n=" + juce::String (w.count) + ")";
        };
        const auto tag = juce::String (fs / 1000.0, 1) + " kHz";
        tool::check (shelves.e <= 0.5, "shelves within 0.5 dB of the analog prototype to 0.9 Nyquist, " + tag, desc (shelves));
        tool::check (bellsInner.e <= 0.5, "bells (fc <= 0.4 fs) within 0.5 dB, " + tag, desc (bellsInner));
        tool::check (bellsOuter.e <= 1.0, "bells (fc > 0.4 fs, outside the 0.5 dB rule) within 1 dB, " + tag, desc (bellsOuter));
    }
}

// The new design must not be worse than the bilinear one anywhere it was already good, and much
// better near Nyquist (cramping).
NL_TEST (eq_designBeatsBilinear)
{
    const double fs = 48000.0;
    eqd::Context ctx;
    ctx.prepare (fs);
    double worstOld = 0.0, worstNew = 0.0;
    for (double fc : { 8000.0, 12000.0, 16000.0 })
        for (double gain : { -12.0, 12.0 })
        {
            const auto cOld = EqModule::bandCoefs (EqModule::mid, 2, fs, (float) fc, (float) gain, 1.0f);
            const auto cNew = EqModule::designCoefs (ctx, EqModule::mid, 2, (float) fc, (float) gain, 1.0f, 0.0f);
            for (int i = 0; i < 200; ++i)
            {
                const double f = 20.0 * std::pow (0.9 * 24000.0 / 20.0, i / 199.0), ref = analogDb (eqd::Kind::Bell, gain, 1.0, fc, f);
                worstOld = std::max (worstOld, std::abs (20.0 * std::log10 (eqd::svfMagnitude (cOld, f, fs)) - ref));
                worstNew = std::max (worstNew, std::abs (20.0 * std::log10 (eqd::svfMagnitude (cNew, f, fs)) - ref));
            }
        }
    tool::check (worstNew < 0.5 && worstOld > 3.0, "new design removes the cramping of the bilinear design (bells at 8-16 kHz)",
                 "old " + juce::String (worstOld, 2) + " dB, new " + juce::String (worstNew, 3) + " dB");
}

// A band at 0 dB is exactly transparent, and designs stay finite and bounded for any input.
NL_TEST (eq_designRobust)
{
    juce::Random rng (11);
    bool finite = true, bounded = true;
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        eqd::Context ctx;
        ctx.prepare (fs);
        for (int i = 0; i < 4000; ++i)
        {
            const int band = rng.nextInt (4), type = rng.nextInt (3);
            const float fc = 5.0f + 25000.0f * rng.nextFloat() * rng.nextFloat(), gain = rng.nextFloat() * 80.0f - 40.0f, q = 0.01f + 60.0f * rng.nextFloat() * rng.nextFloat();
            const auto c = EqModule::designCoefs (ctx, band, type, fc, gain, q, rng.nextFloat());
            const float v[] { c.g, c.k, c.a1, c.a2, c.a3, c.m0, c.m1, c.m2 };
            for (float x : v) finite &= std::isfinite (x);
            bounded &= c.g > 0.0f && c.k > 0.0f && std::abs (c.m0) < 1.0e4f && std::abs (c.m1) < 1.0e5f && std::abs (c.m2) < 1.0e4f;
        }
    }
    tool::check (finite && bounded, "design returns finite, stable coefficients for extreme frequency, gain, Q and sample rate");

    const auto c = EqModule::designCoefs ([] { static eqd::Context x; x.prepare (48000.0); return x; }(), EqModule::mid, 2, 1000.0f, 0.0f, 2.0f, 1.0f);
    tool::check (c.m0 == 1.0f && c.m1 == 0.0f && c.m2 == 0.0f, "0 dB design is the identity mix (m0 = 1, m1 = m2 = 0)");
}

//==============================================================================
// 2. eq_char: Clean is constant Q, Proportional narrows with |gain| (law documented in EqModule.h).
NL_TEST (eq_characterCleanVersusProportional)
{
    const double fs = 96000.0;
    // Q from the half-gain (in dB) points of a bell: Q = 1 / (W+ - W-), W = f / fc.
    auto measuredQ = [&] (const EqParams& p, double fc, double gain)
    {
        auto find = [&] (double lo, double hi, bool rising)
        {
            for (int it = 0; it < 60; ++it)
            {
                const double mid = std::sqrt (lo * hi), v = EqModule::magnitudeDb (p, fs, mid, 0);
                const bool above = (v - 0.5 * gain) * (gain > 0 ? 1.0 : -1.0) > 0.0;
                if (above == rising) hi = mid; else lo = mid;
            }
            return std::sqrt (lo * hi);
        };
        const double fLo = find (fc / 20.0, fc, true), fHi = find (fc, fc * 20.0, false);
        return fc / (fHi - fLo);
    };

    bool cleanConst = true, propLaw = true;
    juce::String cleanDetail, propDetail;
    for (double gain : { 3.0, 6.0, 12.0, 18.0, -12.0 })
    {
        auto p = defaultEq();
        p.band[EqModule::mid] = { true, 2, 0, 1000.0f, (float) gain, 2.0f };
        p.character = 0;
        const double qc = measuredQ (p, 1000.0, gain);
        cleanConst &= std::abs (qc - 2.0) < 0.06;
        cleanDetail << juce::String (qc, 3) << " ";
        p.character = 1;
        const double qp = measuredQ (p, 1000.0, gain), expected = 2.0 * (1.0 + std::abs (gain) / 12.0);
        propLaw &= std::abs (qp / expected - 1.0) < 0.03;
        propDetail << juce::String (qp, 2) << "/" << juce::String (expected, 2) << " ";
    }
    tool::check (cleanConst, "Clean: constant Q (2.0) at +3, +6, +12, +18, -12 dB", cleanDetail);
    tool::check (propLaw, "Proportional: Q = Q0 * (1 + |gain|/12) measured/expected", propDetail);

    // Switching the character is smoothed, not stepped.
    EqModule m;
    m.prepare (48000.0);
    auto p = defaultEq();
    p.band[EqModule::mid] = { true, 2, 0, 1000.0f, 12.0f, 3.0f };
    m.setParams (p);
    auto l = sineVec (48000.0, 1300.0, 0.25f, 48000), r = l;
    std::vector<float> a (l.begin(), l.begin() + 24000), b (r.begin(), r.begin() + 24000), a2 (l.begin() + 24000, l.end()), b2 (r.begin() + 24000, r.end());
    runEq (m, a, b, 256);
    p.character = 1;
    m.setParams (p);
    runEq (m, a2, b2, 256);
    std::vector<float> all = a;
    all.insert (all.end(), a2.begin(), a2.end());
    const double step = maxStep (all, 24000, 48000), ref = 0.25 * 2.0 * kPiD * 1300.0 / 48000.0 * 4.0;
    tool::check (step < 2.0 * ref, "switching Clean -> Proportional glides without a step", "max |dy| " + juce::String (step, 4) + ", bound " + juce::String (2.0 * ref, 4));
}

//==============================================================================
// 7. Display: magnitudeDb equals the measured response (D13), per channel, at 44.1 and 96 kHz.
NL_TEST (eq_displayMatchesMeasuredResponse)
{
    for (double fs : { 44100.0, 96000.0 })
    {
        for (int variant = 0; variant < 2; ++variant)
        {
            auto p = defaultEq();
            p.band[EqModule::low] = { true, 1, variant == 0 ? 0 : 1, 120.0f, 6.0f, 1.0f };
            p.band[EqModule::mid] = { true, 2, 0, 700.0f, -5.0f, 1.3f };
            p.band[EqModule::tone] = { true, 1, 0, 3300.0f, 8.0f, 1.0f };
            p.band[EqModule::high] = { true, 0, variant == 0 ? 0 : 2, 9000.0f, -4.0f, 0.7f };
            p.character = variant;
            double worst = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (double f : { 60.0, 150.0, 400.0, 1000.0, 2500.0, 5000.0, 9000.0, 14000.0 })
                    worst = std::max (worst, std::abs (EqModule::magnitudeDb (p, fs, f, ch) - measuredGainDb (fs, p, f, ch)));
            tool::check (worst <= 0.1, "display curve equals measured sine response, " + juce::String (fs / 1000.0, 1) + " kHz, "
                                           + (variant == 0 ? "Clean, both channels" : "Proportional, Mid/Side targets"),
                         "max difference " + juce::String (worst, 4) + " dB");
        }
    }

    // Targets: a band aimed at Side leaves the Mid curve flat.
    auto p = defaultEq();
    p.band[EqModule::tone] = { true, 2, 2, 2000.0f, 9.0f, 1.0f };
    tool::check (std::abs (EqModule::magnitudeDb (p, 48000.0, 2000.0, 0)) < 1.0e-6 && std::abs (EqModule::magnitudeDb (p, 48000.0, 2000.0, 1) - 9.0) < 0.05,
                 "display honours band targets (Side only: Mid flat, Side +9 dB)");
}

//==============================================================================
// 3. Dynamic bands.
NL_TEST (eq_dynamicBandStaticCurve)
{
    const double fs = 48000.0;
    for (float range : { -12.0f, 9.0f })
    {
        auto p = defaultEq();
        const float staticGain = range < 0 ? 3.0f : -2.0f, thresh = -30.0f;
        p.band[EqModule::mid] = { true, 2, 0, 1000.0f, staticGain, 1.0f, true, false, thresh, range, 0 };
        double worst = 0.0;
        juce::String detail;
        for (float levelDb = -60.0f; levelDb <= 0.0f; levelDb += 6.0f)
        {
            EqModule m;
            m.prepare (fs);
            m.setParams (p);
            const int n = (int) fs * 2;
            const float amp = juce::Decibels::decibelsToGain (levelDb);
            auto l = sineVec (fs, 1000.0, amp, n), r = l;
            runEq (m, l, r, 128);
            const double measured = 20.0 * std::log10 (rmsOf (l, n * 3 / 4, n) / (amp / std::sqrt (2.0)));
            const float bandLevel = levelDb - 3.0103f; // mean-square level of the sine, in dB
            const double expected = staticGain + eqd::dynGainDb (bandLevel, thresh, range);
            worst = std::max (worst, std::abs (measured - expected));
            if (levelDb == -30.0f || levelDb == -18.0f || levelDb == 0.0f) detail << juce::String (levelDb, 0) << " dBFS: " << juce::String (measured, 2) << "/" << juce::String (expected, 2) << "  ";
        }
        tool::check (worst < 0.25, juce::String (range < 0 ? "dynamic cut" : "dynamic boost") + " follows the static curve (range "
                                       + juce::String (range, 0) + " dB reached 12 dB over threshold), measured/expected",
                     "worst " + juce::String (worst, 3) + " dB; " + detail);
    }

    // Soft knee and the 12 dB rule of the law itself.
    tool::check (eqd::dynFraction (-4.0f) == 0.0f && std::abs (eqd::dynFraction (12.0f) - 1.0f) < 1.0e-6f && std::abs (eqd::dynFraction (20.0f) - 1.0f) < 1.0e-6f
                     && std::abs (eqd::dynFraction (6.0f) - 0.5f) < 1.0e-6f,
                 "gain law: zero below the knee, half at +6 dB, full range at +12 dB over threshold");
}

NL_TEST (eq_dynamicBandNullsAtRest)
{
    const double fs = 48000.0;
    auto stat = defaultEq(), dynRest = defaultEq(), dynZero = defaultEq(), flat = defaultEq();
    stat.band[EqModule::mid] = { true, 2, 0, 800.0f, 5.0f, 1.2f };
    stat.band[EqModule::high] = { true, 0, 0, 7000.0f, -3.0f, 0.7f };
    dynRest = dynZero = stat;
    // Signal stays below the threshold.
    dynRest.band[EqModule::mid].dyn = true;  dynRest.band[EqModule::mid].dynThresh = 0.0f;  dynRest.band[EqModule::mid].dynRange = -12.0f;
    dynRest.band[EqModule::high].dyn = true; dynRest.band[EqModule::high].dynThresh = 0.0f; dynRest.band[EqModule::high].dynRange = 12.0f;
    // Range 0: signal is loud, but nothing may change.
    dynZero.band[EqModule::mid].dyn = true;  dynZero.band[EqModule::mid].dynThresh = -60.0f; dynZero.band[EqModule::mid].dynRange = 0.0f;
    dynZero.band[EqModule::high].dyn = true; dynZero.band[EqModule::high].dynThresh = -60.0f; dynZero.band[EqModule::high].dynRange = 0.0f;
    // Static gain 0 and dynamics at rest: the band is a pure pass-through.
    flat.band[EqModule::tone] = { true, 2, 0, 2000.0f, 0.0f, 1.0f, true, false, 0.0f, -12.0f, 1 };

    auto run = [&] (const EqParams& p, float level)
    {
        EqModule m;
        m.prepare (fs);
        m.setParams (p);
        auto l = noiseVec (48000, level, 5), r = noiseVec (48000, level, 6);
        runEq (m, l, r, 64);
        return std::make_pair (l, r);
    };
    auto diffDb = [] (const std::pair<std::vector<float>, std::vector<float>>& a, const std::pair<std::vector<float>, std::vector<float>>& b)
    {
        float m = 0.0f;
        for (size_t i = 0; i < a.first.size(); ++i) m = std::max ({ m, std::abs (a.first[i] - b.first[i]), std::abs (a.second[i] - b.second[i]) });
        return juce::Decibels::gainToDecibels (m, -300.0f);
    };
    const float quiet = juce::Decibels::decibelsToGain (-30.0f), loud = juce::Decibels::decibelsToGain (-6.0f);
    const auto ref = run (stat, quiet), refLoud = run (stat, loud);
    tool::check (diffDb (ref, run (dynRest, quiet)) <= -120.0f, "dynamic band below threshold nulls against the static band (<= -120 dB)", juce::String (diffDb (ref, run (dynRest, quiet)), 1) + " dB");
    tool::check (diffDb (refLoud, run (dynZero, loud)) <= -120.0f, "dynamic band with range 0 nulls against the static band (<= -120 dB)", juce::String (diffDb (refLoud, run (dynZero, loud)), 1) + " dB");
    const auto in = std::make_pair (noiseVec (48000, loud, 5), noiseVec (48000, loud, 6));
    tool::check (diffDb (in, run (flat, loud)) <= -120.0f || run (flat, loud).first == in.first, "dynamic band at 0 dB static gain and at rest is a pass-through");
}

NL_TEST (eq_dynamicBandTiming)
{
    const double fs = 48000.0;
    // 1 kHz burst 20 dB over the threshold, range -12 dB: time for the dynamic gain to cover 63.2 % of the
    // range after the burst starts (attack) and to give back 63.2 % after it ends (release).
    double attackMs[3], releaseMs[3];
    for (int speed = 0; speed < 3; ++speed)
    {
        EqModule m;
        m.prepare (fs);
        auto p = defaultEq();
        p.band[EqModule::mid] = { true, 2, 0, 1000.0f, 0.0f, 1.0f, true, false, -40.0f, -12.0f, speed };
        m.setParams (p);
        const int total = (int) (fs * 3.0), burstStart = (int) (fs * 0.2), burstEnd = (int) (fs * 1.6);
        auto l = sineVec (fs, 1000.0, juce::Decibels::decibelsToGain (-20.0f), total), r = l;
        for (int i = 0; i < burstStart; ++i) l[(size_t) i] = r[(size_t) i] = 0.0f;
        for (int i = burstEnd; i < total; ++i) l[(size_t) i] = r[(size_t) i] = 0.0f;
        double tA = -1.0, tR = -1.0;
        for (int pos = 0; pos < total; pos += 32)
        {
            float* io[2] { l.data() + pos, r.data() + pos };
            m.process (io, 2, 32);
            const float dyn = m.getDynGainDb (EqModule::mid);
            if (tA < 0.0 && pos >= burstStart && dyn <= -12.0f * 0.632f) tA = (pos + 32 - burstStart) * 1000.0 / fs;
            if (tR < 0.0 && pos >= burstEnd && dyn >= -12.0f * 0.368f) tR = (pos + 32 - burstEnd) * 1000.0 / fs;
        }
        attackMs[speed] = tA;
        releaseMs[speed] = tR;
    }
    const float wantA[3] { 2.0f, 8.0f, 30.0f }, wantR[3] { 50.0f, 150.0f, 500.0f };
    bool ok = true;
    juce::String detail;
    for (int s = 0; s < 3; ++s)
    {
        // Attack includes the detector's own integration (about 2 ms at 1 kHz); release is the gain release.
        ok &= attackMs[s] > 0.0 && attackMs[s] >= 0.5 * wantA[s] && attackMs[s] <= wantA[s] + 6.0;
        ok &= releaseMs[s] > 0.0 && std::abs (releaseMs[s] / wantR[s] - 1.0) < 0.2;
        detail << (s == 0 ? "Fast " : s == 1 ? "Medium " : "Slow ") << "attack " << juce::String (attackMs[s], 1) << " ms, release " << juce::String (releaseMs[s], 0) << " ms; ";
    }
    tool::check (ok, "attack / release (Fast 2/50, Medium 8/150, Slow 30/500 ms) as documented", detail);
}

NL_TEST (eq_dynamicBandExternalKey)
{
    const double fs = 48000.0;
    auto p = defaultEq();
    p.band[EqModule::tone] = { true, 2, 0, 3000.0f, 0.0f, 1.0f, true, true, -40.0f, -12.0f, 0 };
    auto run = [&] (bool ext, float inLevelDb, float keyLevelDb)
    {
        auto q = p;
        q.band[EqModule::tone].dynExt = ext;
        EqModule m;
        m.prepare (fs);
        m.setParams (q);
        const int n = 24000;
        auto l = sineVec (fs, 3000.0, juce::Decibels::decibelsToGain (inLevelDb), n), r = l;
        auto kl = sineVec (fs, 3000.0, juce::Decibels::decibelsToGain (keyLevelDb), n), kr = kl;
        runEq (m, l, r, 64, {}, &kl, &kr);
        return m.getDynGainDb (EqModule::tone);
    };
    const float loudKey = run (true, -60.0f, -6.0f), quietKey = run (true, -6.0f, -70.0f), noExt = run (false, -6.0f, -70.0f);
    tool::check (loudKey < -11.0f && std::abs (quietKey) < 0.01f && noExt < -11.0f,
                 "external key drives the dynamic band (loud key, quiet input -> full cut; quiet key, loud input -> none; internal -> follows input)",
                 juce::String (loudKey, 2) + " / " + juce::String (quietKey, 2) + " / " + juce::String (noExt, 2) + " dB");
}

//==============================================================================
// 4. Band solo: auditions one band's region, click-free in and out.
NL_TEST (eq_bandSolo)
{
    const double fs = 48000.0;
    auto p = defaultEq();
    p.band[EqModule::low] = { true, 0, 0, 150.0f, 6.0f, 0.7f };
    p.band[EqModule::mid] = { true, 2, 0, 1000.0f, 8.0f, 2.0f };
    p.band[EqModule::high] = { true, 0, 0, 8000.0f, -6.0f, 0.7f };

    auto soloGain = [&] (int band, double f)
    {
        auto q = p;
        q.solo = band;
        return measuredGainDb (fs, q, f);
    };
    // The band-pass around the mid band passes its centre and removes far-away content.
    const double midCentre = soloGain (2, 1000.0), midLow = soloGain (2, 100.0), midHigh = soloGain (2, 10000.0);
    tool::check (std::abs (midCentre) < 0.3 && midLow < -20.0 && midHigh < -20.0, "solo Mid: band-pass around the band (centre ~0 dB, far content below -20 dB)",
                 juce::String (midCentre, 2) + " / " + juce::String (midLow, 1) + " / " + juce::String (midHigh, 1) + " dB");
    // Shelves audition their shelf region: low band = below the corner, high band = above it.
    const double lowBelow = soloGain (1, 40.0), lowAbove = soloGain (1, 4000.0), highAbove = soloGain (4, 16000.0), highBelow = soloGain (4, 500.0);
    tool::check (lowBelow > -1.0 && lowAbove < -20.0 && highAbove > -1.0 && highBelow < -20.0, "solo Low / High: the shelf region (low-pass / high-pass at the corner)",
                 juce::String (lowBelow, 1) + " / " + juce::String (lowAbove, 1) + " / " + juce::String (highAbove, 1) + " / " + juce::String (highBelow, 1) + " dB");

    // Transitions: Off -> Mid -> Off with a 1 kHz and a 200 Hz sine; no click, and the EQ output returns exactly.
    for (double f : { 200.0, 1000.0 })
    {
        EqModule m, ref;
        m.prepare (fs);
        ref.prepare (fs);
        m.setParams (p);
        ref.setParams (p);
        const int n = (int) (fs * 1.5), tOn = (int) (fs * 0.4), tOff = (int) (fs * 0.9);
        auto l = sineVec (fs, f, 0.2f, n), r = l, lr = l, rr = l;
        auto q = p;
        q.solo = 2;
        runEq (m, l, r, 64, { { tOn, q }, { tOff, p } });
        runEq (ref, lr, rr, 64);
        const double bound = 2.0 * 0.2 * 2.0 * kPiD * f / fs * 4.0;
        const double stepOn = maxStep (l, tOn - 10, tOn + 1500), stepOff = maxStep (l, tOff - 10, tOff + 1500);
        float tail = 0.0f;
        for (int i = tOff + (int) (0.1 * fs); i < n; ++i) tail = std::max (tail, std::abs (l[(size_t) i] - lr[(size_t) i]));
        const double sOn = splatterDb (l, tOn - 480, fs, f), sOff = splatterDb (l, tOff - 480, fs, f);
        tool::check (stepOn < bound && stepOff < bound && sOn < -60.0 && sOff < -60.0 && tail < 1.0e-5f,
                     "solo on and off are click-free at " + juce::String (f, 0) + " Hz (splatter <= -60 dB, EQ output restored)",
                     "splatter " + juce::String (sOn, 1) + " / " + juce::String (sOff, 1) + " dB, restored within " + juce::String (tail, 8));
    }

    // Moving the solo from one band to another cross-fades between the regions.
    {
        EqModule m;
        m.prepare (fs);
        auto q = p;
        q.solo = 1;
        m.setParams (q);
        const int n = (int) (fs * 1.0), tSw = n / 2;
        auto l = sineVec (fs, 300.0, 0.2f, n), r = l;
        q.solo = 3;
        runEq (m, l, r, 128, { { tSw, q } });
        const double sp = splatterDb (l, tSw - 480, fs, 300.0);
        tool::check (sp < -60.0, "solo Low -> Mid cross-fades without splatter", "splatter " + juce::String (sp, 1) + " dB");
    }
}

//==============================================================================
// 5. D10: band type and target changes cross-fade (shadow state), they do not click.
NL_TEST (eq_discreteSwitchesAreClickFree)
{
    const double fs = 48000.0;

    // The metric must catch a hard switch: splice two sine amplitudes at a peak.
    {
        auto x = sineVec (fs, 1000.0, 0.1f, 48000);
        for (int i = 24000; i < 48000; ++i) x[(size_t) i] *= 4.0f;
        const double hard = splatterDb (x, 24000 - 480, fs, 1000.0);
        tool::check (hard > -60.0, "sanity: the splatter metric flags a hard 12 dB step", juce::String (hard, 1) + " dB");
    }

    struct Case
    {
        const char* name;
        int band;
        EqBandParams before, after;
        bool lrToMs; // use channel 1 (Side) for the measurement
    };
    const Case cases[] {
        { "Mid: Wide -> Narrow bell (+12 dB)", EqModule::mid, { true, 0, 0, 1000.0f, 12.0f, 1.0f }, { true, 1, 0, 1000.0f, 12.0f, 1.0f }, false },
        { "Mid: Narrow -> Parametric Q 0.4", EqModule::mid, { true, 1, 0, 1000.0f, -9.0f, 0.4f }, { true, 2, 0, 1000.0f, -9.0f, 0.4f }, false },
        { "Low: shelf -> resonant shelf (+9 dB)", EqModule::low, { true, 0, 0, 300.0f, 9.0f, 1.0f }, { true, 1, 0, 300.0f, 9.0f, 1.0f }, false },
        { "High: shelf -> bell (+10 dB)", EqModule::high, { true, 0, 0, 2000.0f, 10.0f, 1.5f }, { true, 2, 0, 2000.0f, 10.0f, 1.5f }, false },
        { "Tone: target Both -> Mid only (Mid channel)", EqModule::tone, { true, 2, 0, 1000.0f, 12.0f, 1.0f }, { true, 2, 1, 1000.0f, 12.0f, 1.0f }, false },
        { "Tone: target Both -> Mid only (Side channel)", EqModule::tone, { true, 2, 0, 1000.0f, 12.0f, 1.0f }, { true, 2, 1, 1000.0f, 12.0f, 1.0f }, true },
        { "Tone: target Mid -> Side (Mid channel)", EqModule::tone, { true, 2, 1, 1000.0f, -12.0f, 1.0f }, { true, 2, 2, 1000.0f, -12.0f, 1.0f }, false },
    };
    for (const auto& cs : cases)
        for (double f : { 100.0, 1000.0 })
        {
            auto p = defaultEq();
            p.band[cs.band] = cs.before;
            EqModule m;
            m.prepare (fs);
            m.setParams (p);
            auto q = p;
            q.band[cs.band] = cs.after;
            const int n = (int) (fs * 1.2), tSw = (int) (fs * 0.6);
            auto l = sineVec (fs, f, 0.1f, n), r = l;
            runEq (m, l, r, 100, { { tSw, q } });
            const auto& y = cs.lrToMs ? r : l;
            // Steady amplitudes before and after define the derivative bound.
            const double a0 = rmsOf (y, tSw - 4800, tSw) * std::sqrt (2.0), a1 = rmsOf (y, n - 4800, n) * std::sqrt (2.0);
            const double bound = 2.0 * std::max (a0, a1) * 2.0 * kPiD * f / fs;
            const double step = maxStep (y, tSw - 10, tSw + 2400), sp = splatterDb (y, tSw - 480, fs, f);
            tool::check (sp < -60.0 && step < bound, juce::String (cs.name) + " at " + juce::String (f, 0) + " Hz is click-free",
                         "splatter " + juce::String (sp, 1) + " dB, max step " + juce::String (step, 5) + " (bound " + juce::String (bound, 5) + ")");
        }

    // A big gain jump is smoothed too (continuous parameters).
    {
        auto p = defaultEq();
        EqModule m;
        m.prepare (fs);
        m.setParams (p);
        auto q = p;
        q.band[EqModule::mid] = { true, 2, 0, 1000.0f, 18.0f, 1.0f };
        const int n = (int) (fs * 1.2), tSw = (int) (fs * 0.6);
        auto l = sineVec (fs, 1000.0, 0.1f, n), r = l;
        runEq (m, l, r, 100, { { tSw, q } });
        const double sp = splatterDb (l, tSw - 480, fs, 1000.0);
        // Continuous parameters glide with control-rate coefficient steps (every 32 samples), which is audible
        // only as faint sidebands; the -60 dB bar is for discrete switches.
        tool::check (sp < -45.0, "a 0 -> +18 dB gain jump on a 1 kHz sine glides (splatter <= -45 dB)", juce::String (sp, 1) + " dB");    }
}

//==============================================================================
// 6. D3 / D8: identical output for any block size, with automation of everything and dynamic bands.
NL_TEST (eq_blockSizeIndependentWithAutomation)
{
    const double fs = 48000.0;
    const int n = 2048 * 14;
    const char* levelName[] { "continuous parameters", "+ band types, targets, character", "+ dynamic bands", "+ external key, band solo" };
    for (int level = 0; level < 4; ++level)
    {
        juce::Random rng (21 + level);
        std::vector<Event> events;
        for (int s = 0; s < n; s += 2048) events.push_back ({ s, randomEq (rng, level) });

        auto inL = noiseVec (n, 0.3f, 1), inR = noiseVec (n, 0.3f, 2), keyL = noiseVec (n, 0.5f, 3), keyR = noiseVec (n, 0.5f, 4);
        std::vector<std::vector<float>> outs;
        const int blocks[] { 2048, 1, 7, 32, 37, 64, 511, 512 };
        for (int block : blocks)
        {
            EqModule m;
            m.prepare (fs, 4096);
            auto l = inL, r = inR;
            runEq (m, l, r, block, events, &keyL, &keyR);
            l.insert (l.end(), r.begin(), r.end());
            outs.push_back (std::move (l));
        }
        double worst = 0.0;
        int worstBlock = 0, worstIndex = 0;
        for (size_t k = 1; k < outs.size(); ++k)
            for (size_t i = 0; i < outs[0].size(); ++i)
            {
                const double d = std::abs ((double) outs[k][i] - outs[0][i]);
                if (d > worst) { worst = d; worstBlock = blocks[k]; worstIndex = (int) i; }
            }
        bool finite = true;
        double peak = 0.0;
        for (float v : outs[0]) { finite &= std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
        tool::check (finite && worst <= 1.0e-6, "module output identical for block sizes 1, 7, 32, 37, 64, 511, 512, 2048 with automation of " + juce::String (levelName[level]),
                     "max difference " + juce::String (worst, 9) + (worst > 0 ? " at block " + juce::String (worstBlock) + ", sample " + juce::String (worstIndex % n) : juce::String()) + ", peak " + juce::String (peak, 2));
    }
}
NL_TEST (eq_stripBlockSizeIndependentWithAutomation)
{
    const int n = 2048 * 12;
    juce::Random rng (33);
    std::vector<tool::AutomationEvent> events;
    for (int s = 0; s < n; s += 2048)
        for (int k = 0; k < 14; ++k)
        {
            const int idx = P::eqChar + rng.nextInt (2 + 4 * P::kEqBandStride);
            if (idx == P::eqSolo && rng.nextFloat() < 0.7f) continue;
            const auto& d = paramDef (idx);
            float v;
            if (d.kind == ParamDef::Float) v = d.minV + (d.maxV - d.minV) * rng.nextFloat();
            else v = (float) rng.nextInt ((int) d.maxV + 1);
            events.push_back ({ s, idx, v });
        }
    juce::AudioBuffer<float> in (2, n), key (2, n);
    juce::Random nr (7);
    tool::fillNoise (in, nr, 0.3f);
    tool::fillNoise (key, nr, 0.5f);

    auto renderAt = [&] (int block)
    {
        auto p = tool::makeProc (48000.0, 2048, true);
        tool::allModulesOff (*p);
        tool::setParam (*p, P::eqOn, 1);
        tool::setParam (*p, P::eqMode, 1);
        for (int b = 0; b < 4; ++b)
        {
            tool::setParam (*p, P::eqLowDyn + b * P::kEqBandStride, 1);
            tool::setParam (*p, P::eqLowDynExt + b * P::kEqBandStride, (float) (b & 1));
        }
        p->getStrip().reset();
        return renderSampleAccurate (*p, in, block, events, key);
    };
    const auto ref = renderAt (2048);
    double worst = 0.0;
    for (int block : { 1, 7, 32, 37, 64, 511, 512 })
        worst = std::max (worst, (double) juce::Decibels::decibelsToGain (tool::maxDiffDb (ref, renderAt (block))));
    tool::check (worst <= 1.0e-6, "EQ inside the strip (M/S, dynamic bands, key, solo, automation): identical for block sizes 1 .. 2048", "max difference " + juce::String (worst, 9));
}

//==============================================================================
// Real-time safety with everything in the EQ active.
NL_TEST (eq_noAllocationOnTheAudioThread)
{
    auto p = tool::makeProc (48000.0, 256, true);
    tool::allModulesOff (*p);
    tool::setParam (*p, P::eqOn, 1);
    tool::setParam (*p, P::eqMode, 1);
    tool::setParam (*p, P::eqChar, 1);
    for (int b = 0; b < 4; ++b)
    {
        tool::setParam (*p, P::eqLowDyn + b * P::kEqBandStride, 1);
        tool::setParam (*p, P::eqLowDynExt + b * P::kEqBandStride, (float) (b & 1));
        tool::setParam (*p, P::eqLowGain + b * P::kEqBandStride, 6.0f);
        tool::setParam (*p, P::eqLowDynThresh + b * P::kEqBandStride, -50.0f);
    }
    juce::Random rng (9);
    juce::AudioBuffer<float> work (4, 256);
    juce::MidiBuffer midi;
    tool::fillNoise (work, rng, 0.5f);
    p->processBlock (work, midi);
    tool::allocationCount = 0;
    tool::countAllocations = true;
    for (int i = 0; i < 400; ++i)
    {
        tool::fillNoise (work, rng, 0.5f);
        if (i % 7 == 0) tool::setParam (*p, P::eqMidType, (float) ((i / 7) % 3));
        if (i % 9 == 0) tool::setParam (*p, P::eqToneTarget, (float) ((i / 9) % 3));
        if (i % 11 == 0) tool::setParam (*p, P::eqSolo, (float) ((i / 11) % 5));
        if (i % 5 == 0) tool::setParam (*p, P::eqHighFreq, 2000.0f + 700.0f * (float) (i % 17));
        if (i % 13 == 0) tool::setParam (*p, P::eqLowDynRange, (float) (-18 + (i % 37)));
        p->processBlock (work, midi);
    }
    tool::countAllocations = false;
    tool::check (tool::allocationCount.load() == 0, "no heap allocation with dynamic bands, key, solo, type / target / range automation", juce::String (tool::allocationCount.load()) + " allocations");
}







//==============================================================================
// Proportional (console) Q: the same accuracy as Clean, at the effective Q of each gain.
NL_TEST (eq_designProportionalMatchesAnalog)
{
    for (double fs : { 44100.0, 48000.0 })
    {
        eqd::Context ctx;
        ctx.prepare (fs);
        double worstInner = 0.0, worstOuter = 0.0;
        const double fmax = 0.9 * 0.5 * fs;
        for (int fi = 0; fi < 25; ++fi)
        {
            const double fc = 20.0 * std::pow (1000.0, fi / 24.0);
            for (double gain : { -18.0, -12.0, -6.0, -3.0, -1.0, 1.0, 3.0, 6.0, 12.0, 18.0 })
                for (double q : { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0 })
                {
                    const auto bk = EqModule::kindOf (EqModule::mid, 2, (float) q);
                    const double qEff = EqModule::effectiveQ (bk, (float) gain, 1.0f);
                    const auto c = EqModule::designCoefs (ctx, EqModule::mid, 2, (float) fc, (float) gain, (float) q, 1.0f);
                    const double rr = std::min (8.0, std::sqrt (1.0 + 0.25 / (qEff * qEff)) + 0.5 / qEff);
                    double& worst = fc <= 0.4 * fs ? worstInner : worstOuter;
                    auto probe = [&] (double f)
                    {
                        if (f < 20.0 || f > fmax) return;
                        worst = std::max (worst, std::abs (20.0 * std::log10 (eqd::svfMagnitude (c, f, fs)) - analogDb (bk.kind, gain, qEff, fc, f)));
                    };
                    for (int i = 0; i < 300; ++i) probe (20.0 * std::pow (fmax / 20.0, i / 299.0));
                    for (int i = -60; i <= 60; ++i) probe (fc * std::pow (rr, 2.0 * i / 60.0));
                }
        }
        tool::check (worstInner <= 0.5 && worstOuter <= 1.0,
                     "Proportional bells within 0.5 dB (fc <= 0.4 fs) and 1 dB (above) of the analog prototype, " + juce::String (fs / 1000.0, 1) + " kHz",
                     "worst " + juce::String (worstInner, 3) + " dB / " + juce::String (worstOuter, 3) + " dB");
    }
}

// The display with the current dynamic gain (magnitudeDb with the dynamic gains) equals the DSP.
NL_TEST (eq_displayIncludesDynamicGain)
{
    EqParams p;
    for (auto& b : p.band) b.on = false;
    auto& band = p.band[1];
    band.on = true;
    band.type = 2;
    band.freq = 1000.0f;
    band.gain = 0.0f;
    band.q = 1.0f;
    band.dyn = true;
    band.dynThresh = -30.0f;
    band.dynRange = -12.0f;
    band.dynSpeed = 1;
    band.dynExt = true;

    double worst = 0.0, engaged = 1.0e9;
    for (double fs : { 48000.0, 96000.0 })
        for (double keyDb : { -27.0, -21.0, -15.0 })
            for (double f : { 50.0, 200.0, 800.0, 1000.0, 1200.0, 2000.0, 5000.0, 10000.0, 16000.0 })
            {
                const int n = (int) (fs * 1.6);
                const float amp = juce::Decibels::decibelsToGain (-20.0f);
                auto l = sineVec (fs, f, amp, n), r = l;
                auto kl = sineVec (fs, 1000.0, juce::Decibels::decibelsToGain ((float) keyDb), n), kr = kl;
                EqModule m;
                m.prepare (fs);
                m.setParams (p);
                runEq (m, l, r, 256, {}, &kl, &kr);
                const double dyn = m.getDynGainDb (1);
                const double dsp = 20.0 * std::log10 (rmsOf (l, n / 2, n) / (amp / std::sqrt (2.0)));
                const float node[4] { 0.0f, (float) dyn, 0.0f, 0.0f };
                worst = std::max (worst, std::abs (dsp - (double) EqModule::magnitudeDb (p, fs, f, 0, node)));
                engaged = std::min (engaged, std::abs (dyn));
            }
    tool::check (engaged > 0.5, "dynamic gain engaged at every key level tested (|gain| > 0.5 dB)", "smallest " + juce::String (engaged, 2) + " dB");
    tool::check (worst <= 0.1, "display with the dynamic gain equals the DSP (<= 0.1 dB, 48 and 96 kHz, key -27 to -15 dBFS)",
                 "worst " + juce::String (worst, 4) + " dB");
}

// At most one refinement design per control tick, however many bands change at once. Worst case:
// all four bands switch in the same tick to settings whose closed form misses the tolerance. The
// bands must still converge to the exact design once the parameters have settled.
NL_TEST (eq_refinementBudgetPerTick)
{
    const double fs = 48000.0;
    eqd::Context ctx;
    ctx.prepare (fs);

    struct Pick { int band = 0, type = 0; float freq = 0.0f, gain = 0.0f, q = 1.0f; };
    std::vector<Pick> picks;
    for (int band = 0; band < 4; ++band)
    {
        Pick found;
        bool ok = false;
        for (int type = 0; type < 3 && ! ok; ++type)
            for (float freq : { 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 12000.0f })
                for (float gain : { -15.0f, -9.0f, 9.0f, 15.0f })
                    for (float q : { 0.7f, 1.4f, 3.0f })
                    {
                        if (ok) break;
                        const auto bk = EqModule::kindOf (band, type, q);
                        const eqd::Shape s { bk.kind, freq, gain, EqModule::effectiveQ (bk, gain, 0.0f) };
                        if (eqd::designBand (ctx, s).stage >= 2) { found = { band, type, freq, gain, q }; ok = true; }
                    }
        if (ok) picks.push_back (found);
    }
    tool::check (picks.size() == 4, "a setting that needs the refinement exists for every band", "found " + juce::String ((int) picks.size()) + " of 4");
    if (picks.size() != 4) return;

    EqParams target;
    for (const auto& pk : picks)
    {
        auto& bp = target.band[pk.band];
        bp.type = pk.type;
        bp.freq = pk.freq;
        bp.gain = pk.gain;
        bp.q = pk.q;
    }

    EqModule m;
    m.prepare (fs);
    m.setParams (EqParams {}); // all bands at 0 dB: nothing to design yet
    const int n = (int) (fs * 2.5);
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    int maxPerTick = 0, total = 0;
    for (int i = 0; i < n; i += 32)
    {
        if (i == 4096) m.setParams (target); // all four bands change in the same control tick
        float* io[2] { l.data() + i, r.data() + i };
        m.process (io, 2, 32, nullptr);
        const int c = m.refinementsThisTick();
        maxPerTick = std::max (maxPerTick, c);
        total += c;
    }
    tool::check (maxPerTick <= 1, "at most one refinement design per control tick, all four bands changing at once",
                 "max " + juce::String (maxPerTick) + " per tick, " + juce::String (total) + " refinements in total");
    tool::check (total >= 4, "the scenario did need the refinement (at least one per band)", juce::String (total) + " refinements");

    // Settled: the DSP response of each probe equals the exact design of the target.
    double worst = 0.0;
    for (double f : { 200.0, 900.0, 2500.0, 6000.0, 12000.0 })
    {
        EqModule p = m;
        const int len = 24000;
        const float amp = 0.1f;
        auto x = sineVec (fs, f, amp, len), y = x;
        runEq (p, x, y, 256);
        const double dsp = 20.0 * std::log10 (rmsOf (x, len / 2, len) / (amp / std::sqrt (2.0)));
        worst = std::max (worst, std::abs (dsp - (double) EqModule::magnitudeDb (target, fs, f, 0)));
    }
    tool::check (worst <= 0.05, "after the parameters settle, every band equals its exact design (<= 0.05 dB)", "worst " + juce::String (worst, 4) + " dB");
}

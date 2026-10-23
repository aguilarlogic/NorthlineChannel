#include "../TestUtil.h"
#include "DynamicsTestKit.h"

// Acceptance tests for the dynamics modules (GATE, DE-ESS, COMP), roadmap D3, D4, D8, D9, D10,
// D12, D14 and sections 7.4 to 7.6. Shared helpers live in tools/tests/DynamicsTestKit.h so the
// `dynamics` measurement suite uses exactly the same measurements.

namespace tool
{
using namespace dk;

//==============================================================================
// Latency
//==============================================================================
NL_TEST (dynamics_latencyIsTheLookaheadChoice)
{
    // 0, 1, 2, 5 ms at 48 kHz, whether or not the module is enabled.
    const int expected[] { 0, 48, 96, 240 };
    for (int la = 0; la < 4; ++la)
    {
        CompModule c; GateModule g; DeEsserModule d;
        c.prepare (kFs, 512); g.prepare (kFs, 512); d.prepare (kFs, 512);
        CompParams cp; cp.lookahead = la; cp.thresh = -50; cp.type = la % 4; cp.ratio = 8;
        GateParams gp; gp.lookahead = la; gp.type = la % 3; gp.thresh = -10;
        DsParams dp; dp.lookahead = la; dp.band[0].on = (la % 2) == 0;
        c.setParams (cp); g.setParams (gp); d.setParams (dp);
        check (c.latency() == expected[la] && g.latency() == expected[la] && d.latency() == expected[la],
               "latency is the lookahead choice (" + juce::String (kLookaheadMs[la], 0) + " ms)",
               juce::String (c.latency()) + " / " + juce::String (g.latency()) + " / " + juce::String (d.latency()));
    }

    auto p = makeProc();
    allModulesOff (*p);
    setParam (*p, P::gateLookahead, 3);
    setParam (*p, P::dsLookahead, 1);
    setParam (*p, P::compLookahead, 2);
    p->getStrip().reset();
    check (p->getStrip().getLatencySamples() == 240 + 48 + 96, "strip latency sums the lookaheads with every module off",
           juce::String (p->getStrip().getLatencySamples()));
    setParam (*p, P::gateOn, 1); setParam (*p, P::dsOn, 1); setParam (*p, P::compOn, 1);
    p->getStrip().reset();
    check (p->getStrip().getLatencySamples() == 240 + 48 + 96, "and is unchanged when the modules are on");
}

NL_TEST (dynamics_audioIsDelayedByTheLookahead)
{
    // An idle module is a pure delay of exactly `latency()` samples.
    juce::AudioBuffer<float> imp (2, 2000);
    imp.clear();
    imp.setSample (0, 100, 0.5f);
    imp.setSample (1, 100, -0.25f);
    auto position = [&] (const juce::AudioBuffer<float>& o, int ch) { int a = 0; for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (ch, i)) > std::abs (o.getSample (ch, a))) a = i; return a; };

    CompParams cp; cp.lookahead = 2; cp.thresh = 0; cp.ratio = 1; cp.knee = 0;
    auto c = makeModule<CompModule> (cp);
    auto oc = runModule (*c, imp, 37);
    GateParams gp; gp.lookahead = 2; gp.thresh = -100; gp.floorDb = 0;
    auto g = makeModule<GateModule> (gp);
    auto og = runModule (*g, imp, 37);
    DsParams dp; dp.lookahead = 2; dp.band[0].thresh = 0; dp.band[1].on = false; dp.relative = false;
    auto d = makeModule<DeEsserModule> (dp);
    auto od = runModule (*d, imp, 37);
    check (position (oc, 0) == 196 && oc.getSample (0, 196) == 0.5f && oc.getSample (1, 196) == -0.25f, "COMP delays by 96 samples, bit exact", juce::String (position (oc, 0)));
    check (position (og, 0) == 196 && og.getSample (0, 196) == 0.5f, "GATE delays by 96 samples, bit exact", juce::String (position (og, 0)));
    check (position (od, 0) == 196 && od.getSample (0, 196) == 0.5f, "DE-ESS delays by 96 samples, bit exact", juce::String (position (od, 0)));
}

//==============================================================================
// COMP
//==============================================================================
NL_TEST (comp_mixZeroNullsAndDryIsNeverScaledByMakeup)   // D4
{
    for (int la : { 0, 2 })
    {
        auto p = makeProc();
        allModulesOff (*p);
        setParam (*p, P::compOn, 1);
        setParam (*p, P::compThresh, -40);
        setParam (*p, P::compRatio, 10);
        setParam (*p, P::compMakeup, 1);
        setParam (*p, P::compLookahead, (float) la);
        auto in = pinkNoise (48000, -18.0f, 3);
        juce::AudioBuffer<float> outs[3];
        const float mixes[] { 0.0f, 50.0f, 100.0f };
        int lat = 0;
        for (int k = 0; k < 3; ++k)
        {
            setParam (*p, P::compMix, mixes[k]);
            p->getStrip().reset();
            lat = p->getStrip().getLatencySamples();
            outs[k] = render (*p, in, 256);
        }
        float worst = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = lat; i < in.getNumSamples(); ++i) worst = std::max (worst, std::abs (outs[0].getSample (c, i) - in.getSample (c, i - lat)));
        const float nullDb = juce::Decibels::gainToDecibels (worst, -300.0f);
        check (nullDb <= -120.0f, "mix 0% nulls with the aligned input (makeup on, lookahead " + juce::String (la) + ")", juce::String (nullDb, 1) + " dB");

        juce::AudioBuffer<float> half (outs[0]);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < half.getNumSamples(); ++i) half.setSample (c, i, 0.5f * (outs[0].getSample (c, i) + outs[2].getSample (c, i)));
        const float lin = maxDiffDb (outs[1], half);
        check (lin <= -100.0f, "mix 50% is the exact midpoint of dry and wet", juce::String (lin, 1) + " dB");
        check (rmsDb (outs[2], 24000) < rmsDb (outs[0], 24000) + 30.0f && rmsDb (outs[2], 24000) > rmsDb (outs[0], 24000) - 6.0f,
               "wet carries the makeup, dry does not", juce::String (rmsDb (outs[2], 24000) - rmsDb (outs[0], 24000), 2) + " dB wet vs dry");
    }
}

NL_TEST (comp_autoMakeupKeepsLoudnessAtTheOperatingLevel)
{
    const char* names[] { "VCA", "FET", "OPT", "SOFT" };
    const double fs = kFs;
    double worst = 0.0;
    juce::String worstAt;
    int n = 0, within = 0;
    for (int type = 0; type < 4; ++type)
    {
        for (float ratio : { 1.5f, 2.0f, 4.0f, 10.0f })
        {
            for (float thr : { -40.0f, -30.0f, -20.0f, -10.0f })
            {
                const double err = makeupErrorLu (type, ratio, thr, fs);
                ++n;
                if (std::abs (err) <= 1.0) ++within;
                if (std::abs (err) > std::abs (worst)) { worst = err; worstAt = juce::String (names[type]) + " " + juce::String (ratio, 1) + ":1 T" + juce::String (thr, 0); }
            }
        }
    }
    check (within == n, "pink noise at the operating level stays within +/-1 LU (ratio 1.5-10, threshold -40..-10, all types)",
           juce::String (within) + "/" + juce::String (n) + ", worst " + juce::String (worst, 2) + " LU at " + worstAt);
}

NL_TEST (comp_attackIsHonest)   // D9
{
    const char* names[] { "VCA", "FET", "OPT", "SOFT" };
    double worst = 0.0;
    juce::String worstAt;
    int n = 0, ok = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int type = 0; type < 4; ++type)
        {
            for (float attack : { 1.0f, 3.0f, 10.0f, 40.0f })
            {
                if (attack < CompModule::minAttackMs (type)) continue;
                const double ms = measureCompAttackMs (type, fs, attack);
                const double err = ms / attack - 1.0;
                ++n;
                if (std::abs (err) <= 0.10) ++ok;
                if (std::abs (err) > std::abs (worst)) { worst = err; worstAt = juce::String (names[type]) + " " + juce::String (attack, 0) + " ms @" + juce::String (fs, 0); }
            }
        }
    }
    check (ok == n, "measured attack (10-90% GR on a 10 dB step) is within 10% of the knob, every type and sample rate",
           juce::String (ok) + "/" + juce::String (n) + ", worst " + juce::String (100.0 * worst, 1) + "% at " + worstAt);

    // The old hidden scaling: FET x0.5 and the 5 ms opto floor are gone.
    const double fet = measureCompAttackMs (CompModule::FET, kFs, 10.0f), opt = measureCompAttackMs (CompModule::OPT, kFs, 3.0f);
    check (fet > 9.0 && opt > 2.7 && opt < 3.3, "FET at 10 ms and OPT at 3 ms behave as displayed", juce::String (fet, 2) + " / " + juce::String (opt, 2) + " ms");
}

NL_TEST (comp_releaseIsHonestForPlainTypes)
{
    int ok = 0, n = 0;
    double worst = 0.0;
    for (double fs : { 44100.0, 96000.0 })
        for (int type : { 0, 1, 3 })
            for (float rel : { 40.0f, 150.0f, 600.0f })
            {
                const double ms = measureCompReleaseMs (type, fs, rel);
                const double err = ms / rel - 1.0;
                ++n;
                if (std::abs (err) <= 0.12) ++ok;
                if (std::abs (err) > std::abs (worst)) worst = err;
            }
    check (ok == n, "measured release (10-90% recovery) is within 12% of the knob for VCA, FET and SOFT", juce::String (ok) + "/" + juce::String (n) + ", worst " + juce::String (100.0 * worst, 1) + "%");
}

NL_TEST (comp_staticCurveMatchesTheSpecWithAnyKnee)
{
    double worst = 0.0;
    juce::String at;
    for (int type = 0; type < 4; ++type)
        for (float knee : { 0.0f, 6.0f, 12.0f, 24.0f })
            for (float ratio : { 2.0f, 4.0f, 10.0f })
            {
                const double e = staticCurveError (type, knee, ratio, -24.0f);
                if (std::abs (e) > std::abs (worst)) { worst = e; at = "type " + juce::String (type) + " knee " + juce::String (knee, 0) + " ratio " + juce::String (ratio, 0); }
            }
    check (std::abs (worst) <= 0.2, "static curve is within +/-0.2 dB of the knee formula (1 kHz sine, -60..0 dBFS)", juce::String (worst, 3) + " dB at " + at);
}

NL_TEST (comp_rangeLimitsTheGainReduction)
{
    CompParams cp; cp.thresh = -40; cp.ratio = kRatioInf; cp.knee = 0; cp.attackMs = 1; cp.rangeDb = 6.0f;
    auto in = sine (kFs, 1000.0, lin (-6.0f), 48000);
    auto c = makeModule<CompModule> (cp);
    auto o = runModule (*c, in, 64);
    const float limited = peakDb (o, 24000) - (-6.0f);
    cp.rangeDb = 40.0f;
    auto c2 = makeModule<CompModule> (cp);
    auto o2 = runModule (*c2, in, 64);
    const float free = peakDb (o2, 24000) - (-6.0f);
    check (std::abs (limited + 6.0f) < 0.3f && free < -25.0f, "range 6 dB caps the gain reduction; range 40 dB is unlimited",
           juce::String (limited, 2) + " dB / " + juce::String (free, 1) + " dB");
}

NL_TEST (comp_stereoLinkBlendsTheDetectors)
{
    // L is loud, R is quiet and well below the threshold.
    juce::AudioBuffer<float> in (2, 48000);
    for (int i = 0; i < 48000; ++i)
    {
        in.setSample (0, i, lin (-6.0f) * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs));
        in.setSample (1, i, lin (-50.0f) * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs));
    }
    float gainR[3];
    int k = 0;
    for (float link : { 0.0f, 0.5f, 1.0f })
    {
        CompParams cp; cp.thresh = -30; cp.ratio = 4; cp.knee = 0; cp.attackMs = 2; cp.link = link;
        auto c = makeModule<CompModule> (cp);
        auto o = runModule (*c, in, 64);
        gainR[k++] = rmsDb (o, 36000, 1) - rmsDb (in, 36000, 1);
    }
    // Link interpolates the two detector levels in dB: L peaks at -6 dB, R at -50 dB, so 50% gives
    // a detector level of -28 dB, 2 dB over the -30 dB threshold, and 0.75 x 2 = -1.5 dB of gain
    // reduction on R. 100% gives R the full -18 dB of L.
    check (std::abs (gainR[0]) < 0.05f && std::abs (gainR[1] + 1.5f) < 0.1f && std::abs (gainR[2] + 18.0f) < 0.1f,
           "link 0% leaves R alone, 50% interpolates the detector levels in dB (-1.5 dB), 100% reduces R like L",
           juce::String (gainR[0], 2) + " / " + juce::String (gainR[1], 2) + " / " + juce::String (gainR[2], 2) + " dB");
}

NL_TEST (comp_autoReleaseHasTwoTimeConstants)
{
    const double burst = measureCompAutoRelease (50.0, true), sustained = measureCompAutoRelease (4000.0, true);
    const double plainBurst = measureCompAutoRelease (50.0, false), plainSustained = measureCompAutoRelease (4000.0, false);
    check (burst > 80.0 && burst < 170.0 && sustained > 3.0 * burst, "auto release: fast after a burst, slow after sustained compression",
           "burst " + juce::String (burst, 0) + " ms, sustained " + juce::String (sustained, 0) + " ms (knob 100 ms)");
    check (std::abs (plainBurst / 100.0 - 1.0) < 0.12 && std::abs (plainSustained / 100.0 - 1.0) < 0.12, "without auto release both match the knob",
           juce::String (plainBurst, 0) + " / " + juce::String (plainSustained, 0) + " ms");
}

NL_TEST (comp_optMemoryIsResetAndRateIndependent)   // D9
{
    CompParams cp; cp.type = CompModule::OPT; cp.thresh = -30; cp.ratio = 6; cp.releaseMs = 200;
    auto loud = sine (kFs, 500.0, lin (-6.0f), 3 * 48000);
    auto quietThenLoud = sine (kFs, 500.0, lin (-20.0f), 24000);
    auto a = makeModule<CompModule> (cp);
    runModule (*a, loud, 256);
    a->reset();
    auto b = makeModule<CompModule> (cp);
    // maxDiffDb floors at -300 dB, which is what a bit-exact match returns, so the test is "not above the floor".
    check (maxDiffDb (runModule (*a, quietThenLoud, 100), runModule (*b, quietThenLoud, 100)) <= -300.0f, "reset() clears the opto memory and all detector state (bit exact against a fresh module)");

    // The memory runs on control ticks: the recovery after the same program is the same at
    // every sample rate.
    double rel[2];
    int k = 0;
    for (double fs : { 44100.0, 96000.0 })
    {
        rel[k++] = measureCompAutoRelease (3000.0, false, CompModule::OPT, fs);
    }
    check (std::abs (rel[0] / rel[1] - 1.0) < 0.12, "opto release after 3 s of compression is sample-rate independent", juce::String (rel[0], 0) + " / " + juce::String (rel[1], 0) + " ms");
}

NL_TEST (comp_lookaheadAnticipatesTheTransient)
{
    CompParams cp; cp.thresh = -30; cp.ratio = 20; cp.knee = 0; cp.attackMs = 1.5f; cp.releaseMs = 100;
    juce::AudioBuffer<float> in (2, 24000);
    in.clear();
    for (int i = 4800; i < 24000; ++i) { const float v = lin (-6.0f) * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs); in.setSample (0, i, v); in.setSample (1, i, v); }
    float overshoot[2];
    int k = 0;
    for (int la : { 0, 3 })
    {
        cp.lookahead = la;
        auto c = makeModule<CompModule> (cp);
        auto o = runModule (*c, in, 64);
        const int lat = c->latency();
        overshoot[k++] = peakDbRange (o, 4800 + lat, 4800 + lat + 96) - peakDbRange (o, 20000, 24000);
    }
    check (overshoot[1] < overshoot[0] - 4.0f && overshoot[1] < 1.0f, "5 ms lookahead removes the attack overshoot at the onset",
           "no lookahead +" + juce::String (overshoot[0], 1) + " dB, lookahead +" + juce::String (overshoot[1], 1) + " dB over the settled level");
}

NL_TEST (comp_listenKeyAndDeltaAreAligned)
{
    auto in = pinkNoise (24000, -14.0f, 5);
    CompParams cp; cp.thresh = -30; cp.ratio = 6; cp.lookahead = 2; cp.scHp = 2000.0f; cp.knee = 3;
    // Key listen: the high-passed sidechain, delayed with the audio.
    cp.listen = 1;
    auto k = makeModule<CompModule> (cp);
    auto ok = runModule (*k, in, 128);
    auto hp = makeSvf (SvfType::HighPass, kFs, 2000.0f, 0.7071f);
    SvfState st;
    float worst = 0.0f;
    for (int i = 0; i < 24000 - 96; ++i)
    {
        const float ref = st.process (in.getSample (0, i), hp);
        worst = std::max (worst, std::abs (ref - ok.getSample (0, i + 96)));
    }
    // Sample-exact against the filtered sidechain (the content check); the level is only reported, since a
    // 2 kHz high-pass of pink noise sits about 6 dB under the full band by itself.
    check (worst < 1.0e-5f, "Key listen outputs the filtered sidechain, aligned with the audio",
           juce::String (juce::Decibels::gainToDecibels (worst, -300.0f), 1) + " dB, key " + juce::String (rmsDb (ok, 5000), 1) + " dB vs input " + juce::String (rmsDb (in, 5000), 1) + " dB");

    // Delta listen: wet + delta == aligned dry.
    cp.listen = 0; cp.scHp = 20.0f;
    auto w = makeModule<CompModule> (cp);
    auto wet = runModule (*w, in, 128);
    cp.listen = 2;
    auto d = makeModule<CompModule> (cp);
    auto delta = runModule (*d, in, 128);
    float worst2 = 0.0f;
    for (int i = 96; i < 24000; ++i) worst2 = std::max (worst2, std::abs (wet.getSample (1, i) + delta.getSample (1, i) - in.getSample (1, i - 96)));
    check (juce::Decibels::gainToDecibels (worst2, -300.0f) < -110.0f && rmsDb (delta, 5000) > -60.0f, "Delta listen is exactly what the compressor removes (wet + delta = aligned dry)",
           juce::String (juce::Decibels::gainToDecibels (worst2, -300.0f), 1) + " dB");
}

NL_TEST (comp_fetColourIsAntiAliased)
{
    // 5 kHz tone driven hard: the colour sits in the post-gain signal. Alias energy outside the
    // true harmonics, relative to the total (same measure as the PRE saturation target).
    CompParams cp; cp.type = CompModule::FET; cp.thresh = -6; cp.ratio = 30; cp.knee = 0; cp.attackMs = 0.05f; cp.releaseMs = 50;
    const int N = 8192, bin = 853;
    const double f0 = bin * kFs / N;
    auto in = sine (kFs, f0, lin (6.0f), N * 4);
    auto c = makeModule<CompModule> (cp);
    auto o = runModule (*c, in, 64);
    const double alias = aliasDb (o, 0, 3 * N, N, bin);
    check (alias <= -40.0, "FET colour: alias energy <= -40 dB re total (5 kHz, heavy gain reduction)", juce::String (alias, 1) + " dB");

    // The colour itself is there and grows with the gain reduction.
    const double h3Light = harmonicDb (runModule (*makeModule<CompModule> (withThresh (cp, -1.0f, 4.0f)), in, 64), 0, 3 * N, N, bin, 3);
    const double h3Heavy = harmonicDb (o, 0, 3 * N, N, bin, 3);
    std::printf ("         FET third harmonic: %.1f dB (light), %.1f dB (heavy)\n", h3Light, h3Heavy);
    check (h3Heavy > -75.0 && h3Heavy < -30.0, "FET produces odd-harmonic colour under gain reduction", juce::String (h3Heavy, 1) + " dB");
}

//==============================================================================
// GATE
//==============================================================================
NL_TEST (gate_noChatterOnADecaying40HzSine)   // 7.4 detector
{
    for (float hp : { 20.0f, 30.0f })
    {
        GateParams gp; gp.thresh = -30; gp.hyst = 4; gp.holdMs = 0; gp.releaseMs = 20; gp.attackMs = 0.5f; gp.floorDb = -40; gp.scHp = hp;
        auto g = makeModule<GateModule> (gp);
        // 40 Hz sine decaying 20 dB per second through the threshold, starting above it.
        const int n = 3 * 48000;
        juce::AudioBuffer<float> in (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kFs;
            const float v = lin (-20.0f - 20.0f * (float) t) * (float) std::sin (2.0 * kPiD * 40.0 * t);
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        int toggles = 0;
        bool closed = false;
        juce::ScopedNoDenormals nd;
        auto out = in;
        for (int pos = 0; pos < n; pos += 32)
        {
            float* io[2] { out.getWritePointer (0) + pos, out.getWritePointer (1) + pos };
            g->process (io, 2, 32, nullptr);
            const bool nowClosed = g->getGainReductionDb() < -20.0f;
            if (nowClosed != closed) { ++toggles; closed = nowClosed; }
        }
        check (toggles <= 2 && closed, "40 Hz sine decaying through the threshold: at most one open/close cycle (SC HP " + juce::String (hp, 0) + " Hz, hold 0)", juce::String (toggles) + " transitions");
    }
}

NL_TEST (gate_expanderLawIsContinuous)   // D14
{
    for (float ratio : { 2.0f, 4.0f })
        for (float knee : { 0.0f, 6.0f })
        {
            const auto r = expanderSweep (ratio, knee, 12.0f);
            const auto r0 = expanderSweep (ratio, knee, 0.0f);
            check (r.maxJump < 0.25 * (ratio - 1.0) + 0.1, "expander " + juce::String (ratio, 0) + ":1 knee " + juce::String (knee, 0) + " dB: no jump anywhere in the transfer curve",
                   "largest step per 0.25 dB of input: " + juce::String (r.maxJump, 3) + " dB");
            check (r.maxErr < 0.5, "expander " + juce::String (ratio, 0) + ":1 knee " + juce::String (knee, 0) + " dB: follows the downward law and the floor", "max error " + juce::String (r.maxErr, 2) + " dB");
            check (std::abs (r.checksum - r0.checksum) < 0.05, "hysteresis does not touch the expander law");
        }
}

NL_TEST (gate_duckAttenuatesWhileTheKeyIsActive)
{
    const int n = 4 * 48000;
    juce::AudioBuffer<float> main = sine (kFs, 440.0, lin (-10.0f), n), key (2, n);
    key.clear();
    for (int i = 24000; i < 72000; ++i) { const float v = lin (-10.0f) * (float) std::sin (2.0 * kPiD * 100.0 * i / kFs); key.setSample (0, i, v); key.setSample (1, i, v); }
    GateParams gp; gp.type = GateModule::Duck; gp.ext = true; gp.thresh = -30; gp.floorDb = -24; gp.attackMs = 5; gp.holdMs = 20; gp.releaseMs = 100;
    auto g = makeModule<GateModule> (gp);
    auto o = runModule (*g, main, 128, &key);
    const float before = rmsDbRange (o, 10000, 22000) - rmsDbRange (main, 10000, 22000);
    const float during = rmsDbRange (o, 48000, 70000) - rmsDbRange (main, 48000, 70000);
    const float after = rmsDbRange (o, 150000, 190000) - rmsDbRange (main, 150000, 190000);
    check (std::abs (before) < 0.05f && std::abs (during + 24.0f) < 0.6f && std::abs (after) < 0.05f, "Duck: unity, -24 dB while the key is above threshold, unity again",
           juce::String (before, 2) + " / " + juce::String (during, 2) + " / " + juce::String (after, 2) + " dB");
}

NL_TEST (gate_lookaheadKeepsTheKickTransient)
{
    float loss[2];
    int k = 0;
    for (int la : { 0, 2 })
    {
        loss[k++] = kickLossDb (la);
    }
    check (loss[1] <= 0.5f, "kick transient loss in the first 5 ms <= 0.5 dB with 2 ms lookahead", juce::String (loss[1], 2) + " dB (" + juce::String (loss[0], 2) + " dB without)");
    // With the default 0.5 ms opening the detector already reacts at the onset, so the lookahead buys only
    // a fraction of a dB here. Its job is to cover detector and filter delay, so the check is that it never
    // costs transient energy.
    check (loss[0] >= loss[1] - 0.05f, "lookahead never costs transient energy", juce::String (loss[0], 2) + " dB without, " + juce::String (loss[1], 2) + " dB with");
}

NL_TEST (gate_openingIsClickFree)
{
    // The requirement (-60 dB above 8 kHz) is for the default 0.5 ms opening. A 0.1 ms dB-domain ramp is
    // too short to be that quiet: its linear-gain transition is about 25 us long. -60 dB is first reached
    // around 0.3 ms, so the 0.1 ms case is only bounded here.
    const auto def = gateOpeningClick (0.5f);
    check (def.splatterDb <= -60.0f, "opening click-free at -60 dB re signal (0.5 ms default)", juce::String (def.splatterDb, 1) + " dB above 8 kHz, derivative step x" + juce::String (def.stepRatio, 2));
    const auto fast = gateOpeningClick (0.1f);
    check (fast.splatterDb <= -40.0f, "0.1 ms opening is bounded at -40 dB re signal (-60 dB needs about 0.3 ms)", juce::String (fast.splatterDb, 1) + " dB above 8 kHz");
}

//==============================================================================
// DE-ESS
//==============================================================================
NL_TEST (ds_splitModeLeavesTheRestOfTheSpectrumAlone)   // 7.5 target
{
    const auto split = deEssSpectrum (0), wide = deEssSpectrum (1);
    check (split.grDb < -4.0f, "Split: de-esser is working on the synthetic vocal", "GR " + juce::String (split.grDb, 1) + " dB");
    check (std::abs (split.outsideDb) <= 0.3f, "Split: spectral change outside the band <= 0.3 dB at matched GR", juce::String (split.outsideDb, 2) + " dB (100 Hz - 1.5 kHz), GR " + juce::String (split.grDb, 1) + " dB");
    check (wide.outsideDb < -2.0f && std::abs (wide.grDb - split.grDb) < 2.0f, "Wide: the whole signal follows the band detector at the same GR",
           juce::String (wide.outsideDb, 2) + " dB outside, GR " + juce::String (wide.grDb, 1) + " dB");
}

NL_TEST (ds_attackAndReleaseAreHonest)
{
    int n = 0, ok = 0;
    double worst = 0.0;
    for (int mode : { 0, 1 })
        for (float attack : { 0.5f, 2.0f, 5.0f })
        {
            const double a = measureDsAttackMs (mode, attack, 60.0f);
            const double err = a / attack - 1.0;
            ++n;
            if (std::abs (err) <= 0.12) ++ok;
            if (std::abs (err) > std::abs (worst)) worst = err;
        }
    check (ok == n, "DE-ESS attack knob (was fixed at 0.4 ms) is the measured 10-90% time, Split and Wide", juce::String (ok) + "/" + juce::String (n) + ", worst " + juce::String (100.0 * worst, 1) + "%");
    const double rel = measureDsReleaseMs (60.0f);
    check (std::abs (rel / 60.0 - 1.0) < 0.12, "release knob matches the measured recovery", juce::String (rel, 1) + " ms for 60 ms");
}

NL_TEST (ds_listenBandAndDeltaAreAligned)
{
    auto in = pinkNoise (24000, -16.0f, 9);
    DsParams dp; dp.relative = false; dp.lookahead = 2; dp.band[0].thresh = -30; dp.band[0].type = 3; dp.band[0].freq = 6000; dp.band[1].on = false;
    dp.listen = 1;
    auto b = makeModule<DeEsserModule> (dp);
    auto ob = runModule (*b, in, 100);
    auto hp = makeSvf (SvfType::HighPass, kFs, 6000.0f, 0.7071f);
    SvfState st;
    float worst = 0.0f;
    for (int i = 0; i < 24000 - 96; ++i) worst = std::max (worst, std::abs (st.process (in.getSample (0, i), hp) - ob.getSample (0, i + 96)));
    check (worst < 1.0e-5f, "Band listen outputs the detector band, aligned with the audio", juce::String (juce::Decibels::gainToDecibels (worst), 1) + " dB");

    dp.listen = 0;
    auto w = makeModule<DeEsserModule> (dp);
    auto wet = runModule (*w, in, 100);
    dp.listen = 3;
    auto d = makeModule<DeEsserModule> (dp);
    auto delta = runModule (*d, in, 100);
    float worst2 = 0.0f;
    for (int i = 96; i < 24000; ++i) worst2 = std::max (worst2, std::abs (wet.getSample (0, i) + delta.getSample (0, i) - in.getSample (0, i - 96)));
    check (juce::Decibels::gainToDecibels (worst2, -300.0f) < -110.0f && rmsDb (delta, 5000) > -70.0f, "Delta listen is exactly what the de-esser removes (wet + delta = aligned dry)",
           juce::String (juce::Decibels::gainToDecibels (worst2, -300.0f), 1) + " dB");
}

NL_TEST (ds_reenabledBandStartsClean)   // D14
{
    DsParams dp; dp.relative = false; dp.band[0].thresh = -40; dp.band[1].on = false;
    auto loud = sine (kFs, 8000.0, lin (-6.0f), 24000);
    auto probe = pinkNoise (24000, -20.0f, 4);
    auto a = makeModule<DeEsserModule> (dp);
    runModule (*a, loud, 77);
    dp.band[0].on = false;
    a->setParams (dp);
    juce::AudioBuffer<float> silence (2, 96000);
    silence.clear();
    runModule (*a, silence, 77);
    dp.band[0].on = true;
    a->setParams (dp);
    // Let the cross-fade of the switch finish on silence before the probe.
    runModule (*a, silence, 77);
    auto b = makeModule<DeEsserModule> (dp);
    check (maxDiffDb (runModule (*a, probe, 99), runModule (*b, probe, 99)) < -110.0f, "a band that is re-enabled carries no stale filter or detector state");
}

//==============================================================================
// Determinism (D3, D8, D12) and click-free switching (D10)
//==============================================================================
NL_TEST (dynamics_identicalAcrossBlockSizesUnderAutomation)
{
    struct Case { const char* name; std::vector<int> params; std::function<void (NorthlineProcessor&)> setup; };
    const std::vector<Case> cases {
        { "COMP", { P::compThresh, P::compRatio, P::compKnee, P::compAttack, P::compRelease, P::compMix, P::compOut, P::compLink, P::compRange,
                    P::compScHp, P::compScLp, P::compScBellGain, P::compMakeup, P::compAutoRel, P::compType, P::compMode },
          [] (NorthlineProcessor& p) { setParam (p, P::compOn, 1); setParam (p, P::compLookahead, 2); setParam (p, P::compThresh, -26); } },
        { "GATE", { P::gateThresh, P::gateHyst, P::gateFloor, P::gateRatio, P::gateKnee, P::gateAttack, P::gateHold, P::gateRelease,
                    P::gateScHp, P::gateScLp, P::gateType, P::gateMode },
          [] (NorthlineProcessor& p) { setParam (p, P::gateOn, 1); setParam (p, P::gateLookahead, 2); setParam (p, P::gateThresh, -34); } },
        { "DE-ESS", { P::dsAttack, P::dsRelease, P::dsOut, P::dsMode, P::dsRelative, P::ds1Freq, P::ds1Thresh, P::ds1Range, P::ds1Mode, P::ds1Type, P::ds1On,
                      P::ds2Freq, P::ds2Thresh, P::ds2Range, P::ds2Mode, P::ds2Type, P::ds2On },
          [] (NorthlineProcessor& p) { setParam (p, P::dsOn, 1); setParam (p, P::dsLookahead, 2); setParam (p, P::ds2On, 1); setParam (p, P::ds1Thresh, -34); } },
    };
    const auto in = programMaterial (6 * 48000, 21);
    for (const auto& cs : cases)
    {
        // Worst difference against block size 1 over all block sizes, for one set of automated parameters.
        auto worstFor = [&] (const std::vector<int>& params, int seed)
        {
            const auto events = randomAutomation (params, 5 * 48000, seed);
            juce::AudioBuffer<float> ref;
            float worst = -300.0f;
            for (int bs : kBlockSizes)
            {
                auto p = makeProc (kFs, 2048);
                allModulesOff (*p);
                cs.setup (*p);
                p->getStrip().reset();
                auto out = renderWithAutomation (*p, in, bs, events);
                if (bs == kBlockSizes[0]) ref = out;
                else worst = std::max (worst, maxDiffDb (out, ref));
            }
            return worst;
        };
        const float worst = worstFor (cs.params, 77);
        check (worst <= -120.0f, juce::String (cs.name) + ": identical output for block sizes 1, 7, 32, 37, 64, 511, 512, 2048 with automation (<= 1e-6)", juce::String (worst, 1) + " dB");
        if (worst > -120.0f)
            for (int param : cs.params)
            {
                const float w = worstFor ({ param }, 78);
                if (w > -120.0f) std::printf ("         diagnostic: %s alone differs by %.1f dB\n", paramDef (param).id, w);
            }
    }
}

NL_TEST (dynamics_switchesAreClickFree)
{
    struct Switch { const char* name; std::function<void (NorthlineProcessor&)> setup; std::vector<AutomationEvent> events; double f0; };
    const int step = 24000;
    auto seq = [&] (int param, std::initializer_list<float> values)
    {
        std::vector<AutomationEvent> e;
        int k = 1;
        for (float v : values) e.push_back ({ step * k++, param, v });
        return e;
    };
    const std::vector<Switch> sw {
        { "COMP type", [] (NorthlineProcessor& p) { setParam (p, P::compOn, 1); setParam (p, P::compThresh, -26); setParam (p, P::compRatio, 4); }, seq (P::compType, { 1, 2, 3, 0 }), 1000.0 },
        { "COMP type (100 Hz)", [] (NorthlineProcessor& p) { setParam (p, P::compOn, 1); setParam (p, P::compThresh, -26); setParam (p, P::compRatio, 4); }, seq (P::compType, { 1, 2, 3, 0 }), 100.0 },
        { "COMP knee", [] (NorthlineProcessor& p) { setParam (p, P::compOn, 1); setParam (p, P::compThresh, -14); setParam (p, P::compRatio, 4); setParam (p, P::compKnee, 0); }, seq (P::compKnee, { 24, 0, 12, 0 }), 1000.0 },
        { "COMP link", [] (NorthlineProcessor& p) { setParam (p, P::compOn, 1); setParam (p, P::compThresh, -30); setParam (p, P::compRatio, 6); }, seq (P::compLink, { 0, 100, 20, 100 }), 1000.0 },
        { "COMP mix and listen", [] (NorthlineProcessor& p) { setParam (p, P::compOn, 1); setParam (p, P::compThresh, -30); setParam (p, P::compRatio, 6); }, seq (P::compListen, { 1, 0, 2, 0 }), 1000.0 },
        { "GATE type", [] (NorthlineProcessor& p) { setParam (p, P::gateOn, 1); setParam (p, P::gateThresh, -30); setParam (p, P::gateFloor, -30); }, seq (P::gateType, { 1, 2, 0, 2 }), 1000.0 },
        { "GATE type (100 Hz)", [] (NorthlineProcessor& p) { setParam (p, P::gateOn, 1); setParam (p, P::gateThresh, -30); setParam (p, P::gateFloor, -30); }, seq (P::gateType, { 1, 2, 0, 2 }), 100.0 },
        { "DE-ESS band mode", [] (NorthlineProcessor& p) { setParam (p, P::dsOn, 1); setParam (p, P::ds1Type, 1); setParam (p, P::ds1Freq, 1000); setParam (p, P::ds1Thresh, -40); setParam (p, P::dsRelative, 0); }, seq (P::ds1Mode, { 1, 0, 1, 0 }), 1000.0 },
        { "DE-ESS band type", [] (NorthlineProcessor& p) { setParam (p, P::dsOn, 1); setParam (p, P::ds1Type, 1); setParam (p, P::ds1Freq, 1500); setParam (p, P::ds1Thresh, -50); setParam (p, P::dsRelative, 0); }, seq (P::ds1Type, { 0, 2, 3, 1 }), 1000.0 },
        { "DE-ESS band on", [] (NorthlineProcessor& p) { setParam (p, P::dsOn, 1); setParam (p, P::ds1Type, 1); setParam (p, P::ds1Freq, 1000); setParam (p, P::ds1Thresh, -40); setParam (p, P::dsRelative, 0); }, seq (P::ds1On, { 0, 1, 0, 1 }), 1000.0 },
    };
    for (const auto& s : sw)
    {
        auto p = makeProc (kFs, 512);
        allModulesOff (*p);
        s.setup (*p);
        p->getStrip().reset();
        // L and R carry different levels so stereo-link changes are audible in both.
        juce::AudioBuffer<float> in (2, step * 6);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float v = (float) std::sin (2.0 * kPiD * s.f0 * i / kFs);
            in.setSample (0, i, lin (-9.0f) * v);
            in.setSample (1, i, lin (-21.0f) * v);
        }
        auto out = renderWithAutomation (*p, in, 256, s.events);
        float worst = -300.0f, worstStep = 0.0f;
        for (const auto& e : s.events)
            for (int ch = 0; ch < 2; ++ch)
            {
                worst = std::max (worst, splatterDb (out, ch, e.sample - 1200, 2400, kFs, s.f0, 8.0 * s.f0));
                worstStep = std::max (worstStep, stepRatio (out, in, ch, e.sample - 1200, 2400));
            }
        check (worst <= -60.0f && worstStep <= 2.0f, juce::String (s.name) + " switch: splatter <= -60 dB, no derivative step > 2x the signal's",
               juce::String (worst, 1) + " dB, step x" + juce::String (worstStep, 2));
    }
}

NL_TEST (ds_identicalAcrossBlockSizesAgainst2048)   // D3/D8/D12: persistent control phase
{
    const auto in = programMaterial (6 * 48000, 31);
    const std::vector<int> params { P::dsAttack, P::dsRelease, P::dsOut, P::ds1Freq, P::ds1Thresh, P::ds1Range, P::ds2Thresh, P::ds2Range };
    const int sizes[] { 1, 7, 37, 64, 511, 2048 };
    // Two configurations: the defaults (no lookahead, one band) and both bands with 2 ms lookahead.
    for (int variant = 0; variant < 2; ++variant)
    for (bool automated : { false, true })
    {
        const auto events = automated ? randomAutomation (params, 5 * 48000, 91) : std::vector<AutomationEvent> {};
        auto render = [&] (int bs)
        {
            auto p = makeProc (kFs, 2048);
            allModulesOff (*p);
            setParam (*p, P::dsOn, 1);
            if (variant == 1)
            {
                setParam (*p, P::dsLookahead, 2);
                setParam (*p, P::ds2On, 1);
                setParam (*p, P::ds1Thresh, -34);
            }
            p->getStrip().reset();
            return renderWithAutomation (*p, in, bs, events);
        };
        const auto ref = render (2048);
        if (! automated)
        {
            // Against the input delayed by the 2 ms lookahead: the band must actually be cutting something.
            float change = 0.0f;
            for (int i = 96; i < in.getNumSamples(); ++i) change = std::max (change, std::abs (ref.getSample (0, i) - in.getSample (0, i - 96)));
            check (juce::Decibels::gainToDecibels (change, -300.0f) > -20.0f, "the de-esser is active on this material (so the comparison means something)",
                   juce::String (juce::Decibels::gainToDecibels (change, -300.0f), 1) + " dB");
        }
        float worst = -300.0f;
        int worstBs = 0;
        for (int bs : sizes)
        {
            if (bs == 2048) continue;
            const float d = maxDiffDb (render (bs), ref);
            if (d > worst) { worst = d; worstBs = bs; }
        }
        check (worst <= -120.0f, juce::String ("DE-ESS identical for block sizes 1, 7, 37, 64, 511, 2048 against 2048, ") + (variant == 0 ? "defaults" : "2 bands, 2 ms lookahead") + ", " + (automated ? "with" : "without") + " automation (<= 1e-6)",
               juce::String (worst, 1) + " dB at block " + juce::String (worstBs));
    }
}
} // namespace tool

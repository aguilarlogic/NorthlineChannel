#include "TestUtil.h"
#include <complex>
#include <map>

using namespace nl;

namespace tool
{
namespace
{
//==============================================================================
void testFastMath()
{
    std::puts ("Fast math");
    double maxLogErr = 0.0, maxExpErr = 0.0;
    for (double x = 1.0e-6; x < 1.0e4; x *= 1.0007)
        maxLogErr = std::max (maxLogErr, std::abs ((double) fastLog2 ((float) x) - std::log2 (x)));
    for (double x = -30.0; x < 30.0; x += 0.0007)
        maxExpErr = std::max (maxExpErr, std::abs ((double) fastExp2 ((float) x) / std::exp2 (x) - 1.0));
    check (maxLogErr < 2.0e-6 * 4, "fastLog2 max abs error", juce::String (maxLogErr, 9) + " (= " + juce::String (maxLogErr * 6.0206, 7) + " dB)");
    check (maxExpErr < 3.0e-6 * 4, "fastExp2 max relative error", juce::String (maxExpErr, 9));
}

void testParams()
{
    std::puts ("Parameters");
    std::map<juce::String, int> ids;
    for (int i = 0; i < P::count; ++i) ids[paramID (i)]++;
    bool unique = true;
    for (auto& [id, n] : ids) unique &= n == 1;
    check (unique && (int) ids.size() == P::count, "parameter IDs unique", juce::String (P::count) + " parameters");
}

void testNullAndLatency()
{
    std::puts ("Transparency and latency");
    auto p = makeProc();
    juce::Random rng (1);
    juce::AudioBuffer<float> in (2, 48000);
    fillNoise (in, rng, 0.5f);
    auto out = render (*p, in, 256);
    float maxDiff = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < in.getNumSamples(); ++i) maxDiff = std::max (maxDiff, std::abs (out.getSample (c, i) - in.getSample (c, i)));
    check (maxDiff == 0.0f, "default preset is bit-exact transparent", "max diff " + juce::String (maxDiff, 9));
    check (p->getLatencySamples() == 0, "default latency is 0 samples");

    configureEverything (*p, false, false);
    p->getStrip().reset();
    check (p->getStrip().getLatencySamples() == 0, "all modules on (zero-latency limiter): latency 0");

    setParam (*p, P::gateFloor, 0);
    p->getStrip().reset();
    juce::AudioBuffer<float> imp (2, 4096);
    imp.clear();
    imp.setSample (0, 1000, 0.5f);
    imp.setSample (1, 1000, 0.5f);
    auto o = render (*p, imp, 128);
    int argMax = 0;
    float m = 0.0f;
    for (int i = 0; i < 4096; ++i)
        if (std::abs (o.getSample (0, i)) > m) { m = std::abs (o.getSample (0, i)); argMax = i; }
    check (std::abs (o.getSample (0, 1000)) > 0.1f * m && argMax - 1000 <= 2, "impulse response starts at t = 0 (no pure delay)",
           "peak at +" + juce::String (argMax - 1000) + " samples");

    configureEverything (*p, false, true);
    p->getStrip().reset();
    check (p->getStrip().getLatencySamples() == 72, "lookahead limiter reports 1.5 ms (72 samples @ 48 kHz)", juce::String (p->getStrip().getLatencySamples()));
}

void testFilters()
{
    std::puts ("Filters and EQ");
    auto p = makeProc();
    allModulesOff (*p);
    setParam (*p, P::preOn, 1);
    setParam (*p, P::hpOn, 1);
    setParam (*p, P::hpFreq, 100);
    setParam (*p, P::hpSlope, 3);
    float g100 = gainAt (*p, 100), g50 = gainAt (*p, 50), g1k = gainAt (*p, 1000);
    check (std::abs (g100 + 3.01f) < 0.15f, "HP 24 dB/oct: -3 dB at cutoff", juce::String (g100, 2) + " dB");
    check (std::abs (g50 + 24.1f) < 0.6f, "HP 24 dB/oct: -24 dB one octave below", juce::String (g50, 2) + " dB");
    check (std::abs (g1k) < 0.05f, "HP 24 dB/oct: flat in passband", juce::String (g1k, 3) + " dB");
    setParam (*p, P::hpSlope, 1);
    g50 = gainAt (*p, 50);
    check (std::abs (g50 + 12.3f) < 0.4f, "HP 12 dB/oct: -12.3 dB one octave below", juce::String (g50, 2) + " dB");
    setParam (*p, P::hpOn, 0);

    setParam (*p, P::eqOn, 1);
    setParam (*p, P::eqMidType, 2);
    setParam (*p, P::eqMidFreq, 1000);
    setParam (*p, P::eqMidGain, 6);
    setParam (*p, P::eqMidQ, 1);
    const float e1k = gainAt (*p, 1000), e100 = gainAt (*p, 100);
    check (std::abs (e1k - 6.0f) < 0.05f, "EQ bell +6 dB at 1 kHz", juce::String (e1k, 3) + " dB");
    check (std::abs (e100) < 0.35f, "EQ bell leaves 100 Hz nearly untouched", juce::String (e100, 3) + " dB");

    setParam (*p, P::eqMidGain, 0);
    setParam (*p, P::eqHighType, 0);
    setParam (*p, P::eqHighFreq, 8000);
    setParam (*p, P::eqHighGain, -6);
    const float h20k = gainAt (*p, 19000), h100 = gainAt (*p, 100);
    check (std::abs (h20k + 6.0f) < 0.4f, "High shelf -6 dB reaches -6 dB at 19 kHz", juce::String (h20k, 2) + " dB");
    check (std::abs (h100) < 0.05f, "High shelf flat at 100 Hz", juce::String (h100, 3) + " dB");
}

void testSaturation()
{
    std::puts ("Saturation");
    auto p = makeProc();
    allModulesOff (*p);
    setParam (*p, P::preOn, 1);
    setParam (*p, P::satOn, 1);
    setParam (*p, P::satType, 0);
    setParam (*p, P::satDrive, 0);
    const float a1 = gainAt (*p, 1000, -40), a10 = gainAt (*p, 10000, -40), a16 = gainAt (*p, 16000, -40);
    // Level compensation matches a -12 dBFS peak, so small signals sit a fraction of a dB
    // above unity; flatness is judged relative to 1 kHz.
    check (std::abs (a1) < 0.5f, "saturation at 0% drive is near unity for small signals", juce::String (a1, 2) + " dB @ 1 kHz, -40 dBFS");
    check (std::abs (a10 - a1) < 0.3f && std::abs (a16 - a1) < 0.6f,
           "ADAA path is flat to 16 kHz (de-emphasis compensation)", juce::String (a10 - a1, 2) + " / " + juce::String (a16 - a1, 2) + " dB @ 10k/16k re 1k");

    // Aliasing: bin-exact 5 kHz tone at full drive, energy outside the true harmonics.
    auto aliasDb = [&] (bool hq, int type)
    {
        setParam (*p, P::quality, hq ? 1.0f : 0.0f);
        setParam (*p, P::satType, (float) type);
        setParam (*p, P::satDrive, 100);
        p->getStrip().reset();
        const int N = 8192, bin = 853; // 853 * 48000 / 8192 = 4998 Hz
        const double fs = 48000.0, f0 = bin * fs / N;
        auto in = sine (fs, f0, juce::Decibels::decibelsToGain (-6.0f), N * 3);
        auto out = render (*p, in, 256);
        std::vector<std::complex<double>> X ((size_t) N);
        double total = 0.0, alias = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            std::complex<double> acc = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPiD * i / N);
                acc += w * out.getSample (0, 2 * N + i) * std::polar (1.0, -2.0 * kPiD * k * i / N);
            }
            const double e = std::norm (acc);
            total += e;
            bool harmonic = false;
            for (int h = 1; h * bin < N / 2; ++h) harmonic |= std::abs (k - h * bin) <= 3;
            if (! harmonic && k > 4) alias += e;
        }
        return 10.0 * std::log10 (alias / total + 1.0e-30);
    };
    const double eco = aliasDb (false, 0), hq = aliasDb (true, 0);
    check (hq < eco - 3.0, "HQ oversampling lowers aliasing (Odd, 100% drive, 5 kHz)",
           "Eco " + juce::String (eco, 1) + " dB, HQ " + juce::String (hq, 1) + " dB relative to total");
    const double crushEco = aliasDb (false, 3), crushHq = aliasDb (true, 3);
    std::printf ("         Crush alias level: Eco %.1f dB, HQ %.1f dB\n", crushEco, crushHq);
}

void testDynamics()
{
    std::puts ("Dynamics");
    auto p = makeProc();
    allModulesOff (*p);
    setParam (*p, P::compOn, 1);
    setParam (*p, P::compType, 0);
    setParam (*p, P::compThresh, -20);
    setParam (*p, P::compRatio, 4);
    setParam (*p, P::compAttack, 1);
    setParam (*p, P::compRelease, 80);
    p->getStrip().reset();
    auto in = sine (48000.0, 200.0, juce::Decibels::decibelsToGain (-8.0f), 48000);
    auto out = render (*p, in, 256);
    const float pk = peakDb (out, 24000);
    check (std::abs (pk + 17.0f) < 0.75f, "VCA static curve: -8 dBFS in, T -20, 4:1 -> -17 dBFS", juce::String (pk, 2) + " dBFS");

    setParam (*p, P::compType, 1);
    setParam (*p, P::compRatio, kRatioInf);
    setParam (*p, P::compAttack, 0.1f);
    p->getStrip().reset();
    juce::Random rng (7);
    juce::AudioBuffer<float> noise (2, 96000);
    fillNoise (noise, rng, 1.0f);
    out = render (*p, noise, 64);
    bool finite = true;
    float mx = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < out.getNumSamples(); ++i) { finite &= std::isfinite (out.getSample (c, i)); mx = std::max (mx, std::abs (out.getSample (c, i))); }
    check (finite && mx < 2.0f, "FET at inf:1 and 0.1 ms attack is stable (implicit feedback)", "max |y| " + juce::String (mx, 3));

    // Limiter: zero-latency and lookahead both hold the ceiling.
    allModulesOff (*p);
    setParam (*p, P::limOn, 1);
    setParam (*p, P::limCeiling, -1);
    for (bool la : { false, true })
    {
        setParam (*p, P::limMode, la ? 1.0f : 0.0f);
        p->getStrip().reset();
        auto loud = sine (48000.0, 100.0, juce::Decibels::decibelsToGain (6.0f), 48000);
        juce::Random r2 (3);
        for (int i = 0; i < loud.getNumSamples(); i += 997) { loud.setSample (0, i, 3.0f); loud.setSample (1, i, -3.0f); }
        auto lo = render (*p, loud, 128);
        const float lp = std::max (peakDb (lo, 0, 0), peakDb (lo, 0, 1));
        check (lp <= -1.0f + 0.001f, la ? "lookahead limiter holds -1 dBFS ceiling" : "zero-latency limiter holds -1 dBFS ceiling", juce::String (lp, 3) + " dBFS");
    }

    // Gate closes on quiet input and opens on loud input.
    allModulesOff (*p);
    setParam (*p, P::gateOn, 1);
    setParam (*p, P::gateThresh, -30);
    setParam (*p, P::gateFloor, -60);
    p->getStrip().reset();
    auto quiet = sine (48000.0, 300.0, juce::Decibels::decibelsToGain (-45.0f), 48000);
    auto qo = render (*p, quiet, 256);
    const float att = peakDb (qo, 36000) - (-45.0f);
    check (att < -55.0f, "gate attenuates signal below threshold by the floor", juce::String (att, 1) + " dB");
    auto loudS = sine (48000.0, 300.0, juce::Decibels::decibelsToGain (-10.0f), 48000);
    auto lo2 = render (*p, loudS, 256);
    check (std::abs (peakDb (lo2, 24000) + 10.0f) < 0.05f, "gate passes signal above threshold untouched");

    // De-esser: idle de-esser is exactly transparent, and it reduces a loud sibilant band.
    allModulesOff (*p);
    setParam (*p, P::dsOn, 1);
    setParam (*p, P::ds1Thresh, 0);
    setParam (*p, P::dsRelative, 0);
    p->getStrip().reset();
    auto low = sine (48000.0, 200.0, 0.1f, 24000);
    auto dso = render (*p, low, 256);
    float dsDiff = 0.0f;
    for (int i = 0; i < 24000; ++i) dsDiff = std::max (dsDiff, std::abs (dso.getSample (0, i) - low.getSample (0, i)));
    check (dsDiff < 1.0e-6f, "idle de-esser is transparent (parallel dynamic EQ)", "max diff " + juce::String (dsDiff, 8));
    setParam (*p, P::ds1Thresh, -30);
    setParam (*p, P::ds1Range, 12);
    p->getStrip().reset();
    auto ess = sine (48000.0, 8000.0, juce::Decibels::decibelsToGain (-6.0f), 48000);
    auto eo = render (*p, ess, 256);
    const float red = peakDb (eo, 24000) + 6.0f;
    check (red < -6.0f && red > -12.5f, "de-esser reduces 8 kHz within its 12 dB range", juce::String (red, 2) + " dB");
}

void testMetering()
{
    std::puts ("Loudness and true peak (BS.1770 / EBU Tech 3341)");
    LoudnessMeter m;
    m.prepare (48000.0);
    const auto& s1 = m.kStage (0);
    const auto& s2 = m.kStage (1);
    const bool coefOk = std::abs (s1.b0 - 1.53512485958697) < 1e-6 && std::abs (s1.b1 + 2.69169618940638) < 1e-6 && std::abs (s1.b2 - 1.19839281085285) < 1e-6
                        && std::abs (s1.a1 + 1.69065929318241) < 1e-6 && std::abs (s1.a2 - 0.73248077421585) < 1e-6
                        && std::abs (s2.a1 + 1.99004745483398) < 1e-6 && std::abs (s2.a2 - 0.99007225036621) < 1e-6;
    check (coefOk, "K-weighting coefficients match BS.1770 at 48 kHz");

    std::vector<StereoSample> buf (48000 * 20);
    for (size_t i = 0; i < buf.size(); ++i)
    {
        const float v = juce::Decibels::decibelsToGain (-23.0f) * (float) std::sin (2.0 * kPiD * 1000.0 * (double) i / 48000.0);
        buf[i] = { v, v };
    }
    m.process (buf.data(), buf.size(), 2);
    check (std::abs (m.getIntegrated() + 23.0) < 0.1, "1 kHz -23 dBFS stereo reads -23.0 LUFS integrated", juce::String (m.getIntegrated(), 2) + " LUFS");
    check (std::abs (m.getShortTerm() + 23.0) < 0.1 && std::abs (m.getMomentary() + 23.0) < 0.1, "momentary and short-term agree",
           juce::String (m.getMomentary(), 2) + " / " + juce::String (m.getShortTerm(), 2));

    LoudnessMeter t;
    t.prepare (48000.0);
    std::vector<StereoSample> tp (48000);
    for (size_t i = 0; i < tp.size(); ++i)
    {
        const float v = (float) std::sin (2.0 * kPiD * 12000.0 * (double) i / 48000.0 + kPiD / 4.0);
        tp[i] = { v, v };
    }
    t.process (tp.data(), tp.size(), 2);
    const float tpd = t.getTruePeakDb();
    check (tpd > -0.4f && tpd < 0.2f, "fs/4 sine at 45 deg (sample peak -3 dB) reads 0 dBTP (+0.2/-0.4)", juce::String (tpd, 2) + " dBTP");
}

void testBlockSizeIndependence()
{
    std::puts ("Determinism");
    juce::Random rng (5);
    juce::AudioBuffer<float> in (2, 48000 * 2);
    fillNoise (in, rng, 0.4f);
    std::vector<juce::AudioBuffer<float>> outs;
    for (int block : { 64, 37, 512, 1 })
    {
        auto p = makeProc (48000.0, 512);
        configureEverything (*p, false, false);
        setParam (*p, P::limOn, 0);
        p->getStrip().reset();
        outs.push_back (render (*p, in, block));
    }
    float maxDiff = 0.0f;
    for (size_t k = 1; k < outs.size(); ++k)
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < in.getNumSamples(); ++i) maxDiff = std::max (maxDiff, std::abs (outs[k].getSample (c, i) - outs[0].getSample (c, i)));
    check (maxDiff < 1.0e-5f, "output is independent of host block size (64 / 37 / 512 / 1)", "max diff " + juce::String (maxDiff, 8));
}

void testRealtimeSafety()
{
    std::puts ("Real-time safety");
    auto p = makeProc (48000.0, 256, true);
    configureEverything (*p, true, true);
    setParam (*p, P::compExt, 1);
    setParam (*p, P::gateExt, 1);
    setParam (*p, P::dsExt, 1);
    setParam (*p, P::compMode, 2);
    setParam (*p, P::eqMode, 1);
    p->getStrip().setSampleStreamingEnabled (true);
    juce::Random rng (9);
    juce::AudioBuffer<float> work (4, 256);
    juce::MidiBuffer midi;
    fillNoise (work, rng, 0.5f);
    p->processBlock (work, midi); // warm-up
    allocationCount = 0;
    countAllocations = true;
    for (int i = 0; i < 400; ++i)
    {
        fillNoise (work, rng, 0.5f);
        if (i % 50 == 0) setParam (*p, P::compType, (float) ((i / 50) % 4));
        if (i % 70 == 0) setParam (*p, P::hpSlope, (float) ((i / 70) % 4));
        if (i % 90 == 0) setParam (*p, P::satType, (float) ((i / 90) % 4));
        p->processBlock (work, midi);
    }
    countAllocations = false;
    check (allocationCount.load() == 0, "no heap allocation on the audio thread (all modules, HQ, lookahead, sidechain, automation)",
           juce::String (allocationCount.load()) + " allocations");
    p->getStrip().setSampleStreamingEnabled (false);

    // Randomised automation stress (every parameter except the limiter's), twice: with the limiter
    // off, the output is bounded by the gain structure; with the limiter in Brickwall, by its ceiling.
    for (const bool brickwall : { false, true })
    {
        auto q = makeProc (44100.0, 128);
        const auto plainOf = [&] (int index) { auto* prm = q->param (index); return prm->convertFrom0to1 (prm->getValue()); };
        if (brickwall)
        {
            setParam (*q, P::limOn, 1);
            setParam (*q, P::limMode, 1);
            q->getStrip().reset();
        }
        juce::AudioBuffer<float> buf (2, 128);
        bool finite = true;
        float worstMarginDb = -300.0f, mx = 0.0f;
        std::array<float, 400> bounds {}; // per-block bound over the last ~1.2 s (gain smoothers lag)
        bounds.fill (-300.0f);
        for (int b = 0; b < 3000; ++b)
        {
            if (b % 8 == 0)
                for (int k = 0; k < 6; ++k)
                {
                    const int idx = rng.nextInt (P::count);
                    if (idx == P::limOn || idx == P::limMode || idx == P::limCeiling || idx == P::limRelease) continue;
                    q->param (idx)->setValueNotifyingHost (rng.nextFloat());
                }
            const float amp = rng.nextFloat() < 0.1f ? 4.0f : 0.5f;
            fillNoise (buf, rng, amp);

            // Gain structure at the current settings: input and output gain, level match (clamped to
            // +12 dB), the boost of each EQ band, compressor output and de-esser output gain, plus
            // 12 dB for the automatic compensation of saturation and compressor make-up.
            float gainDb = std::max (0.0f, plainOf (P::inGain)) + std::max (0.0f, plainOf (P::outGain)) + 12.0f;
            if (plainOf (P::matchMode) > 0.5f) gainDb += 12.0f;
            for (int g : { (int) P::eqLowGain, (int) P::eqMidGain, (int) P::eqToneGain, (int) P::eqHighGain }) gainDb += std::max (0.0f, plainOf (g));
            gainDb += std::max (0.0f, plainOf (P::compOut)) + std::max (0.0f, plainOf (P::dsOut));
            bounds[(size_t) (b % (int) bounds.size())] = juce::Decibels::gainToDecibels (amp) + gainDb;
            const float boundDb = brickwall ? plainOf (P::limCeiling) + 0.01f : *std::max_element (bounds.begin(), bounds.end());

            q->processBlock (buf, midi);
            float blockPeak = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 128; ++i)
                {
                    finite &= std::isfinite (buf.getSample (c, i));
                    blockPeak = std::max (blockPeak, std::abs (buf.getSample (c, i)));
                }
            mx = std::max (mx, blockPeak);
            if (b > 20) worstMarginDb = std::max (worstMarginDb, juce::Decibels::gainToDecibels (blockPeak, -200.0f) - boundDb);
        }
        check (finite && worstMarginDb <= 0.0f,
               brickwall ? "randomised automation stress, Brickwall limiter: output never exceeds the ceiling"
                         : "randomised automation stress, limiter off: output stays within the gain-structure bound",
               "max |y| " + juce::String (juce::Decibels::gainToDecibels (mx, -200.0f), 1) + " dBFS, worst margin to the bound " + juce::String (worstMarginDb, 1) + " dB");
    }
}

void testStateAndUndo()
{
    std::puts ("State, undo, A/B");
    auto p = makeProc();
    setParam (*p, P::compThresh, -33);
    setParam (*p, P::eqToneGain, 4.5f);
    p->setOrder ({ modEq, modComp, modPre, modGate, modDs });
    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    auto q = makeProc();
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    const bool same = std::abs (q->param (P::compThresh)->convertFrom0to1 (q->param (P::compThresh)->getValue()) + 33.0f) < 0.01f
                      && std::abs (q->param (P::eqToneGain)->convertFrom0to1 (q->param (P::eqToneGain)->getValue()) - 4.5f) < 0.01f
                      && q->getOrder() == ChannelStrip::Order { modEq, modComp, modPre, modGate, modDs };
    check (same, "state round-trip restores parameters and module order");

    auto r = makeProc();
    r->pushUndoSnapshot();
    setParam (*r, P::compThresh, -10);
    r->pushUndoSnapshot();
    r->undo();
    const float t = r->param (P::compThresh)->convertFrom0to1 (r->param (P::compThresh)->getValue());
    check (std::abs (t + 18.0f) < 0.01f, "undo restores the previous value", juce::String (t, 2));
    r->redo();
    check (std::abs (r->param (P::compThresh)->convertFrom0to1 (r->param (P::compThresh)->getValue()) + 10.0f) < 0.01f, "redo re-applies it");

    r->selectSlot (1);
    setParam (*r, P::compThresh, -40);
    r->selectSlot (0);
    const float a = r->param (P::compThresh)->convertFrom0to1 (r->param (P::compThresh)->getValue());
    r->selectSlot (1);
    const float b = r->param (P::compThresh)->convertFrom0to1 (r->param (P::compThresh)->getValue());
    check (std::abs (a + 10.0f) < 0.01f && std::abs (b + 40.0f) < 0.01f, "A/B slots keep independent settings");

    bool presetsOk = true;
    for (int i = 0; i < (int) factoryPresets().size(); ++i)
    {
        r->loadFactoryPreset (i);
        presetsOk &= r->getPresetName() == factoryPresets()[(size_t) i].name;
    }
    check (presetsOk, "all " + juce::String ((int) factoryPresets().size()) + " factory presets load");
}
} // namespace

int runTests (const juce::StringArray& args)
{
    const juce::String filter = args.size() > 1 ? args[1] : juce::String();
    std::puts ("Northline Channel test suite\n");

    std::vector<TestCase> all {
        { "core_fastMath", testFastMath },
        { "core_params", testParams },
        { "core_nullAndLatency", testNullAndLatency },
        { "core_filters", testFilters },
        { "core_saturation", testSaturation },
        { "core_dynamics", testDynamics },
        { "core_metering", testMetering },
        { "core_blockSizeIndependence", testBlockSizeIndependence },
        { "core_realtimeSafety", testRealtimeSafety },
        { "core_stateAndUndo", testStateAndUndo },
    };
    auto reg = testRegistry();
    std::sort (reg.begin(), reg.end(), [] (const TestCase& x, const TestCase& y) { return juce::String (x.name) < juce::String (y.name); });
    all.insert (all.end(), reg.begin(), reg.end());

    int ran = 0;
    for (auto& t : all)
    {
        if (filter.isNotEmpty() && ! juce::String (t.name).containsIgnoreCase (filter)) continue;
        std::printf ("== %s\n", t.name);
        t.fn();
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failed\n", ran, checks, failures);
    return failures == 0 && ran > 0 ? 0 : 1;
}
} // namespace tool

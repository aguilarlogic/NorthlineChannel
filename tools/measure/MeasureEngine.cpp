#include "Measure.h"
#include <algorithm>
#include <functional>

// Suite `engine`: latency per configuration, Level Match accuracy, block-size determinism and
// the CPU cost of the strip stages. Thresholds live in tools/tests/TestEngine.cpp.

using namespace tool;

namespace
{
constexpr double kFs = 48000.0;

float plain (NorthlineProcessor& p, int index)
{
    auto* prm = p.param (index);
    return prm->convertFrom0to1 (prm->getValue());
}

float maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float m = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i) m = std::max (m, std::abs (a.getSample (c, i) - b.getSample (c, i)));
    return m;
}

int measuredDelay (NorthlineProcessor& p)
{
    p.getStrip().reset();
    juce::AudioBuffer<float> imp (2, 12000);
    imp.clear();
    imp.setSample (0, 500, 0.25f);
    imp.setSample (1, 500, 0.25f);
    auto o = render (p, imp, 128);
    int arg = 0;
    float m = 0.0f;
    for (int i = 0; i < o.getNumSamples(); ++i)
        if (std::abs (o.getSample (0, i)) > m) { m = std::abs (o.getSample (0, i)); arg = i; }
    return arg - 500;
}

double integratedLufs (const juce::AudioBuffer<float>& b, int from, int to)
{
    LoudnessMeter m;
    m.prepare (kFs);
    std::vector<StereoSample> v ((size_t) (to - from));
    for (int i = from; i < to; ++i) v[(size_t) (i - from)] = { b.getSample (0, i), b.getSample (1, i) };
    m.process (v.data(), v.size(), 2);
    return m.getIntegrated();
}

juce::AudioBuffer<float> stereoPink (int n, float rmsDb, int seed)
{
    auto a = pinkNoise (n, rmsDb, seed), b = pinkNoise (n, rmsDb, seed + 100);
    juce::AudioBuffer<float> out (2, n);
    out.copyFrom (0, 0, a, 0, 0, n);
    out.copyFrom (1, 0, b, 0, 0, n);
    return out;
}

std::vector<int> continuousParams()
{
    std::vector<int> out;
    for (int i = 0; i < P::count; ++i)
        if (paramDef (i).kind == ParamDef::Float && isAutomatable (i) && ! isTransient (i)) out.push_back (i);
    return out;
}

// Random automation every 2048 samples; gain-like dB parameters limited to +/-limitDb.
std::vector<AutomationEvent> automation (int length, const std::vector<int>& eligible, uint32_t seed, int perEvent, float limitDb)
{
    juce::Random rng ((juce::int64) seed);
    std::vector<AutomationEvent> ev;
    for (int t = 2048; t < length; t += 2048)
        for (int k = 0; k < perEvent; ++k)
        {
            const int idx = eligible[(size_t) rng.nextInt ((int) eligible.size())];
            const auto& d = paramDef (idx);
            float lo = d.minV, hi = d.maxV;
            if (d.unit == Unit::Db && lo < 0.0f && hi > 0.0f) { lo = std::max (lo, -limitDb); hi = std::min (hi, limitDb); }
            ev.push_back ({ t, idx, lo + (hi - lo) * rng.nextFloat() });
        }
    return ev;
}

//==============================================================================
void latencyTable (MeasureReport& r)
{
    r.h2 ("Latency per configuration (48 kHz)");
    r.para ("Reported = what the plug-in tells the host after `updateLatency()`. Measured = position of the peak of a 0.25 impulse through the whole processor. "
            "They must be equal, and the module on/off state must not change them. Rows with all modules on use settings that are transparent "
            "(no filtering, no gain change), so the peak position is a pure delay. A row that applies real filtering would show the filters' own group delay "
            "(for example the minimum-phase IIR half-band of the HQ oversampler, which PRE does not report yet).");
    r.table ({ "Configuration", "Reported (samples)", "Measured (samples)", "Equal" });

    const auto transparentOn = [] (NorthlineProcessor& p)
    {
        for (int on : { (int) P::preOn, (int) P::gateOn, (int) P::dsOn, (int) P::eqOn, (int) P::compOn }) setParam (p, on, 1);
        setParam (p, P::gateFloor, 0); setParam (p, P::gateThresh, -80);
        setParam (p, P::ds1Thresh, 0); setParam (p, P::dsRelative, 0);
        setParam (p, P::compRatio, 1); setParam (p, P::compThresh, 0);
    };
    struct Config { const char* name; std::function<void (NorthlineProcessor&)> setup; };
    const std::vector<Config> configs {
        { "default", [] (NorthlineProcessor&) {} },
        { "all modules on (transparent settings), limiter off", transparentOn },
        { "all modules on (transparent settings), limiter on (Safety Clip)", [transparentOn] (NorthlineProcessor& p) { transparentOn (p); setParam (p, P::limOn, 1); } },
        { "limiter off, Brickwall", [] (NorthlineProcessor& p) { setParam (p, P::limMode, 1); } },
        { "limiter on, Brickwall", [] (NorthlineProcessor& p) { setParam (p, P::limOn, 1); setParam (p, P::limMode, 1); } },
        { "limiter on, True Peak", [] (NorthlineProcessor& p) { setParam (p, P::limOn, 1); setParam (p, P::limMode, 2); } },
        { "quality HQ, all modules on (transparent settings)", [transparentOn] (NorthlineProcessor& p) { transparentOn (p); setParam (p, P::quality, 1); } },
        { "gate lookahead 2 ms, gate off", [] (NorthlineProcessor& p) { setParam (p, P::gateLookahead, 2); } },
        { "gate lookahead 2 ms, gate on", [] (NorthlineProcessor& p) { setParam (p, P::gateLookahead, 2); setParam (p, P::gateOn, 1); } },
        { "comp lookahead 5 ms, comp off", [] (NorthlineProcessor& p) { setParam (p, P::compLookahead, 3); } },
        { "comp lookahead 5 ms, comp on", [] (NorthlineProcessor& p) { setParam (p, P::compLookahead, 3); setParam (p, P::compOn, 1); } },
        { "de-esser lookahead 2 ms, de-esser on", [] (NorthlineProcessor& p) { setParam (p, P::dsLookahead, 2); setParam (p, P::dsOn, 1); } },
        { "test latency 11/37/64/0/100 on the five modules (alignment check)", [] (NorthlineProcessor& p)
          {
              const int lat[numModules] { 11, 37, 64, 0, 100 };
              for (int m = 0; m < numModules; ++m) p.getStrip().setTestModuleLatency (m, lat[m]);
          } },
    };
    for (auto& cfg : configs)
    {
        auto p = makeProc (kFs, 128);
        cfg.setup (*p);
        p->getStrip().reset();
        p->updateLatency();
        const int reported = p->getLatencySamples(), measured = measuredDelay (*p);
        r.row ({ cfg.name, juce::String (reported), juce::String (measured), reported == measured ? "yes" : "NO" });
    }
    r.endTable();
}

//==============================================================================
void levelMatchTable (MeasureReport& r)
{
    r.h2 ("Level Match accuracy");
    r.para ("Learn: pink noise at -20 dBFS RMS, 16 s. Truth = difference of BS.1770 integrated loudness (input minus chain output) over the blocks Learn used. "
            "Error must stay within 0.3 LU.");
    r.table ({ "Scenario", "Learn time (s)", "Learned trim (dB)", "Measured (LU)", "Error (LU)" });
    const auto in = stereoPink ((int) (16 * kFs), -20.0f, 1);

    struct Scenario { const char* name; std::function<void (NorthlineProcessor&)> setup; };
    const std::vector<Scenario> scenarios {
        { "low +6 / high -6 dB tilt", [] (NorthlineProcessor& p)
          { allModulesOff (p); setParam (p, P::eqOn, 1); setParam (p, P::eqLowType, 0); setParam (p, P::eqLowFreq, 200); setParam (p, P::eqLowGain, 6);
            setParam (p, P::eqHighType, 0); setParam (p, P::eqHighFreq, 2000); setParam (p, P::eqHighGain, -6); } },
        { "low -6 / high +6 dB tilt", [] (NorthlineProcessor& p)
          { allModulesOff (p); setParam (p, P::eqOn, 1); setParam (p, P::eqLowType, 0); setParam (p, P::eqLowFreq, 200); setParam (p, P::eqLowGain, -6);
            setParam (p, P::eqHighType, 0); setParam (p, P::eqHighFreq, 2000); setParam (p, P::eqHighGain, 6); } },
        { "compression (VCA, -24 dB, 3:1)", [] (NorthlineProcessor& p)
          { allModulesOff (p); setParam (p, P::compOn, 1); setParam (p, P::compType, 0); setParam (p, P::compThresh, -24); setParam (p, P::compRatio, 3);
            setParam (p, P::compAttack, 5); setParam (p, P::compRelease, 100); } },
        { "everything on", [] (NorthlineProcessor& p) { configureEverything (p, false, false); setParam (p, P::limOn, 0); } },
    };
    for (auto& s : scenarios)
    {
        auto learn = makeProc (kFs, 480);
        s.setup (*learn);
        setParam (*learn, P::matchMode, 1);
        learn->getStrip().reset();
        int learnedAt = -1;
        {
            juce::AudioBuffer<float> work (2, 480);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 480 <= in.getNumSamples(); pos += 480)
            {
                for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, c, pos, 480);
                learn->processBlock (work, midi);
                if (learnedAt < 0 && learn->getLevelMatchStatus().learnedSerial != 0) learnedAt = pos + 480;
            }
        }
        learn->servicePendingWork();
        const float trim = plain (*learn, P::matchTrim);

        auto chainProc = makeProc (kFs, 480);
        s.setup (*chainProc);
        chainProc->getStrip().reset();
        const auto chain = render (*chainProc, in, 480);
        const int to = learnedAt > 0 ? learnedAt : in.getNumSamples();
        const double truth = integratedLufs (in, (int) (0.4 * kFs), to) - integratedLufs (chain, (int) (0.4 * kFs), to);
        r.row ({ s.name, learnedAt > 0 ? fmt (learnedAt / kFs, 1) : "-", fmt (trim, 2), fmt (truth, 2), fmt (trim - truth, 2) });
    }
    r.endTable();

    r.para ("Live: 1 kHz tone at -20 dBFS for 12 s, 15 s of gated noise, tone again for 3 s. EQ bell at 1 kHz as the chain. "
            "Re-entry error = peak of the first second after re-entry (without the first 50 ms, which are the EQ's own onset) minus the peak before the gap. "
            "Gap drift = noise level at 11-13 s into the gap minus 2-4 s into the gap. Old AGC: re-entry +3.9 dBFS, gated noise +24 dB.");
    r.table ({ "Chain", "Gap noise (dBFS RMS)", "Gain before gap (dB)", "Re-entry error (dB)", "Gap drift (dB)" });
    struct Case { float dipDb, gapDb; };
    for (auto c : { Case { 0.0f, -50.0f }, Case { -6.0f, -50.0f }, Case { -6.0f, -75.0f }, Case { 6.0f, -50.0f } })
    {
        const int seg1 = (int) (12 * kFs), gap = (int) (15 * kFs), seg3 = (int) (3 * kFs);
        juce::AudioBuffer<float> in2 (2, seg1 + gap + seg3);
        juce::Random rng (21);
        const float toneAmp = juce::Decibels::decibelsToGain (-20.0f), noiseAmp = juce::Decibels::decibelsToGain (c.gapDb) * std::sqrt (3.0f);
        for (int i = 0; i < in2.getNumSamples(); ++i)
        {
            const bool tone = i < seg1 || i >= seg1 + gap;
            const float v = tone ? toneAmp * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs) : noiseAmp * (rng.nextFloat() * 2.0f - 1.0f);
            in2.setSample (0, i, v);
            in2.setSample (1, i, tone ? v : noiseAmp * (rng.nextFloat() * 2.0f - 1.0f));
        }
        auto p = makeProc (kFs, 480);
        allModulesOff (*p);
        setParam (*p, P::eqOn, 1); setParam (*p, P::eqMidType, 2); setParam (*p, P::eqMidFreq, 1000); setParam (*p, P::eqMidQ, 1); setParam (*p, P::eqMidGain, c.dipDb);
        setParam (*p, P::matchMode, 2);
        p->getStrip().reset();
        juce::AudioBuffer<float> out (2, in2.getNumSamples()), work (2, 480);
        juce::MidiBuffer midi;
        float gainBefore = 0.0f;
        for (int pos = 0; pos + 480 <= in2.getNumSamples(); pos += 480)
        {
            for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, in2, ch, pos, 480);
            p->processBlock (work, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, work, ch, 0, 480);
            if (pos + 480 == seg1) gainBefore = p->getLevelMatchStatus().gainDb;
        }
        auto peak = [&] (int a, int b) { float m = 0; for (int i = a; i < b; ++i) m = std::max (m, std::abs (out.getSample (0, i))); return juce::Decibels::gainToDecibels (m, -200.0f); };
        const float err = peak (seg1 + gap + (int) (0.05 * kFs), seg1 + gap + (int) kFs) - peak (seg1 - (int) (1.5 * kFs), seg1);
        const float drift = rmsDbRange (out, seg1 + (int) (11 * kFs), seg1 + (int) (13 * kFs)) - rmsDbRange (out, seg1 + (int) (2 * kFs), seg1 + (int) (4 * kFs));
        r.row ({ c.dipDb == 0.0f ? "transparent" : "EQ bell " + fmt (c.dipDb, 0) + " dB", fmt (c.gapDb, 0), fmt (gainBefore, 2), fmt (err, 3), fmt (drift, 3) });
    }
    r.endTable();
}

//==============================================================================
void determinismTable (MeasureReport& r, bool quick)
{
    r.h2 ("Determinism: output difference against block size 2048");
    r.para ("3 s of noise, random automation of the continuous parameters of the listed part every 2048 samples (sample-exact, "
            "`renderWithAutomationExact`), gain-like parameters limited to +/-4 dB. Maximum absolute sample difference per host block size; "
            "the bound is 1e-6. Rows other than the strip gains show what the module internals contribute (the strip does not own them).");
    const std::vector<int> blocks { 1, 7, 32, 37, 64, 511, 512 };
    juce::StringArray header;
    header.add ("Part");
    for (int b : blocks) header.add ("block " + juce::String (b));
    r.text << "| " << header.joinIntoString (" | ") << " |\n|";
    for (int i = 0; i < header.size(); ++i) r.text << "---|";
    r.text << "\n";

    juce::Random rng (41);
    juce::AudioBuffer<float> in (2, quick ? 48000 : 3 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);

    struct Part { const char* name; std::function<void (NorthlineProcessor&)> setup; std::function<bool (int)> eligible; };
    const auto inRange = [] (int a, int b) { return [a, b] (int i) { return i >= a && i <= b; }; };
    const std::vector<Part> parts {
        { "strip gains + Level Match Live (modules off)", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::eqOn, 1); setParam (p, P::eqMidType, 2);
              setParam (p, P::eqMidFreq, 1000); setParam (p, P::eqMidQ, 0.3f); setParam (p, P::eqMidGain, -8); setParam (p, P::matchMode, 2); },
          [] (int i) { return i == P::inGain || i == P::outGain || i == P::globalMix || i == P::matchTrim; } },
        { "PRE", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::preOn, 1); setParam (p, P::satOn, 1); setParam (p, P::hpOn, 1); setParam (p, P::lpOn, 1); setParam (p, P::weightOn, 1); },
          inRange (P::satDrive, P::weightTrack) },
        { "GATE", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::gateOn, 1); setParam (p, P::gateThresh, -20); }, inRange (P::gateThresh, P::gateScLp) },
        { "DE-ESS", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::dsOn, 1); setParam (p, P::ds2On, 1); }, inRange (P::dsAttack, P::ds2Range) },
        { "EQ", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::eqOn, 1); }, inRange (P::eqLowFreq, P::eqHighDynSpeed) },
        { "COMP", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::compOn, 1); setParam (p, P::compThresh, -30); }, inRange (P::compThresh, P::compScBellGain) },
        { "limiter (Safety Clip)", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::limOn, 1); setParam (p, P::inGain, 12); }, [] (int i) { return i == P::limCeiling || i == P::limRelease; } },
        { "everything on (all continuous parameters)", [] (NorthlineProcessor& p) { configureEverything (p, false, false); setParam (p, P::matchMode, 2); }, [] (int) { return true; } },
    };
    const auto continuous = continuousParams();
    for (auto& part : parts)
    {
        std::vector<int> eligible;
        for (int i : continuous) if (part.eligible (i)) eligible.push_back (i);
        const auto ev = automation (in.getNumSamples(), eligible, 5, 6, 4.0f);

        auto reference = [&]
        {
            auto p = makeProc (kFs, 2048);
            part.setup (*p);
            p->getStrip().reset();
            return renderWithAutomationExact (*p, in, 2048, ev);
        }();
        juce::StringArray row;
        row.add (part.name);
        for (int b : blocks)
        {
            auto p = makeProc (kFs, 2048);
            part.setup (*p);
            p->getStrip().reset();
            const float d = maxAbsDiff (renderWithAutomationExact (*p, in, b, ev), reference);
            row.add (d == 0.0f ? "0" : juce::String (d, 2, true));
        }
        r.text << "| " << row.joinIntoString (" | ") << " |\n";
    }
    r.endTable();
}

//==============================================================================
void cpuTable (MeasureReport& r, bool quick)
{
    r.h2 ("CPU cost of the strip stages (48 kHz, 64-sample blocks, stereo)");
    r.para ("Processing time of `processBlock` with every module off unless stated, mean and 99th percentile per block. "
            "Single run on a shared machine: noisy, use differences between rows only as an order of magnitude.");
    r.table ({ "Configuration", "Mean (us/block)", "p99 (us/block)", "Real-time CPU (%)" });

    const double seconds = quick ? 1.5 : 4.0;
    const int block = 64;
    juce::Random rng (3);
    juce::AudioBuffer<float> src (2, (int) kFs);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < src.getNumSamples(); ++i) src.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.2f);

    struct Config { const char* name; std::function<void (NorthlineProcessor&)> setup; };
    const std::vector<Config> configs {
        { "modules off, nothing engaged (unity path)", [] (NorthlineProcessor& p) { allModulesOff (p); } },
        { "polarity + swap + input/output gain", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::inPolarity, 3); setParam (p, P::inSwap, 1); setParam (p, P::inGain, 2); setParam (p, P::outGain, -2); } },
        { "global mix 50 %", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::globalMix, 50); } },
        { "Level Match Live (K-weighted measurement)", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::matchMode, 2); } },
        { "Level Match Learn, learned (measurement stopped)", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::matchMode, 1); setParam (p, P::matchTrim, 3); p.getStrip().holdLevelMatch(); } },
        { "monitor Side", [] (NorthlineProcessor& p) { allModulesOff (p); setParam (p, P::monitor, 4); } },
        { "latency alignment (five modules with 20-100 samples, all off)", [] (NorthlineProcessor& p)
          { allModulesOff (p); for (int m = 0; m < numModules; ++m) p.getStrip().setTestModuleLatency (m, 20 + 20 * m); } },
        { "everything on (Eco, zero latency)", [] (NorthlineProcessor& p) { configureEverything (p, false, false); } },
        { "everything on + Live match + mix 80 %", [] (NorthlineProcessor& p) { configureEverything (p, false, false); setParam (p, P::matchMode, 2); setParam (p, P::globalMix, 80); } },
    };
    for (auto& cfg : configs)
    {
        auto p = makeProc (kFs, block);
        cfg.setup (*p);
        p->getStrip().reset();
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        const int blocks = (int) (seconds * kFs / block), warm = (int) (0.5 * kFs / block);
        std::vector<double> times;
        times.reserve ((size_t) blocks);
        const double tickUs = 1.0e6 / (double) juce::Time::getHighResolutionTicksPerSecond();
        int pos = 0;
        for (int b = 0; b < blocks + warm; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i) buf.setSample (c, i, src.getSample (c, (pos + i) % src.getNumSamples()));
            pos = (pos + block) % src.getNumSamples();
            const auto t0 = juce::Time::getHighResolutionTicks();
            p->processBlock (buf, midi);
            const auto t1 = juce::Time::getHighResolutionTicks();
            if (b >= warm) times.push_back ((double) (t1 - t0) * tickUs);
        }
        double sum = 0.0;
        for (double t : times) sum += t;
        std::sort (times.begin(), times.end());
        const double mean = sum / (double) times.size(), p99 = times[(size_t) (0.99 * (double) (times.size() - 1))];
        r.row ({ cfg.name, fmt (mean, 2), fmt (p99, 2), fmt (100.0 * mean / (1.0e6 * block / kFs), 3) });
    }
    r.endTable();
}
} // namespace

NL_MEASURE (engine, "Latency per configuration, Level Match accuracy, block-size determinism, CPU of the strip stages")
{
    r.para ("Strip-level behaviour of the engine (roadmap D1, D3, 7.1). Latency, Level Match accuracy and determinism numbers are checked against thresholds by `NorthlineTool test engine`.");
    latencyTable (r);
    levelMatchTable (r);
    determinismTable (r, quick);
    cpuTable (r, quick);
}

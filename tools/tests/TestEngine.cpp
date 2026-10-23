#include "../TestUtil.h"
#include <atomic>
#include <functional>
#include <thread>

// Engine tests: Level Match v2 (D1), latency alignment, strip-level determinism (D3), global
// features (roadmap 7.1), dirty-flag parameter updates (10), thread-safe state (D7), transient
// parameters (D11), state schema v2 and the physical output bound (D8/D15).

using namespace tool;

namespace
{
constexpr double kFs = 48000.0;

float plain (NorthlineProcessor& p, int index)
{
    auto* prm = p.param (index);
    return prm->convertFrom0to1 (prm->getValue());
}

float noiseAmpForRms (float rmsDb) { return juce::Decibels::decibelsToGain (rmsDb) * std::sqrt (3.0f); } // uniform noise

juce::AudioBuffer<float> stereoPink (int n, float rmsDb, int seed = 1)
{
    auto a = pinkNoise (n, rmsDb, seed);
    auto b = pinkNoise (n, rmsDb, seed + 100);
    juce::AudioBuffer<float> out (2, n);
    out.copyFrom (0, 0, a, 0, 0, n);
    out.copyFrom (1, 0, b, 0, 0, n);
    return out;
}

// Like render(), calling onBlock (samplesDone) after every block.
juce::AudioBuffer<float> renderWithCallback (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                             const std::function<void (int)>& onBlock)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    juce::AudioBuffer<float> work (juce::jmax (2, p.getTotalNumInputChannels()), block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        work.setSize (work.getNumChannels(), len, false, false, true);
        work.clear();
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, len);
        p.processBlock (work, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, work, c, 0, len);
        onBlock (pos + len);
    }
    return out;
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

float maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = juce::jmin (a.getNumSamples(), b.getNumSamples());
    float m = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < to; ++i) m = std::max (m, std::abs (a.getSample (c, i) - b.getSample (c, i)));
    return m;
}

// Maximum |out[t] - in[t - delay]| for t >= from.
float maxDiffDelayed (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int delay, int from)
{
    float m = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = juce::jmax (from, delay); i < out.getNumSamples(); ++i)
            m = std::max (m, std::abs (out.getSample (c, i) - in.getSample (c, i - delay)));
    return m;
}

// Delay (in samples) at which a 0.25 impulse emerges; -1 if its amplitude is wrong.
int measuredDelay (NorthlineProcessor& p, int at = 500, float* peakOut = nullptr)
{
    p.getStrip().reset();
    juce::AudioBuffer<float> imp (2, 6000);
    imp.clear();
    imp.setSample (0, at, 0.25f);
    imp.setSample (1, at, 0.25f);
    auto o = render (p, imp, 128);
    int arg = 0;
    float m = 0.0f;
    for (int i = 0; i < o.getNumSamples(); ++i)
        if (std::abs (o.getSample (0, i)) > m) { m = std::abs (o.getSample (0, i)); arg = i; }
    if (peakOut != nullptr) *peakOut = m;
    return arg - at;
}

enum class Scenario { TiltDown, TiltUp, Compression };

void configureScenario (NorthlineProcessor& p, Scenario s)
{
    allModulesOff (p);
    if (s == Scenario::Compression)
    {
        setParam (p, P::compOn, 1);
        setParam (p, P::compType, 0);
        setParam (p, P::compThresh, -24);
        setParam (p, P::compRatio, 3);
        setParam (p, P::compAttack, 5);
        setParam (p, P::compRelease, 100);
        return;
    }
    const float lowGain = s == Scenario::TiltDown ? 6.0f : -6.0f;
    setParam (p, P::eqOn, 1);
    setParam (p, P::eqLowType, 0); setParam (p, P::eqLowFreq, 200); setParam (p, P::eqLowGain, lowGain);
    setParam (p, P::eqHighType, 0); setParam (p, P::eqHighFreq, 2000); setParam (p, P::eqHighGain, -lowGain);
}

struct LearnResult { float trim = 0; int learnedAt = -1; };

LearnResult runLearn (Scenario s, const juce::AudioBuffer<float>& in)
{
    auto p = makeProc (kFs, 480);
    configureScenario (*p, s);
    setParam (*p, P::matchMode, 1);
    p->getStrip().reset();
    LearnResult r;
    renderWithCallback (*p, in, 480, [&] (int done)
    {
        if (r.learnedAt < 0 && p->getLevelMatchStatus().learnedSerial != 0) r.learnedAt = done;
    });
    p->servicePendingWork();
    r.trim = plain (*p, P::matchTrim);
    return r;
}

//==============================================================================
struct ReentryResult { float steadyPeakDb, reentryPeakDb, gapEarlyDb, gapLateDb, gainBeforeGapDb; };

ReentryResult runReentry (float gapRmsDb, float eqDipDb)
{
    const int seg1 = (int) (12 * kFs), gap = (int) (15 * kFs), seg3 = (int) (3 * kFs);
    juce::AudioBuffer<float> in (2, seg1 + gap + seg3);
    in.clear();
    juce::Random rng (21);
    const float toneAmp = juce::Decibels::decibelsToGain (-20.0f), noiseAmp = noiseAmpForRms (gapRmsDb);
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const bool tone = i < seg1 || i >= seg1 + gap;
        const float v = tone ? toneAmp * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs) : noiseAmp * (rng.nextFloat() * 2.0f - 1.0f);
        in.setSample (0, i, v);
        in.setSample (1, i, tone ? v : noiseAmp * (rng.nextFloat() * 2.0f - 1.0f));
    }

    auto p = makeProc (kFs, 480);
    allModulesOff (*p);
    setParam (*p, P::eqOn, 1);
    setParam (*p, P::eqMidType, 2);
    setParam (*p, P::eqMidFreq, 1000);
    setParam (*p, P::eqMidQ, 1);
    setParam (*p, P::eqMidGain, eqDipDb);
    setParam (*p, P::matchMode, 2);
    p->getStrip().reset();
    float gainBefore = 0.0f;
    auto out = renderWithCallback (*p, in, 480, [&] (int done) { if (done == seg1) gainBefore = p->getLevelMatchStatus().gainDb; });

    ReentryResult r {};
    auto peakIn = [&] (int from, int to)
    {
        float m = 0.0f;
        for (int i = from; i < to; ++i) m = std::max (m, std::abs (out.getSample (0, i)));
        return juce::Decibels::gainToDecibels (m, -200.0f);
    };
    r.steadyPeakDb = peakIn (seg1 - (int) (1.5 * kFs), seg1);
    // The first 50 ms are the EQ's own onset transient (a cut passes the first cycles at full level).
    r.reentryPeakDb = peakIn (seg1 + gap + (int) (0.05 * kFs), seg1 + gap + (int) kFs);
    r.gapEarlyDb = rmsDbRange (out, seg1 + (int) (2 * kFs), seg1 + (int) (4 * kFs));
    r.gapLateDb = rmsDbRange (out, seg1 + (int) (11 * kFs), seg1 + (int) (13 * kFs));
    r.gainBeforeGapDb = gainBefore;
    return r;
}
} // namespace

//==============================================================================
// D1 Level Match v2
//==============================================================================
NL_TEST (engine_levelMatchOffIsUnity)
{
    auto p = makeProc();
    juce::Random rng (3);
    juce::AudioBuffer<float> in (2, 24000);
    fillNoise (in, rng, 0.4f);
    setParam (*p, P::matchTrim, 6.0f); // ignored while the mode is Off
    p->getStrip().reset();
    auto out = render (*p, in, 256);
    check (maxAbsDiff (out, in) == 0.0f, "match_mode Off is unity and ignores match_trim", "max diff " + juce::String (maxAbsDiff (out, in), 9));
    check (p->getLevelMatchStatus().state == LevelMatchStatus::Idle, "status is idle while Off");
}

NL_TEST (engine_levelMatchReentry)
{
    // A -20 dBFS tone re-enters after a 15 s gated gap. The old AGC pumped the gap up by 24 dB and
    // re-entered at +3.9 dBFS.
    struct Case { const char* name; float gapDb, dipDb; };
    for (const auto& c : { Case { "transparent chain, gap -50 dBFS (below the relative gate)", -50.0f, 0.0f },
                           Case { "-6 dB chain, gap -50 dBFS (below the relative gate)", -50.0f, -6.0f },
                           Case { "-6 dB chain, gap -75 dBFS (below the absolute gate)", -75.0f, -6.0f } })
    {
        const auto r = runReentry (c.gapDb, c.dipDb);
        const float err = r.reentryPeakDb - r.steadyPeakDb;
        check (std::abs (r.steadyPeakDb + 20.0f) < 0.5f, juce::String ("Live converges before the gap: ") + c.name,
               "peak " + juce::String (r.steadyPeakDb, 2) + " dBFS, gain " + juce::String (r.gainBeforeGapDb, 2) + " dB");
        check (std::abs (err) <= 0.3f, juce::String ("re-entry peak within 0.3 dB of steady state: ") + c.name,
               "error " + juce::String (err, 3) + " dB, re-entry peak " + juce::String (r.reentryPeakDb, 2) + " dBFS");
        check (std::abs (r.gapLateDb - r.gapEarlyDb) <= 0.5f, juce::String ("noise floor does not move during the gap: ") + c.name,
               "early " + juce::String (r.gapEarlyDb, 2) + ", late " + juce::String (r.gapLateDb, 2) + " dB");
    }
}

NL_TEST (engine_levelMatchLearnAccuracy)
{
    const auto in = stereoPink ((int) (16 * kFs), -20.0f);
    for (auto s : { Scenario::TiltDown, Scenario::TiltUp, Scenario::Compression })
    {
        const char* name = s == Scenario::TiltDown ? "low +6 / high -6 dB tilt" : (s == Scenario::TiltUp ? "low -6 / high +6 dB tilt" : "compression");
        const auto r = runLearn (s, in);

        auto p = makeProc (kFs, 480);
        configureScenario (*p, s);
        p->getStrip().reset();
        const auto chain = render (*p, in, 480);
        const int to = r.learnedAt > 0 ? r.learnedAt : in.getNumSamples();
        const double truth = integratedLufs (in, (int) (0.4 * kFs), to) - integratedLufs (chain, (int) (0.4 * kFs), to);

        check (r.learnedAt > 0 && r.learnedAt <= (int) (11 * kFs), juce::String ("Learn finishes within 10 s of signal: ") + name,
               "after " + juce::String (r.learnedAt / kFs, 1) + " s");
        check (std::abs (r.trim - truth) <= 0.3, juce::String ("Learn within 0.3 LU of the measured loudness difference: ") + name,
               "learned " + juce::String (r.trim, 2) + " dB, measured " + juce::String (truth, 2) + " LU");
        if (s == Scenario::Compression)
            check (std::abs (truth) > 3.0, "compression scenario removes more than 3 LU", juce::String (truth, 2) + " LU");
    }
}

NL_TEST (engine_levelMatchLearnNull)
{
    // After Learn the gain is static, so the output nulls against a plain output gain of the same size.
    const auto in = stereoPink ((int) (16 * kFs), -20.0f, 7);
    const auto r = runLearn (Scenario::TiltDown, in);

    auto a = makeProc (kFs, 480);
    configureScenario (*a, Scenario::TiltDown);
    setParam (*a, P::matchMode, 1);
    a->getStrip().reset();
    const auto learned = renderWithCallback (*a, in, 480, [&] (int) { a->servicePendingWork(); });
    const float trim = plain (*a, P::matchTrim);

    auto b = makeProc (kFs, 480);
    configureScenario (*b, Scenario::TiltDown);
    setParam (*b, P::outGain, trim);
    b->getStrip().reset();
    const auto reference = render (*b, in, 480);

    const int from = r.learnedAt + (int) (3 * kFs);
    const float diffDb = juce::Decibels::gainToDecibels (maxAbsDiff (learned, reference, from), -300.0f);
    check (std::abs (trim - r.trim) < 0.01f, "learned trim is mirrored into match_trim", juce::String (trim, 3) + " dB");
    check (diffDb < -90.0f, "Learn output nulls against a static gain", juce::String (diffDb, 1) + " dBFS residual");
}

NL_TEST (engine_levelMatchLiveSlew)
{
    // Live follows the loudness difference at no more than 1 dB/s and holds when nothing counts.
    const int n = (int) (14 * kFs);
    juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = juce::Decibels::decibelsToGain (-20.0f) * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs);
        in.setSample (0, i, v);
        in.setSample (1, i, v);
    }
    auto p = makeProc (kFs, 480);
    allModulesOff (*p);
    setParam (*p, P::eqOn, 1);
    setParam (*p, P::eqMidType, 2); setParam (*p, P::eqMidFreq, 1000); setParam (*p, P::eqMidQ, 1); setParam (*p, P::eqMidGain, -9);
    setParam (*p, P::matchMode, 2);
    p->getStrip().reset();
    std::vector<float> gains { 0.0f };
    renderWithCallback (*p, in, 480, [&] (int done)
    {
        if (done % 4800 == 0) gains.push_back (p->getLevelMatchStatus().gainDb);
    });
    float maxStep = 0.0f;
    for (size_t i = 1; i < gains.size(); ++i) maxStep = std::max (maxStep, std::abs (gains[i] - gains[i - 1]));
    check (maxStep <= 0.1f + 1.0e-4f, "Live gain moves at most 1 dB/s", "largest step per 100 ms " + juce::String (maxStep, 4) + " dB");
    check (std::abs (gains.back() - 9.0f) < 0.3f, "Live converges to the loudness difference (+9 dB)", juce::String (gains.back(), 2) + " dB");
    check (std::abs (p->getLevelMatchStatus().gainDb) <= 12.0f, "gain stays within +/-12 dB");
    check (p->getLevelMatchStatus().state == LevelMatchStatus::Live, "status reports live");
}

NL_TEST (engine_levelMatchLearnStateMachine)
{
    const auto in = stereoPink ((int) (14 * kFs), -20.0f, 11);
    auto p = makeProc (kFs, 480);
    configureScenario (*p, Scenario::TiltDown);
    setParam (*p, P::matchMode, 1);
    p->getStrip().reset();
    check (p->getLevelMatchStatus().state == LevelMatchStatus::Learning, "Learn starts in the learning state");

    float midProgress = -1.0f;
    int serialAt = -1;
    renderWithCallback (*p, in, 480, [&] (int done)
    {
        const auto st = p->getLevelMatchStatus();
        if (done == (int) (2 * kFs)) midProgress = st.progress;
        if (serialAt < 0 && st.learnedSerial != 0) serialAt = done;
    });
    p->servicePendingWork();
    const auto st = p->getLevelMatchStatus();
    check (midProgress > 0.3f && midProgress < 1.0f, "progress advances while learning", juce::String (midProgress, 2) + " after 2 s");
    check (st.state == LevelMatchStatus::Learned && st.progress == 1.0f, "learning completes and holds");
    const float learned = plain (*p, P::matchTrim);

    // A relearn restarts, and the same material gives the same answer.
    p->relearnLevelMatch();
    const auto afterRelearn = renderWithCallback (*p, in, 480, [&] (int) {});
    p->servicePendingWork();
    check (std::abs (plain (*p, P::matchTrim) - learned) < 0.15f, "relearn on the same material reproduces the trim",
           juce::String (plain (*p, P::matchTrim), 2) + " vs " + juce::String (learned, 2) + " dB");
    (void) afterRelearn;

    // A session saved in Learn mode is restored as it was, not learned again.
    setParam (*p, P::matchTrim, 4.0f);
    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    auto q = makeProc (kFs, 480);
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    q->getStrip().reset();
    const auto serialBefore = q->getLevelMatchStatus().learnedSerial;
    const auto qOut = render (*q, in, 480);
    q->servicePendingWork();
    auto r = makeProc (kFs, 480);
    configureScenario (*r, Scenario::TiltDown);
    setParam (*r, P::outGain, 4.0f);
    r->getStrip().reset();
    const auto rOut = render (*r, in, 480);
    check (q->getLevelMatchStatus().learnedSerial == serialBefore && std::abs (plain (*q, P::matchTrim) - 4.0f) < 0.001f,
           "restored Learn session keeps its trim (no new learning pass)", juce::String (plain (*q, P::matchTrim), 3) + " dB");
    check (maxAbsDiff (qOut, rOut, (int) kFs) < 1.0e-4f, "restored Learn session applies the stored trim",
           "max diff " + juce::String (maxAbsDiff (qOut, rOut, (int) kFs), 7));
}

//==============================================================================
// Latency manager and bypass alignment
//==============================================================================
namespace
{
void makeTransparentWhenOn (NorthlineProcessor& p)
{
    setParam (p, P::gateFloor, 0);
    setParam (p, P::gateThresh, -80);
    setParam (p, P::ds1Thresh, 0);
    setParam (p, P::dsRelative, 0);
    setParam (p, P::compRatio, 1);
    setParam (p, P::compThresh, 0);
}

constexpr std::array<int, numModules> kOnParams { P::preOn, P::gateOn, P::dsOn, P::eqOn, P::compOn };

void giveModulesLatency (NorthlineProcessor& p, const std::array<int, numModules>& lat)
{
    for (int m = 0; m < numModules; ++m) p.getStrip().setTestModuleLatency (m, lat[(size_t) m]);
}
} // namespace

NL_TEST (engine_latencyAlignmentImpulse)
{
    const std::array<int, numModules> lat { 11, 37, 64, 0, 100 }; // PRE, GATE, DS, EQ, COMP: 212 in total
    auto p = makeProc (kFs, 128);
    makeTransparentWhenOn (*p);
    giveModulesLatency (*p, lat);

    check (p->getStrip().getChainLatencySamples() == 212 && p->getStrip().getLatencySamples() == 212,
           "strip latency is the sum of the module latencies", juce::String (p->getStrip().getLatencySamples()));
    p->updateLatency();
    check (p->getLatencySamples() == 212, "latency reported to the host", juce::String (p->getLatencySamples()));

    int wrong = 0, wrongAmp = 0;
    for (int mask = 0; mask < (1 << numModules); ++mask)
    {
        for (int m = 0; m < numModules; ++m) setParam (*p, kOnParams[(size_t) m], (mask >> m) & 1 ? 1.0f : 0.0f);
        float peak = 0.0f;
        const int d = measuredDelay (*p, 500, &peak);
        wrong += d != 212;
        wrongAmp += std::abs (peak - 0.25f) > 1.0e-4f;
    }
    check (wrong == 0, "an impulse is delayed by exactly the strip latency for all 32 on/off combinations", juce::String (wrong) + " combinations off");
    check (wrongAmp == 0, "the impulse keeps its amplitude in every combination", juce::String (wrongAmp) + " combinations off");

    // The master section (limiter lookahead) adds to the same total, whatever its on/off state.
    setParam (*p, P::limMode, 1);
    for (float on : { 0.0f, 1.0f })
    {
        setParam (*p, P::limOn, on);
        p->getStrip().reset();
        p->updateLatency();
        const int reported = p->getLatencySamples();
        const int d = measuredDelay (*p);
        check (d == reported && reported >= 212, juce::String ("reported latency equals measured delay (Brickwall, limiter ") + (on > 0 ? "on" : "off") + ")",
               "reported " + juce::String (reported) + ", measured " + juce::String (d));
    }
}

NL_TEST (engine_latencyToggleNeverShifts)
{
    // Modules that are transparent when on: toggling any of them at random must leave the signal
    // exactly the input delayed by the strip latency, with no dip, jump or timing shift.
    const std::array<int, numModules> lat { 11, 37, 64, 0, 100 };
    auto p = makeProc (kFs, 100);
    makeTransparentWhenOn (*p);
    giveModulesLatency (*p, lat);
    allModulesOff (*p);
    p->getStrip().reset();

    juce::Random rng (17);
    juce::AudioBuffer<float> in (2, (int) (4 * kFs));
    fillNoise (in, rng, 0.3f);
    std::vector<AutomationEvent> ev;
    std::array<float, numModules> state {};
    for (int t = 300; t < in.getNumSamples(); t += 100 * (1 + rng.nextInt (12)))
    {
        const int m = rng.nextInt (numModules);
        state[(size_t) m] = state[(size_t) m] > 0.5f ? 0.0f : 1.0f;
        ev.push_back ({ t, kOnParams[(size_t) m], state[(size_t) m] });
    }
    const auto out = renderWithAutomation (*p, in, 100, ev);
    const float d = maxDiffDelayed (out, in, 212, 212);
    check (ev.size() > 20 && d < 1.0e-5f, "random module toggling keeps the signal aligned to the strip latency",
           juce::String ((int) ev.size()) + " toggles, max diff " + juce::String (d, 8));
}

NL_TEST (engine_latencyReportedForRealSettings)
{
    // Contract with the module owners: reported latency equals the delay a real signal sees,
    // and it does not depend on which modules are on. Passes trivially while all latencies are 0.
    auto p = makeProc (kFs, 128);
    makeTransparentWhenOn (*p);
    setParam (*p, P::gateLookahead, 2); // 2 ms
    setParam (*p, P::gateOn, 0);
    p->getStrip().reset();
    p->updateLatency();
    const int off = p->getLatencySamples();
    check (measuredDelay (*p) == off, "gate lookahead 2 ms, module off: measured delay equals reported", juce::String (off) + " samples");
    setParam (*p, P::gateOn, 1);
    p->getStrip().reset();
    p->updateLatency();
    check (p->getLatencySamples() == off && measuredDelay (*p) == off, "gate lookahead 2 ms, module on: latency unchanged and aligned");
}

//==============================================================================
// Input stage, global mix, monitor, bus layouts (roadmap 7.1)
//==============================================================================
NL_TEST (engine_inputStagePolaritySwap)
{
    auto p = makeProc (kFs, 256);
    allModulesOff (*p);
    juce::Random rng (5);
    juce::AudioBuffer<float> in (2, 24000);
    fillNoise (in, rng, 0.3f);

    struct Case { int polarity; bool swap; };
    int bad = 0;
    for (auto c : { Case { 0, false }, Case { 1, false }, Case { 2, false }, Case { 3, false }, Case { 0, true }, Case { 3, true }, Case { 1, true } })
    {
        setParam (*p, P::inPolarity, (float) c.polarity);
        setParam (*p, P::inSwap, c.swap ? 1.0f : 0.0f);
        p->getStrip().reset();
        const auto out = render (*p, in, 256);
        float worst = 0.0f;
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            float l = in.getSample (0, i), r = in.getSample (1, i);
            if (c.swap) std::swap (l, r);
            if (c.polarity == 1 || c.polarity == 3) l = -l;
            if (c.polarity == 2 || c.polarity == 3) r = -r;
            worst = std::max ({ worst, std::abs (out.getSample (0, i) - l), std::abs (out.getSample (1, i) - r) });
        }
        bad += worst != 0.0f;
    }
    check (bad == 0, "polarity (L, R, both) and channel swap are exact", juce::String (bad) + " of 7 cases off");

    // Click-free: flipping polarity or swapping while a 1 kHz tone runs stays continuous.
    juce::AudioBuffer<float> tone (2, 48000);
    for (int i = 0; i < tone.getNumSamples(); ++i)
    {
        tone.setSample (0, i, 0.5f * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs));
        tone.setSample (1, i, 0.25f * (float) std::sin (2.0 * kPiD * 1000.0 * i / kFs + 1.0));
    }
    setParam (*p, P::inPolarity, 0);
    setParam (*p, P::inSwap, 0);
    p->getStrip().reset();
    const auto out = renderWithAutomation (*p, tone, 256, { { 20480, P::inPolarity, 3.0f }, { 30720, P::inSwap, 1.0f }, { 40960, P::inPolarity, 0.0f } });
    float step = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 1; i < out.getNumSamples(); ++i) step = std::max (step, std::abs (out.getSample (c, i) - out.getSample (c, i - 1)));
    check (step < 0.07f, "polarity and swap switches are click-free", "largest sample step " + juce::String (step, 4) + " (tone's own: 0.065)");
}

NL_TEST (engine_globalMix)
{
    const std::array<int, numModules> lat { 0, 37, 0, 20, 100 };
    juce::Random rng (9);
    juce::AudioBuffer<float> in (2, (int) (2 * kFs));
    fillNoise (in, rng, 0.25f);

    auto make = [&] (float mix)
    {
        auto p = makeProc (kFs, 256);
        configureEverything (*p, false, false);
        giveModulesLatency (*p, lat);
        setParam (*p, P::globalMix, mix);
        p->getStrip().reset();
        return p;
    };
    auto p0 = make (0.0f), p100 = make (100.0f), p50 = make (50.0f);
    const auto o0 = render (*p0, in, 256), o100 = render (*p100, in, 256), o50 = render (*p50, in, 256);
    const int delay = p0->getStrip().getChainLatencySamples();

    const float nullDb = juce::Decibels::gainToDecibels (maxDiffDelayed (o0, in, delay, delay), -300.0f);
    check (delay == 157 && nullDb <= -120.0f, "mix 0% nulls against the latency-aligned input", juce::String (nullDb, 1) + " dB, chain latency " + juce::String (delay));

    juce::AudioBuffer<float> avg (2, in.getNumSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < in.getNumSamples(); ++i) avg.setSample (c, i, 0.5f * (o0.getSample (c, i) + o100.getSample (c, i)));
    check (maxAbsDiff (avg, o50) < 1.0e-6f, "mix 50% is the average of dry and wet", "max diff " + juce::String (maxAbsDiff (avg, o50), 8));
    check (maxAbsDiff (o100, o0, delay) > 0.01f, "mix 100% differs from the dry signal (the chain is audible)");

    // Mix moves smoothly: a step from 100% to 0% mid-stream has no discontinuity beyond the
    // signals' own (the larger of the wet and the dry sample-to-sample steps).
    auto q = make (100.0f);
    const auto o = renderWithAutomation (*q, in, 256, { { 40960, P::globalMix, 0.0f } });
    float step = 0.0f, ref = 0.0f;
    for (int i = 1; i < o.getNumSamples(); ++i)
    {
        step = std::max (step, std::abs (o.getSample (0, i) - o.getSample (0, i - 1)));
        ref = std::max ({ ref, std::abs (o100.getSample (0, i) - o100.getSample (0, i - 1)),
                         std::abs (o0.getSample (0, i) - o0.getSample (0, i - 1)) });
    }
    check (step < 3.0f * ref, "global mix changes are click-free", "largest step " + juce::String (step, 3) + ", signal's own " + juce::String (ref, 3));
}

NL_TEST (engine_monitorModes)
{
    auto p = makeProc (kFs, 256);
    allModulesOff (*p);
    juce::Random rng (13);
    juce::AudioBuffer<float> in (2, 12000);
    fillNoise (in, rng, 0.4f);

    int bad = 0;
    for (int mode = 0; mode < 6; ++mode)
    {
        setParam (*p, P::monitor, (float) mode);
        p->getStrip().reset();
        const auto out = render (*p, in, 256);
        float worst = 0.0f;
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float l = in.getSample (0, i), r = in.getSample (1, i);
            float el = l, er = r;
            switch (mode)
            {
                case 1: el = er = 0.5f * (l + r); break;
                case 2: er = l; break;
                case 3: el = r; break;
                case 4: el = er = 0.5f * (l - r); break; // Side: 0.5 (L - R)
                case 5: el = r; er = l; break;
                default: break;
            }
            worst = std::max ({ worst, std::abs (out.getSample (0, i) - el), std::abs (out.getSample (1, i) - er) });
        }
        bad += worst > 1.0e-6f;
    }
    check (bad == 0, "monitor modes Stereo, Mono, Left, Right, Side (0.5 (L-R)) and Swap", juce::String (bad) + " of 6 modes off");

    // Click-free switching.
    juce::AudioBuffer<float> tone (2, 48000);
    for (int i = 0; i < tone.getNumSamples(); ++i)
    {
        tone.setSample (0, i, 0.5f * (float) std::sin (2.0 * kPiD * 500.0 * i / kFs));
        tone.setSample (1, i, 0.5f * (float) std::sin (2.0 * kPiD * 500.0 * i / kFs + 2.0));
    }
    setParam (*p, P::monitor, 0);
    p->getStrip().reset();
    const auto out = renderWithAutomation (*p, tone, 256, { { 10240, P::monitor, 4.0f }, { 20480, P::monitor, 5.0f }, { 30720, P::monitor, 0.0f } });
    float step = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 1; i < out.getNumSamples(); ++i) step = std::max (step, std::abs (out.getSample (c, i) - out.getSample (c, i - 1)));
    check (step < 0.04f, "monitor switches are click-free", "largest sample step " + juce::String (step, 4) + " (tone's own: 0.033)");
}

NL_TEST (engine_busLayouts)
{
    auto p = std::make_unique<NorthlineProcessor>();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    auto layoutOf = [&] (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out, const juce::AudioChannelSet& sc)
    {
        auto l = p->getBusesLayout();
        l.inputBuses.getReference (0) = in;
        l.outputBuses.getReference (0) = out;
        l.inputBuses.getReference (1) = sc;
        return l;
    };
    const auto none = juce::AudioChannelSet::disabled();
    check (p->isBusesLayoutSupported (layoutOf (stereo, stereo, none)) && p->isBusesLayoutSupported (layoutOf (mono, mono, none))
               && p->isBusesLayoutSupported (layoutOf (mono, stereo, none)) && p->isBusesLayoutSupported (layoutOf (mono, stereo, stereo)),
           "stereo, mono and mono-in/stereo-out layouts are supported");
    check (! p->isBusesLayoutSupported (layoutOf (stereo, mono, none)) && ! p->isBusesLayoutSupported (layoutOf (juce::AudioChannelSet::quadraphonic(), stereo, none)),
           "stereo-in/mono-out and surround layouts are refused");

    // Mono in, stereo out: the input feeds both channels, whatever the host leaves on channel 1.
    check (p->setBusesLayout (layoutOf (mono, stereo, none)), "mono-in/stereo-out layout can be set");
    p->setRateAndBufferSizeDetails (kFs, 128);
    p->prepareToPlay (kFs, 128);
    juce::AudioBuffer<float> buf (2, 128);
    juce::MidiBuffer midi;
    juce::Random rng (2);
    float worst = 0.0f;
    for (int b = 0; b < 10; ++b)
    {
        juce::AudioBuffer<float> ref (1, 128);
        for (int i = 0; i < 128; ++i) { ref.setSample (0, i, rng.nextFloat() - 0.5f); buf.setSample (0, i, ref.getSample (0, i)); buf.setSample (1, i, 7.0f); }
        p->processBlock (buf, midi);
        for (int i = 0; i < 128; ++i)
            worst = std::max ({ worst, std::abs (buf.getSample (0, i) - ref.getSample (0, i)), std::abs (buf.getSample (1, i) - ref.getSample (0, i)) });
    }
    check (worst == 0.0f, "mono input is copied to both output channels", "max diff " + juce::String (worst, 8));

    // With a sidechain the key sits on the buffer channel the right output uses: it must be read first.
    check (p->setBusesLayout (layoutOf (mono, stereo, stereo)), "mono-in/stereo-out with a stereo sidechain can be set");
    p->prepareToPlay (kFs, 128);
    allModulesOff (*p);
    setParam (*p, P::gateOn, 1); setParam (*p, P::gateExt, 1); setParam (*p, P::gateThresh, -30); setParam (*p, P::gateFloor, -60);
    setParam (*p, P::gateAttack, 0.05f); setParam (*p, P::gateHold, 0); setParam (*p, P::gateRelease, 5);
    p->getStrip().reset();
    juce::AudioBuffer<float> sc3 (3, 128);
    auto run = [&] (float keyAmp)
    {
        float level = 0.0f;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                sc3.setSample (0, i, 0.3f * (float) std::sin (0.1 * (b * 128 + i)));
                const float k = keyAmp * (rng.nextFloat() * 2.0f - 1.0f);
                sc3.setSample (1, i, k);
                sc3.setSample (2, i, k);
            }
            p->processBlock (sc3, midi);
            if (b >= 150) for (int i = 0; i < 128; ++i) level = std::max (level, std::abs (sc3.getSample (0, i)));
        }
        return level;
    };
    const float closed = run (0.0f), open = run (0.5f);
    check (open > 0.25f && closed < 0.01f, "gate follows the external key in the mono-in layout", "open " + juce::String (open, 3) + ", closed " + juce::String (closed, 4));
}

//==============================================================================
// D3 determinism and dirty flags
//==============================================================================
namespace
{
// Random values within each parameter's range; gain-like dB parameters (ranges that span zero)
// are limited to +/-gainLimitDb so the output stays near full scale (float rounding of a loud
// output would otherwise exceed the 1e-6 absolute bound by itself).
std::vector<AutomationEvent> randomAutomation (int length, const std::vector<int>& eligible, uint32_t seed, int perEvent, float gainLimitDb)
{
    juce::Random rng ((juce::int64) seed);
    std::vector<AutomationEvent> ev;
    for (int t = 2048; t < length; t += 2048)
        for (int k = 0; k < perEvent; ++k)
        {
            const int idx = eligible[(size_t) rng.nextInt ((int) eligible.size())];
            const auto& d = paramDef (idx);
            float lo = d.minV, hi = d.maxV;
            if (d.unit == Unit::Db && lo < 0.0f && hi > 0.0f) { lo = std::max (lo, -gainLimitDb); hi = std::min (hi, gainLimitDb); }
            ev.push_back ({ t, idx, lo + (hi - lo) * rng.nextFloat() });
        }
    return ev;
}

std::vector<int> continuousParams()
{
    std::vector<int> out;
    for (int i = 0; i < P::count; ++i)
        if (paramDef (i).kind == ParamDef::Float && isAutomatable (i) && ! isTransient (i)) out.push_back (i);
    return out;
}

// Renders the same material with each block size; returns the worst difference against 2048.
float determinismSpread (const std::function<void (NorthlineProcessor&)>& configure, const juce::AudioBuffer<float>& in,
                         const std::vector<AutomationEvent>& ev, juce::String& detail)
{
    auto reference = [&]
    {
        auto p = makeProc (kFs, 2048);
        configure (*p);
        p->getStrip().reset();
        return renderWithAutomationExact (*p, in, 2048, ev);
    }();
    float worst = 0.0f;
    for (int block : { 1, 7, 32, 37, 64, 511, 512 })
    {
        auto p = makeProc (kFs, 2048);
        configure (*p);
        p->getStrip().reset();
        const float d = maxAbsDiff (renderWithAutomationExact (*p, in, block, ev), reference);
        detail << block << ":" << juce::String (d, 8) << " ";
        worst = std::max (worst, d);
    }
    return worst;
}
} // namespace

NL_TEST (engine_determinismMasterGains)
{
    // Strip-level gains only (modules off): input, output, mix and match trim, with Level Match
    // Live measuring against a static EQ dip, under automation.
    juce::Random rng (31);
    juce::AudioBuffer<float> in (2, 6 * 2048 * 4);
    fillNoise (in, rng, 0.3f);
    const std::vector<int> eligible { P::inGain, P::outGain, P::globalMix, P::matchTrim, P::limCeiling };
    const auto ev = randomAutomation (in.getNumSamples(), eligible, 77, 3, 6.0f);

    juce::String detail;
    const float worst = determinismSpread ([] (NorthlineProcessor& p)
    {
        allModulesOff(p);
        setParam (p, P::eqOn, 1);
        setParam (p, P::eqMidType, 2); setParam (p, P::eqMidFreq, 1000); setParam (p, P::eqMidQ, 0.3f); setParam (p, P::eqMidGain, -8);
        setParam (p, P::matchMode, 2);
    }, in, ev, detail);
    check (worst <= 1.0e-6f, "strip gains + Level Match Live: output independent of block size under automation (1/7/32/37/64/511/512 vs 2048)",
           "max diff " + juce::String (worst, 9) + "  [" + detail.trim() + "]");

    juce::String detail2;
    const float worst2 = determinismSpread ([] (NorthlineProcessor& p)
    {
        allModulesOff (p);
        setParam (p, P::matchMode, 1);
        setParam (p, P::monitor, 0);
    }, in, ev, detail2);
    check (worst2 <= 1.0e-6f, "strip gains + Level Match Learn: output independent of block size under automation",
           "max diff " + juce::String (worst2, 9) + "  [" + detail2.trim() + "]");
}

NL_TEST (integration_determinismAutomation)
{
    // Everything on, random automation of every continuous parameter every 2048 samples.
    juce::Random rng (41);
    juce::AudioBuffer<float> in (2, 3 * 48000);
    fillNoise (in, rng, 0.3f);
    const auto ev = randomAutomation (in.getNumSamples(), continuousParams(), 5, 8, 4.0f);
    juce::String detail;
    const float worst = determinismSpread ([] (NorthlineProcessor& p)
    {
        configureEverything (p, false, false);
        setParam (p, P::matchMode, 2);
    }, in, ev, detail);
    check (worst <= 1.0e-6f, "everything on: output independent of block size under automation (1/7/32/37/64/511/512 vs 2048)",
           "max diff " + juce::String (worst, 9) + "  [" + detail.trim() + "]");
}

NL_TEST (engine_dirtyParamUpdates)
{
    auto p = makeProc (kFs, 256);
    configureEverything (*p, false, false);
    p->getStrip().reset();
    auto counts = [&]
    {
        std::array<uint32_t, numModules + 1> c {};
        for (int i = 0; i <= numModules; ++i) c[(size_t) i] = p->getStrip().getParamUpdateCount (i);
        return c;
    };
    juce::Random rng (3);
    juce::AudioBuffer<float> blk (2, 256);
    juce::MidiBuffer midi;
    auto runBlocks = [&] (int n) { for (int i = 0; i < n; ++i) { fillNoise (blk, rng, 0.2f); p->processBlock (blk, midi); } };

    runBlocks (4);
    const auto base = counts();
    runBlocks (50);
    check (counts() == base, "constant parameters: no module is updated", "no setParams calls in 50 blocks");

    setParam (*p, P::compThresh, -22);
    runBlocks (3);
    auto c = counts();
    bool onlyComp = c[modComp] == base[modComp] + 1;
    for (int m : { (int) modPre, (int) modGate, (int) modDs, (int) modEq, (int) numModules }) onlyComp &= c[(size_t) m] == base[(size_t) m];
    check (onlyComp, "changing a compressor parameter updates only the compressor, once");

    setParam (*p, P::inGain, 3.0f);
    setParam (*p, P::outGain, -2.0f);
    runBlocks (3);
    check (counts() == c, "strip-level gains do not touch the modules");

    setParam (*p, P::eqToneGain, 5.0f);
    setParam (*p, P::limCeiling, -3.0f);
    runBlocks (3);
    auto d = counts();
    check (d[modEq] == c[modEq] + 1 && d[numModules] == c[numModules] + 1 && d[modComp] == c[modComp], "EQ and limiter settings update only their own module");

    setParam (*p, P::quality, 1);
    runBlocks (3);
    auto e = counts();
    bool all = true;
    for (int m = 0; m <= numModules; ++m) all &= e[(size_t) m] == d[(size_t) m] + 1;
    check (all, "a global setting (quality) updates every module once");

    // Same result as updating everything every block.
    juce::AudioBuffer<float> in (2, 48000);
    fillNoise (in, rng, 0.3f);
    const auto ev = randomAutomation (in.getNumSamples(), continuousParams(), 9, 4, 4.0f);
    auto a = makeProc (kFs, 256), b = makeProc (kFs, 256);
    for (auto* q : { a.get(), b.get() }) { configureEverything (*q, false, false); q->getStrip().reset(); }
    b->getStrip().setForceFullParamUpdates (true);
    const float diff = maxAbsDiff (renderWithAutomation (*a, in, 256, ev), renderWithAutomation (*b, in, 256, ev));
    check (diff == 0.0f, "dirty-flag updates give the same output as full updates every block", "max diff " + juce::String (diff, 9));
}

//==============================================================================
// D11 transient parameters, schema v2 migration
//==============================================================================
namespace
{
// Gives the tests access to the (protected) binary XML helpers.
struct Probe : NorthlineProcessor
{
    static void pack (const juce::XmlElement& xml, juce::MemoryBlock& mb) { copyXmlToBinary (xml, mb); }
    static std::unique_ptr<juce::XmlElement> unpack (const void* data, int size) { return getXmlFromBinary (data, size); }
};

constexpr std::array<int, 5> kTransientIndices { P::monitor, P::gateListen, P::dsListen, P::compListen, P::eqSolo };

void setTransients (NorthlineProcessor& p)
{
    setParam (p, P::monitor, 4);
    setParam (p, P::gateListen, 1);
    setParam (p, P::dsListen, 2);
    setParam (p, P::compListen, 1);
    setParam (p, P::eqSolo, 2);
}

bool transientsAreDefault (NorthlineProcessor& p)
{
    bool ok = true;
    for (int i : kTransientIndices) ok &= plain (p, i) == paramDef (i).defV;
    return ok;
}

bool documentMentionsTransient (const juce::XmlElement& xml)
{
    const auto doc = xml.toString();
    for (int i = 0; i < P::count; ++i)
        if (isTransient (i) && doc.contains (paramID (i))) return true;
    return false;
}
} // namespace

NL_TEST (engine_transientParams)
{
    // The table marks exactly the monitoring aids as transient.
    bool flags = true;
    for (int i = 0; i < P::count; ++i)
    {
        const bool expected = std::find (kTransientIndices.begin(), kTransientIndices.end(), i) != kTransientIndices.end();
        flags &= isTransient (i) == expected;
    }
    check (flags, "monitor, the three listens and band solo are the transient parameters");

    auto p = makeProc();
    setParam (*p, P::compThresh, -33);
    setTransients (*p);
    check (! transientsAreDefault (*p), "test setup: monitoring aids are engaged");

    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    auto xml = Probe::unpack (mb.getData(), (int) mb.getSize());
    check (xml != nullptr && ! documentMentionsTransient (*xml), "saved session contains no transient parameter (state, A/B slots)");

    auto q = makeProc();
    setParam (*q, P::monitor, 3);
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    check (transientsAreDefault (*q) && std::abs (plain (*q, P::compThresh) + 33.0f) < 0.01f,
           "reloading a session saved while soloed/monitoring gives Stereo, listens off, solo off (settings kept)");

    const auto snap = p->makeSnapshot();
    bool snapClean = true;
    for (int i : kTransientIndices) snapClean &= ! snap.hasProperty (paramID (i));
    check (snapClean, "snapshots (undo, A/B, presets) exclude transient parameters");

    // User presets.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("northline_engine_test_preset.xml");
    check (p->saveUserPreset (file, "Transient test"), "user preset saved");
    auto pxml = juce::XmlDocument::parse (file);
    check (pxml != nullptr && ! documentMentionsTransient (*pxml), "preset file contains no transient parameter");
    setTransients (*q);
    check (q->loadUserPreset (file) && transientsAreDefault (*q), "loading a preset resets the transient parameters");
    file.deleteFile();

    // A/B and undo neither store nor touch them; factory presets and resets clear them.
    setParam (*p, P::monitor, 1);
    p->pushUndoSnapshot();
    setParam (*p, P::compThresh, -10);
    p->pushUndoSnapshot();
    p->selectSlot (1);
    p->selectSlot (0);
    p->undo();
    p->redo();
    check (std::abs (plain (*p, P::monitor) - 1.0f) < 0.01f, "A/B and undo/redo leave the monitor mode alone");
    p->loadFactoryPreset (1);
    check (transientsAreDefault (*p), "loading a factory preset resets the transient parameters");
    setTransients (*p);
    p->resetToDefaults();
    check (transientsAreDefault (*p), "reset to defaults clears the transient parameters");

    // The audio path honours the reset: plain stereo, not Side.
    juce::Random rng (4);
    juce::AudioBuffer<float> in (2, 4800);
    fillNoise (in, rng, 0.4f);
    allModulesOff (*p);
    p->getStrip().reset();
    check (maxAbsDiff (render (*p, in, 256), in) == 0.0f, "after a reload the output is the plain stereo signal");
}

NL_TEST (engine_stateMigrationV1)
{
    const char* v1Session = R"(<NorthlineState order="2,1,0,3,4" focus="comp_thresh,sat_drive" presetName="Old Vocal" uiScale="1.25" activeSlot="1" stateVersion="1">
  <PARAM id="in_gain" value="3.5"/>
  <PARAM id="in_phase" value="1"/>
  <PARAM id="monitor" value="5"/>
  <PARAM id="lim_lookahead" value="1"/>
  <PARAM id="level_match" value="1"/>
  <PARAM id="vu_ref" value="-26"/>
  <PARAM id="sat_hq" value="1"/>
  <PARAM id="thump_on" value="1"/>
  <PARAM id="thump_amt" value="1"/>
  <PARAM id="ds_listen" value="2"/>
  <PARAM id="comp_listen" value="1"/>
  <PARAM id="comp_thresh" value="-31"/>
  <PARAM id="eq_tone_gain" value="4.5"/>
  <AB>
    <Snapshot slot="0" in_phase="0" level_match="0" lim_lookahead="0" vu_ref="-6" sat_hq="0" comp_thresh="-12" order="0,1,2,3,4" focus="" presetName="A"/>
    <Snapshot slot="1" in_phase="1" level_match="1" lim_lookahead="1" vu_ref="-20" sat_hq="1" comp_thresh="-31" order="2,1,0,3,4" focus="" presetName="Old Vocal"/>
  </AB>
</NorthlineState>)";

    auto doc = juce::XmlDocument::parse (juce::String (v1Session));
    check (doc != nullptr, "test setup: hand-written v1 session parses");
    juce::MemoryBlock mb;
    Probe::pack (*doc, mb);

    auto p = makeProc();
    p->setStateInformation (mb.getData(), (int) mb.getSize());
    check (plain (*p, P::inPolarity) == 3.0f, "in_phase on -> in_polarity Invert L+R");
    check (plain (*p, P::matchMode) == 2.0f, "level_match on -> match_mode Live");
    check (plain (*p, P::limMode) == 1.0f, "lim_lookahead on -> lim_mode Brickwall");
    check (plain (*p, P::opLevel) == -24.0f, "vu_ref -26 -> op_level clamped to -24", juce::String (plain (*p, P::opLevel), 2));
    check (plain (*p, P::quality) == 1.0f, "sat_hq on -> quality HQ");
    check (plain (*p, P::weightOn) == 1.0f && plain (*p, P::weightAmt) == 1.0f, "thump_on / thump_amt -> weight_on / weight_amt");
    check (std::abs (plain (*p, P::inGain) - 3.5f) < 0.001f && std::abs (plain (*p, P::compThresh) + 31.0f) < 0.001f
               && std::abs (plain (*p, P::eqToneGain) - 4.5f) < 0.001f, "unchanged IDs keep their values");
    check (transientsAreDefault (*p), "v1 monitor / listen values are not restored (transient)");
    check (p->getOrder() == ChannelStrip::Order { 2, 1, 0, 3, 4 } && p->getPresetName() == "Old Vocal" && std::abs (p->getUiScale() - 1.25f) < 1.0e-6f
               && p->getActiveSlot() == 1 && p->getFocus().size() == 2, "order, preset name, UI scale, active slot and focus are kept");

    p->selectSlot (0);
    check (plain (*p, P::inPolarity) == 0.0f && plain (*p, P::matchMode) == 0.0f && plain (*p, P::limMode) == 0.0f && plain (*p, P::quality) == 0.0f
               && plain (*p, P::opLevel) == -10.0f && std::abs (plain (*p, P::compThresh) + 12.0f) < 0.001f,
           "the A/B slot snapshots are migrated too (vu_ref -6 -> -10)");

    // Saving again writes v2 and none of the old IDs.
    juce::MemoryBlock again;
    p->getStateInformation (again);
    auto out = Probe::unpack (again.getData(), (int) again.getSize());
    const auto text = out != nullptr ? out->toString() : juce::String();
    bool oldIds = false;
    for (auto* id : { "in_phase", "level_match", "lim_lookahead", "vu_ref", "sat_hq", "thump_" }) oldIds |= text.contains (id);
    check (out != nullptr && out->getIntAttribute ("stateVersion") == NorthlineProcessor::kStateVersion && ! oldIds,
           "re-saved state is version 2 with no v1 IDs");

    // And a v1 user preset.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("northline_engine_test_v1preset.xml");
    file.replaceWithText (R"(<NorthlinePreset version="1" name="Legacy"><Snapshot in_phase="1" level_match="1" vu_ref="-4" sat_hq="1" thump_on="1" comp_thresh="-27" order="0,1,2,3,4" focus="" presetName="x"/></NorthlinePreset>)");
    auto q = makeProc();
    check (q->loadUserPreset (file), "v1 preset file loads");
    check (plain (*q, P::inPolarity) == 3.0f && plain (*q, P::matchMode) == 2.0f && plain (*q, P::opLevel) == -10.0f && plain (*q, P::quality) == 1.0f
               && plain (*q, P::weightOn) == 1.0f && std::abs (plain (*q, P::compThresh) + 27.0f) < 0.001f && q->getPresetName() == "Legacy",
           "v1 preset is migrated (vu_ref -4 -> -10)");
    file.deleteFile();
}

//==============================================================================
// D7 thread-safe state
//==============================================================================
NL_TEST (engine_stateStress)
{
    auto p = makeProc (kFs, 256);
    configureEverything (*p, false, false);
    setParam (*p, P::matchMode, 2);
    p->getStrip().setSampleStreamingEnabled (true);

    std::atomic<bool> stop { false }, nonFinite { false }, torn { false };
    std::atomic<long long> renders { 0 }, stateOps { 0 }, polls { 0 }, popped { 0 };

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        juce::Random rng (1);
        while (! stop.load())
        {
            fillNoise (buf, rng, 0.3f);
            p->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i) if (! std::isfinite (buf.getSample (c, i))) nonFinite = true;
            ++renders;
            std::this_thread::sleep_for (std::chrono::microseconds (300));
        }
    });

    std::thread state ([&]
    {
        juce::Random rng (2);
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("northline_engine_stress_preset.xml");
        const auto continuous = continuousParams();
        while (! stop.load())
        {
            switch (rng.nextInt (11))
            {
                case 0:
                case 1:
                {
                    // Round trip: the restored state matches what was saved.
                    std::vector<float> before;
                    for (int i = 0; i < P::count; ++i) before.push_back (plain (*p, i));
                    const auto order = p->getOrder();
                    juce::MemoryBlock mb;
                    p->getStateInformation (mb);
                    p->setStateInformation (mb.getData(), (int) mb.getSize());
                    for (int i = 0; i < P::count; ++i)
                        if (! isTransient (i) && std::abs (plain (*p, i) - before[(size_t) i]) > 1.0e-3f + 1.0e-4f * std::abs (before[(size_t) i])) torn = true;
                    if (p->getOrder() != order) torn = true;
                    break;
                }
                case 2: p->undo(); break;
                case 3: p->redo(); break;
                case 4: p->loadFactoryPreset (rng.nextInt ((int) factoryPresets().size())); break;
                case 5: p->selectSlot (rng.nextInt (2)); break;
                case 6: setParam (*p, continuous[(size_t) rng.nextInt ((int) continuous.size())], 0.0f); p->pushUndoSnapshot(); break;
                case 7: p->copyActiveToOther(); p->setUiScale (0.75f + 0.25f * (float) rng.nextInt (5)); break;
                case 8: p->moveModule (rng.nextInt (numModules), rng.nextBool() ? 1 : -1); break;
                case 9: if (rng.nextInt (6) == 0) { p->saveUserPreset (file, "Stress"); p->loadUserPreset (file); } break;
                default: p->param (P::monitor)->setValueNotifyingHost (rng.nextFloat()); break;
            }
            ++stateOps;
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
        file.deleteFile();
    });

    std::thread ui ([&]
    {
        std::vector<MeterFrame> frames (256);
        std::vector<StereoSample> samples (4096);
        while (! stop.load())
        {
            const auto name = p->getPresetName();
            const auto focus = p->getFocus();
            for (int i : focus) if (i < 0 || i >= P::count) torn = true;
            if (name.length() > 200 || p->getUiScale() < 0.4f || p->getUiScale() > 4.0f) torn = true;
            (void) p->canUndo(); (void) p->canRedo(); (void) p->getActiveSlot(); (void) p->getOrder();
            (void) p->getLevelMatchStatus(); (void) p->getLatencySamples(); (void) p->getStrip().meterHub().read();
            for (int i = 0; i < P::count; i += 7) (void) p->param (i)->getValue();
            popped += (long long) p->getStrip().popMeterFrames (frames.data(), frames.size());
            popped += (long long) p->getStrip().popOutputSamples (samples.data(), samples.size());
            p->servicePendingWork();
            ++polls;
            std::this_thread::sleep_for (std::chrono::microseconds (150));
        }
    });

    juce::Thread::sleep (10000);
    stop = true;
    audio.join();
    state.join();
    ui.join();

    check (renders.load() > 200 && stateOps.load() > 200 && polls.load() > 200, "all three threads made progress for 10 s",
           juce::String (renders.load()) + " blocks, " + juce::String (stateOps.load()) + " state operations, " + juce::String (polls.load()) + " UI polls");
    check (! nonFinite.load(), "rendering stayed finite during the stress");
    check (! torn.load(), "no torn state: round trips restore the state, UI accessors only ever see valid values");
    check (popped.load() > 0, "the UI thread drained meter data while the state changed", juce::String (popped.load()) + " items");

    juce::MemoryBlock a;
    p->getStateInformation (a);
    auto q = makeProc (kFs, 256);
    q->setStateInformation (a.getData(), (int) a.getSize());
    bool same = true;
    for (int i = 0; i < P::count; ++i)
        same &= isTransient (i) || std::abs (plain (*p, i) - plain (*q, i)) <= 1.0e-3f + 1.0e-4f * std::abs (plain (*p, i));
    check (same && p->getOrder() == q->getOrder() && p->getPresetName() == q->getPresetName(), "state still round-trips after the stress");
}

NL_TEST (engine_ringsSurvivePrepare)
{
    auto p = makeProc (kFs, 256);
    p->getStrip().setSampleStreamingEnabled (true);
    std::vector<StereoSample> samples (4096);
    std::vector<MeterFrame> frames (256);
    bool reset = false;
    p->getStrip().popOutputSamples (samples.data(), samples.size(), &reset); // first call syncs to the generation
    p->getStrip().popMeterFrames (frames.data(), frames.size());

    juce::Random rng (6);
    juce::AudioBuffer<float> in (2, 4800);
    fillNoise (in, rng, 0.3f);
    render (*p, in, 256);
    const auto got = p->getStrip().popOutputSamples (samples.data(), samples.size(), &reset);
    check (got > 0 && ! reset, "samples flow through the ring", juce::String ((int) got) + " samples");
    render (*p, in, 256);
    p->prepareToPlay (96000.0, 512);
    const auto stale = p->getStrip().popOutputSamples (samples.data(), samples.size(), &reset);
    check (stale == 0 && reset, "after prepare the consumer is told to discard what was in the ring");
    p->getStrip().popMeterFrames (frames.data(), frames.size(), &reset);
    check (reset, "same for the meter frames");

    // A consumer thread keeps draining while prepare() is called repeatedly.
    std::atomic<bool> stop { false }, bad { false };
    std::atomic<long long> total { 0 };
    std::thread consumer ([&]
    {
        std::vector<StereoSample> s (2048);
        std::vector<MeterFrame> f (64);
        while (! stop.load())
        {
            const auto n = p->getStrip().popOutputSamples (s.data(), s.size());
            for (size_t i = 0; i < n; ++i) if (! std::isfinite (s[i].l) || ! std::isfinite (s[i].r)) bad = true;
            total += (long long) n + (long long) p->getStrip().popMeterFrames (f.data(), f.size());
            std::this_thread::sleep_for (std::chrono::microseconds (100));
        }
    });
    const double rates[] { 44100.0, 192000.0, 48000.0, 384000.0, 96000.0 };
    for (int round = 0; round < 40; ++round)
    {
        const double fs = rates[round % 5];
        p->setRateAndBufferSizeDetails (fs, 256);
        p->prepareToPlay (fs, 256);
        juce::AudioBuffer<float> blk (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < 25; ++b)
        {
            fillNoise (blk, rng, 0.3f);
            p->processBlock (blk, midi);
            juce::Thread::sleep (1);
        }
    }
    stop = true;
    consumer.join();
    check (! bad.load() && total.load() > 0, "prepare() under a live consumer: no crash, no garbage", juce::String (total.load()) + " items read");
}

NL_TEST (engine_realtimeSafetyEngine)
{
    // The allocation counter must stay at zero with every engine feature in use.
    auto p = makeProc (kFs, 256, true);
    configureEverything (*p, true, true);
    setParam (*p, P::matchMode, 2);
    setParam (*p, P::globalMix, 60);
    setParam (*p, P::inPolarity, 1);
    setParam (*p, P::inSwap, 1);
    for (int m = 0; m < numModules; ++m) p->getStrip().setTestModuleLatency (m, 20 + 7 * m);
    p->getStrip().setSampleStreamingEnabled (true);
    p->getStrip().reset();

    juce::Random rng (8);
    juce::AudioBuffer<float> work (4, 256);
    juce::MidiBuffer midi;
    fillNoise (work, rng, 0.3f);
    p->processBlock (work, midi); // warm-up
    allocationCount = 0;
    countAllocations = true;
    for (int i = 0; i < 600; ++i)
    {
        fillNoise (work, rng, 0.3f);
        if (i % 40 == 0) setParam (*p, P::monitor, (float) ((i / 40) % 6));
        if (i % 30 == 0) setParam (*p, P::inPolarity, (float) ((i / 30) % 4));
        if (i % 50 == 0) setParam (*p, P::globalMix, (float) (20 + (i / 50) * 7 % 80));
        if (i % 60 == 0) setParam (*p, P::inSwap, (float) ((i / 60) % 2));
        if (i % 70 == 0) setParam (*p, kOnParams[(size_t) ((i / 70) % numModules)], (float) ((i / 70) % 2));
        if (i % 90 == 0) setParam (*p, P::outGain, (float) (i % 7) - 3.0f);
        p->processBlock (work, midi);
    }
    countAllocations = false;
    check (allocationCount.load() == 0, "no heap allocation with level match, mix, polarity, swap, monitor, latency alignment and module toggling",
           juce::String (allocationCount.load()) + " allocations");
    p->getStrip().setSampleStreamingEnabled (false);
}

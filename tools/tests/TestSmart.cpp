#include "../TestUtil.h"
#include "smart/SmartAnalysis.h"
#include "gui/SmartStartPanel.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <thread>

// Differentiators: Smart Start (roadmap 9.1: analysis, capture, one undo step) and level-honest
// Compare and Delta (9.3).

using namespace tool;
using namespace nl;
using namespace nl::smart;

namespace
{
constexpr double kFs = 48000.0;
// Writes a component snapshot as PNG. Used only by the development snapshot tests.
static void writeSnapshot (juce::Component& c, const juce::File& file)
{
    juce::Image img = c.createComponentSnapshot (c.getLocalBounds());
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    }
}

constexpr double kTwoPi = 2.0 * juce::MathConstants<double>::pi;

// 120 Hz harmonic tone with a syllable envelope, plus sibilant bursts (second-difference noise,
// 150 ms, starting at k + 0.5 s) at about -20 dBFS RMS. Both channels are equal.
juce::AudioBuffer<float> voiceSignal (double seconds)
{
    const int n = (int) std::round (seconds * kFs);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (21);
    float p1 = 0.0f, p2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kFs;
        double v = 0.0;
        for (int h = 1; h <= 8; ++h) v += std::sin (kTwoPi * 120.0 * h * t) / h;
        v *= 0.06 * (0.6 + 0.4 * std::sin (kTwoPi * 3.0 * t));

        const float noise = rng.nextFloat() * 2.0f - 1.0f;
        const float shaped = noise - 2.0f * p1 + p2; // second difference: mostly above 4 kHz
        p2 = p1;
        p1 = noise;
        const double phase = std::fmod (t, 1.0);
        double burst = 0.0;
        if (phase >= 0.5 && phase < 0.65)
        {
            const double s = std::sin (juce::MathConstants<double>::pi * (phase - 0.5) / 0.15);
            burst = 0.0707 * shaped * s * s;
        }
        b.setSample (0, i, (float) (v + burst));
        b.setSample (1, i, (float) (v + burst));
    }
    return b;
}

// Kick: a 60 Hz sine decaying with a 60 ms time constant (peak -6 dBFS), once a second, so it returns to the
// noise floor between hits; the noise is at -60 dB RMS.
juce::AudioBuffer<float> kickSignal (double seconds)
{
    const int n = (int) std::round (seconds * kFs);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (33);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kFs;
        const double k = std::fmod (t, 1.0);
        const double kick = 0.5 * std::exp (-k / 0.06) * std::sin (kTwoPi * 60.0 * t);
        const float noise = (rng.nextFloat() * 2.0f - 1.0f) * 1.73e-3f;
        b.setSample (0, i, (float) kick + noise);
        b.setSample (1, i, (float) kick + noise);
    }
    return b;
}

std::vector<float> monoOf (const juce::AudioBuffer<float>& b)
{
    std::vector<float> m ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
    return m;
}

const Change* findChange (const Result& r, int param)
{
    for (const auto& c : r.changes)
        if (c.param == param) return &c;
    return nullptr;
}

// 90th percentile of 100 ms RMS windows, in dB (exact sort, for checking the analysis).
float p90Db (const juce::AudioBuffer<float>& b, int ch = 0)
{
    const int win = (int) std::lround (0.1 * kFs);
    std::vector<double> lv;
    for (int s = 0; s + win <= b.getNumSamples(); s += win)
        lv.push_back (rmsDbRange (b, s, s + win, ch));
    std::sort (lv.begin(), lv.end());
    return (float) lv[(size_t) std::floor (0.9 * (double) (lv.size() - 1))];
}

// Runs blocks through the processor and returns the gain reduction of the de-esser per block (dB, negative).
std::vector<float> ds1GrPerBlock (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block)
{
    std::vector<float> gr;
    juce::AudioBuffer<float> work (2, block);
    juce::MidiBuffer midi;
    MeterFrame frames[4];
    for (int pos = 0; pos + block <= in.getNumSamples(); pos += block)
    {
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, c, pos, block);
        p.processBlock (work, midi);
        const size_t got = p.getStrip().popMeterFrames (frames, 4);
        gr.push_back (got > 0 ? frames[got - 1].ds1Gr : 0.0f);
    }
    return gr;
}

// Blocks render with a control callback, so Compare can be switched at exact sample positions.
juce::AudioBuffer<float> renderSwitching (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                          const std::vector<std::pair<int, int>>& compareAt)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    juce::AudioBuffer<float> work (2, block);
    juce::MidiBuffer midi;
    size_t next = 0;
    for (int pos = 0; pos < n; pos += block)
    {
        while (next < compareAt.size() && compareAt[next].first <= pos)
            p.setCompare (compareAt[next++].second);
        const int len = std::min (block, n - pos);
        work.setSize (2, len, false, false, true);
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, c, pos, len);
        p.processBlock (work, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, work, c, 0, len);
    }
    return out;
}
} // namespace

//==============================================================================
// Smart Start: analysis on synthetic signals

NL_TEST (smart_voice_highpass_and_deesser)
{
    auto in = voiceSignal (10.0);
    const auto defaults = defaultValues();
    const Result r = analyze (monoOf (in).data(), (int) in.getNumSamples(), kFs, Source::Vocal, defaults);

    check (r.valid, "voice: the capture holds signal");
    check (std::abs (r.m.f0Hz - 120.0f) < 2.0f, "voice: pitch found at the 120 Hz fundamental", juce::String (r.m.f0Hz, 2) + " Hz");
    const Change* hp = findChange (r, P::hpFreq);
    check (hp != nullptr, "voice: a high-pass is proposed");
    if (hp != nullptr)
        check (hp->to <= 0.7f * 120.0f * 1.01f && hp->to >= 0.7f * 120.0f * 0.95f,
               "voice: high-pass sits at 0.7 x f0 (84 Hz), so it does not cut the fundamental", juce::String (hp->to, 2) + " Hz");
    const Change* dsOn = findChange (r, P::dsOn);
    check (dsOn != nullptr && dsOn->to == 1.0f, "voice: the de-esser is switched on for the sibilant bursts");
    check (r.m.sibilantPeakHz > 4000.0f && r.m.sibilantPeakHz < 10000.0f, "voice: sibilant peak in 4 to 10 kHz",
           juce::String (r.m.sibilantPeakHz, 0) + " Hz");

    // Apply the proposal and measure the real gain reduction on the bursts.
    auto p = makeProc (kFs, 256);
    p->applySmartResult (r);
    const auto gr = ds1GrPerBlock (*p, in, 256);
    std::vector<float> burst;
    for (size_t b = 0; b < gr.size(); ++b)
    {
        const double t = (double) (b * 256) / kFs;
        const double phase = std::fmod (t, 1.0);
        if (t > 1.0 && phase > 0.52 && phase < 0.63) burst.push_back (-gr[b]);
    }
    std::sort (burst.begin(), burst.end());
    const float medianGr = burst.empty() ? 0.0f : burst[burst.size() / 2];
    check (medianGr >= 3.0f && medianGr <= 6.0f, "voice: de-esser gain reduction on the bursts is 3 to 6 dB",
           juce::String (medianGr, 2) + " dB median over " + juce::String ((int) burst.size()) + " blocks");
}

NL_TEST (smart_kick_gate_on_with_threshold_in_valley)
{
    auto in = kickSignal (8.0);
    const auto defaults = defaultValues();
    const Result r = analyze (monoOf (in).data(), (int) in.getNumSamples(), kFs, Source::Kick, defaults);

    check (r.valid, "kick: the capture holds signal");
    check (r.m.gateSeparationDb > 15.0f, "kick: noise and program are more than 15 dB apart", juce::String (r.m.gateSeparationDb, 1) + " dB");
    const Change* on = findChange (r, P::gateOn);
    check (on != nullptr && on->to == 1.0f, "kick: the gate is switched on");
    const Change* th = findChange (r, P::gateThresh);
    check (th != nullptr, "kick: a gate threshold is proposed");
    if (th != nullptr)
    {
        check (th->to > r.m.noiseDb + 1.0f && th->to < -12.0f, "kick: threshold sits above the noise floor and below the kick",
               juce::String (th->to, 1) + " dB (noise " + juce::String (r.m.noiseDb, 1) + " dB)");
        check (std::abs (th->to - r.m.gateValleyDb) < 0.1f, "kick: threshold is the histogram valley");
    }
    check (findChange (r, P::gateHold) != nullptr, "kick: hold follows the note length");
}

NL_TEST (smart_quiet_signal_trim_reaches_operating_level)
{
    // Pink noise at -38 dBFS RMS: the input trim has to bring its 90th percentile to the Operating Level.
    auto in = pinkNoise ((int) (6.0 * kFs), -38.0f, 5);
    const auto defaults = defaultValues();
    const Result r = analyze (monoOf (in).data(), (int) in.getNumSamples(), kFs, Source::Bus, defaults);
    const Change* trim = findChange (r, P::inGain);
    check (trim != nullptr, "quiet: an input trim is proposed");
    if (trim == nullptr) return;

    auto p = makeProc (kFs, 256);
    setParam (*p, P::inGain, trim->to);
    const auto out = render (*p, in, 256);
    const float opLevel = defaults[P::opLevel];
    const float reached = p90Db (out);
    check (std::abs (reached - opLevel) <= 0.5f, "quiet: after the trim the 90th percentile is at the Operating Level within 0.5 dB",
           juce::String (reached, 2) + " dB against " + juce::String (opLevel, 1) + " dB, trim " + juce::String (trim->to, 2) + " dB");
}

NL_TEST (smart_analysis_is_deterministic_and_rejects_silence)
{
    auto in = voiceSignal (6.0);
    const auto mono = monoOf (in);
    const auto defaults = defaultValues();
    const Result a = analyze (mono.data(), (int) mono.size(), kFs, Source::Vocal, defaults);
    const Result b = analyze (mono.data(), (int) mono.size(), kFs, Source::Vocal, defaults);
    bool same = a.changes.size() == b.changes.size() && a.summary == b.summary;
    for (size_t i = 0; same && i < a.changes.size(); ++i)
        same = a.changes[i].param == b.changes[i].param && a.changes[i].to == b.changes[i].to && a.changes[i].reason == b.changes[i].reason;
    check (same, "analysis is deterministic: the same capture gives the same changes and reasons", juce::String ((int) a.changes.size()) + " changes");

    std::vector<float> silence (kFs * 4, 0.0f);
    const Result s = analyze (silence.data(), (int) silence.size(), kFs, Source::Vocal, defaults);
    check (! s.valid && s.changes.empty(), "a silent capture gives no changes");
}

//==============================================================================
// Smart Start: capture path and the processor

NL_TEST (smart_capture_path_does_not_allocate)
{
    auto p = makeProc (kFs, 256);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    juce::Random rng (8);
    fillNoise (work, rng, 0.2f);
    p->processBlock (work, midi); // warm-up
    check (p->smartListen (Source::Vocal), "listen starts a capture");

    allocationCount = 0;
    countingThread = std::this_thread::get_id();
    countAllocations = true;
    for (int i = 0; i < 600; ++i)
    {
        fillNoise (work, rng, 0.2f);
        p->processBlock (work, midi);
    }
    countAllocations = false;
    check (allocationCount.load() == 0, "capture and processing do not allocate on the audio thread while recording",
           juce::String (allocationCount.load()) + " allocations");
    p->smartCancel();
}

NL_TEST (smart_result_applies_as_one_undo_step_with_spotlight)
{
    auto p = makeProc (kFs, 256);
    check (p->smartListen (Source::Vocal), "listen starts a capture");
    auto in = voiceSignal (13.5);
    render (*p, in, 256); // the capture completes after 12 s of audio

    for (int i = 0; i < 1000 && p->getSmartStart().getPhase() != SmartStart::Phase::Done; ++i)
    {
        p->servicePendingWork();
        juce::Thread::sleep (10);
    }
    check (p->getSmartStart().getPhase() == SmartStart::Phase::Done, "the analysis finishes and is applied");
    check (p->getSmartStart().lastResult().valid, "the result is valid");
    check (std::abs (p->param (P::hpOn)->getValue() - 1.0f) < 1.0e-6f, "the high-pass is switched on by the result");
    check (p->getFocus().size() > 0 && std::find (p->getFocus().begin(), p->getFocus().end(), (int) P::hpFreq) != p->getFocus().end(),
           "Spotlight lists the changed parameters");

    // One undo step restores every change of the analysis.
    p->undo();
    const float hpOnBack = p->param (P::hpOn)->convertFrom0to1 (p->param (P::hpOn)->getValue());
    const float inBack = p->param (P::inGain)->convertFrom0to1 (p->param (P::inGain)->getValue());
    check (hpOnBack < 0.5f && std::abs (inBack) < 1.0e-4f, "one undo step reverts the whole Smart Start result",
           "hp " + juce::String (hpOnBack, 1) + ", in " + juce::String (inBack, 2));
}

//==============================================================================
// Compare (9.3)

NL_TEST (compare_whole_strip_is_loudness_matched_through_learned_trim)
{
    auto p = makeProc (kFs, 256);
    setParam (*p, P::compOn, 1);
    setParam (*p, P::compThresh, -30);
    setParam (*p, P::compRatio, 8);
    setParam (*p, P::matchMode, 1); // Learn

    // A steady tone: the compressor's gain reduction settles, so the learned trim matches the level exactly.
    // (Pink noise would change the spectrum under compression, and Level Match judges loudness, not RMS.)
    auto in = sine (kFs, 1000.0, 0.1f, (int) (14.0 * kFs));
    render (*p, in, 256);
    check (p->getLevelMatchStatus().state == LevelMatchStatus::Learned, "Level Match learns the processed loudness");
    p->servicePendingWork(); // the message-thread timer writes the learned trim to match_trim

    // Compare off: the processed output, matched by the learned trim. Compare on: the input path.
    p->setCompare (-1);
    const auto outA = render (*p, in, 256);
    const float levelA = rmsDbRange (outA, (int) (12.0 * kFs), (int) (14.0 * kFs));
    p->setCompare (0);
    const auto outB = render (*p, in, 256);
    const float levelB = rmsDbRange (outB, (int) (12.0 * kFs), (int) (14.0 * kFs));
    check (std::abs (levelA - levelB) <= 0.5f, "Compare on the whole strip is loudness-matched to the processed output within 0.5 dB",
           juce::String (levelA, 2) + " dB against " + juce::String (levelB, 2) + " dB");
}

NL_TEST (compare_switch_is_click_free_and_latency_constant)
{
    auto p = makeProc (kFs, 256);
    setParam (*p, P::compOn, 1);
    setParam (*p, P::compThresh, -30);
    setParam (*p, P::compRatio, 4);

    juce::AudioBuffer<float> in = sine (kFs, 1000.0, 0.1f, (int) (2.0 * kFs));
    const int latency = p->getStrip().getLatencySamples();
    const int s0 = 188 * 256, s1 = 240 * 256;
    auto out = renderSwitching (*p, in, 256, { { s0, 0 }, { s1, -1 } });

    float steady = 0.0f, transition = 0.0f;
    const auto diffMax = [&] (int from, int to)
    {
        float m = 0.0f;
        for (int i = std::max (1, from); i < to; ++i) m = std::max (m, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        return m;
    };
    steady = std::max ({ diffMax (s0 - 9600, s0 - 480), diffMax (s0 + 4800, s1 - 480), diffMax (s1 + 2400, s1 + 9600) });
    transition = std::max (diffMax (s0 - 480, s0 + 1440), diffMax (s1 - 480, s1 + 1440));
    check (transition <= 1.2f * steady + 1.0e-6f, "Compare switches are click-free (no step larger than the signal's own slope)",
           juce::String (transition, 6) + " against " + juce::String (steady, 6));

    check (p->getStrip().getLatencySamples() == latency, "Compare does not change the latency");

    // Constant latency: an impulse arrives at the same sample with Compare off and on (HQ, all modules bypassed).
    auto impulseIndex = [&] (int target)
    {
        auto q = makeProc (kFs, 256);
        setParam (*q, P::quality, 1);
        q->setCompare (target);
        juce::AudioBuffer<float> imp (2, 8192);
        imp.clear();
        imp.setSample (0, 1000, 0.5f);
        imp.setSample (1, 1000, 0.5f);
        const auto o = render (*q, imp, 256);
        int best = 0;
        for (int i = 0; i < o.getNumSamples(); ++i)
            if (std::abs (o.getSample (0, i)) > std::abs (o.getSample (0, best))) best = i;
        return best;
    };
    const int iOff = impulseIndex (-1), iOn = impulseIndex (0);
    check (iOff == iOn, "the impulse arrives at the same sample with Compare off and on", juce::String (iOff) + " and " + juce::String (iOn));
}

NL_TEST (compare_module_is_loudness_matched_through_learned_level)
{
    auto p = makeProc (kFs, 256);
    setParam (*p, P::eqToneFreq, 1000);
    setParam (*p, P::eqToneGain, 6);
    setParam (*p, P::eqToneQ, 1.0f);
    auto in = sine (kFs, 1000.0, 0.1f, (int) (3.0 * kFs));
    render (*p, in, 256); // learns the EQ's level at 1 kHz
    const float levelA = rmsDbRange (render (*p, in, 256), 0, (int) kFs);
    p->setCompare (1 + modEq);
    const float levelB = rmsDbRange (render (*p, in, 256), 0, (int) kFs);
    check (std::abs (levelA - levelB) <= 0.5f, "Compare on the EQ alone is loudness-matched within 0.5 dB",
           juce::String (levelA, 2) + " against " + juce::String (levelB, 2) + " dB");
}

//==============================================================================
// Delta (9.3)

NL_TEST (delta_module_off_gives_silence)
{
    auto p = makeProc (kFs, 256);
    setParam (*p, P::preOn, 0);
    p->setDelta (modPre, true);
    auto in = pinkNoise ((int) kFs, -12.0f, 9);
    const auto out = render (*p, in, 256);
    check (rmsDbRange (out, (int) (0.5 * kFs), (int) kFs) < -150.0f, "with the module off, Delta is silence",
           juce::String (rmsDbRange (out, (int) (0.5 * kFs), (int) kFs), 1) + " dB");
    check (p->getDelta (modPre), "the Delta state is reported");
}

NL_TEST (delta_output_minus_delta_is_input)
{
    // Module on: wet output. Delta on: wet minus the latency-aligned input. Wet minus Delta is the input.
    const auto configure = [] (NorthlineProcessor& q, int module)
    {
        if (module == modPre)
        {
            setParam (q, P::satOn, 1);
            setParam (q, P::satType, 0);
            setParam (q, P::satDrive, 60);
        }
        else
        {
            setParam (q, P::eqToneFreq, 1000);
            setParam (q, P::eqToneGain, 6);
            setParam (q, P::eqToneQ, 1.0f);
        }
    };
    for (int module : { (int) modPre, (int) modEq })
    {
        auto plain = makeProc (kFs, 256);
        auto withDelta = makeProc (kFs, 256);
        configure (*plain, module);
        configure (*withDelta, module);
        withDelta->setDelta (module, true);

        auto in = sine (kFs, 1000.0, 0.2f, (int) (1.5 * kFs));
        const auto yWet = render (*plain, in, 256);
        const auto yDelta = render (*withDelta, in, 256);
        const int latency = plain->getStrip().getLatencySamples();

        float worst = 0.0f;
        for (int i = (int) (0.5 * kFs); i < in.getNumSamples(); ++i)
        {
            const float x = i - latency >= 0 ? in.getSample (0, i - latency) : 0.0f;
            worst = std::max (worst, std::abs (yWet.getSample (0, i) - yDelta.getSample (0, i) - x));
        }
        check (worst < 1.0e-4f, juce::String ("Delta: output minus Delta is the input (") + moduleName (module) + ")",
               "max error " + juce::String (worst, 7));
    }
}

// Development aid, not a behaviour check: with NL_SNAPSHOT_DIR set, runs a real Listen and writes the
// Smart Start panel to smart_start.png in that folder, for a visual check. Does nothing otherwise.
NL_TEST (smart_panel_snapshot_for_review)
{
    const char* dir = std::getenv ("NL_SNAPSHOT_DIR");
    if (dir == nullptr) return;
    auto p = makeProc (kFs, 256);
    p->smartListen (Source::Vocal);
    render (*p, voiceSignal (13.5), 256);
    for (int i = 0; i < 1000 && p->getSmartStart().getPhase() != SmartStart::Phase::Done; ++i)
    {
        p->servicePendingWork();
        juce::Thread::sleep (10);
    }
    SmartStartPanel panel (*p);
    panel.setSize (760, 640);
    writeSnapshot (panel, juce::File (dir).getChildFile ("smart_start.png"));
    check (true, "smart start panel snapshot written");
}

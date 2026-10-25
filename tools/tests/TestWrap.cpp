// Counter wrap-around. Placed just below 2^31 and 2^32 with the tool-only seams (ControlClock::seedForTest,
// MeterHub::seedCountersForTest, ChannelStrip::seedClockForTest / seedCountersForTest): the control clock, the meter
// snapshot sequence and seqlock, the gain-reduction history point count, the ring generation and the parameter
// update counters. The output and the meter snapshots must match an unseeded run exactly, and the counters must keep
// counting across the wrap.

#include "MasterSignals.h"

using namespace nl;

namespace
{
using Buf = juce::AudioBuffer<float>;

double maxAbsDiff (const Buf& a, const Buf& b)
{
    double worst = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            worst = std::max (worst, std::abs ((double) a.getSample (c, i) - (double) b.getSample (c, i)));
    return worst;
}

// Every snapshot field except the sequence number (which is expected to differ by the seed offset).
bool sameFields (const MeterSnapshot& a, const MeterSnapshot& b)
{
    return a.momentary == b.momentary && a.shortTerm == b.shortTerm && a.integrated == b.integrated && a.maxMomentary == b.maxMomentary
        && a.maxShortTerm == b.maxShortTerm && a.lra == b.lra && a.lraLow == b.lraLow && a.lraHigh == b.lraHigh && a.truePeakDb == b.truePeakDb
        && a.samplePeakDb == b.samplePeakDb && a.samplePeakLDb == b.samplePeakLDb && a.samplePeakRDb == b.samplePeakRDb
        && a.correlation == b.correlation && a.vuDb == b.vuDb && a.overs == b.overs && a.plr == b.plr && a.integrating == b.integrating;
}
} // namespace

// The clock is seeded at 0x7fffffc0, which crosses 2^31 mid-render, and at 0xffffffc0, which crosses 2^32 within the first
// 64-sample block. Both are multiples of kControlBlock, so the phase is unchanged. The output must equal the unseeded render.
NL_TEST (wrap_clockSeededAcrossTheWrap)
{
    Buf in (2, 16384);
    juce::Random rng (5);
    tool::fillNoise (in, rng, 0.3f);
    for (int everything = 0; everything < 2; ++everything)
    {
        auto setup = [everything] (NorthlineProcessor& p)
        {
            if (everything != 0) tool::configureEverything (p, false, false);
        };
        auto ref = tool::makeProc (48000.0, 64);
        setup (*ref);
        const Buf reference = tool::render (*ref, in, 64);
        for (uint32_t seed : { 0x7fffffc0u, 0xffffffc0u })
        {
            auto p = tool::makeProc (48000.0, 64);
            setup (*p);
            p->getStrip().seedClockForTest (seed);
            const Buf out = tool::render (*p, in, 64);
            const double d = maxAbsDiff (out, reference);
            tool::check (d == 0.0,
                         juce::String ("clock seeded at ") + juce::String ((juce::int64) seed) + (everything != 0 ? ", everything on" : ", default")
                             + ": output identical across the wrap",
                         "max difference " + juce::String (d, 12));
        }
    }
}

// The snapshot sequence (advanced once per publish, i.e. per 64-sample block here) and the seqlock word are seeded
// 2 and 4 steps short of 2^32. Every field but the sequence must match an unseeded hub, and the sequence must
// count on modulo 2^32 (0xfffffffe, 0xffffffff, 0, 1, ...).
NL_TEST (wrap_meterSnapshotsAcrossTheWrap)
{
    const double fs = 48000.0;
    const Buf sig = tool::master::tones (fs, { { -18.0, 0.4 }, { -40.0, 0.3 }, { tool::master::kSilence, 0.1 }, { -3.0, 0.4 } });
    const int n = sig.getNumSamples();

    auto run = [&] (bool seeded, std::vector<MeterSnapshot>& snaps)
    {
        auto hub = tool::master::makeHub (fs, 64);
        if (seeded) hub->seedCountersForTest (0xfffffffdu, 0xfffffff0u, 0, 0);
        const float* ch[2] { sig.getReadPointer (0), sig.getReadPointer (1) };
        for (int pos = 0; pos < n; pos += 64)
        {
            const float* p[2] { ch[0] + pos, ch[1] + pos };
            hub->process (p, 2, juce::jmin (64, n - pos));
            snaps.push_back (hub->read());
        }
    };
    std::vector<MeterSnapshot> ref, seeded;
    run (false, ref);
    run (true, seeded);

    bool fields = ref.size() == seeded.size(), sequence = true;
    for (size_t k = 0; fields && k < ref.size(); ++k)
    {
        fields = sameFields (ref[k], seeded[k]);
        sequence = sequence && seeded[k].sequence == (uint32_t) (0xfffffffdu + (uint32_t) k + 1u);
    }
    tool::check (fields, "meter snapshots are identical to an unseeded hub across the sequence wrap",
                 juce::String ((int) ref.size()) + " blocks");
    tool::check (sequence, "snapshot sequence counts on modulo 2^32",
                 juce::String ((juce::int64) seeded.front().sequence) + " then ... " + juce::String ((juce::int64) seeded.back().sequence));
}

// The gain-reduction history is a ring of kGrHistoryPoints indexed by a 64-bit point count. The count is seeded at
// 2^32 - 50 and passes 2^32 here. The newest points must come back in order, in the right ring slots.
NL_TEST (wrap_gainReductionHistoryAcrossTheWrap)
{
    auto hub = tool::master::makeHub (48000.0, 512); // one point per 480 samples (10 ms)
    hub->seedCountersForTest (0, 0, (uint64_t) 0xffffffffu - 49u, 0);
    const int pushes = 120;
    std::vector<float> values;
    for (int i = 0; i < pushes; ++i)
    {
        values.push_back (-0.1f * (float) (i + 1));
        hub->pushGainReduction (values.back(), 480);
    }
    std::vector<float> hist ((size_t) MeterHub::kGrHistoryPoints);
    const int got = hub->readGrHistory (hist.data(), MeterHub::kGrHistoryPoints);
    bool ok = got == MeterHub::kGrHistoryPoints;
    for (int k = 0; ok && k < 100; ++k)
        ok = hist[(size_t) (MeterHub::kGrHistoryPoints - 100 + k)] == values[(size_t) (pushes - 100 + k)];
    tool::check (ok, "gain-reduction history returns the newest points in order across the 2^32 point count",
                 "got " + juce::String (got) + " points");
}

// The integration's sub-block count saturates at 2^31 - 1. Seeded 31 sub-blocks short of that, a 4 s steady tone crosses
// the saturation point, and the integrated and momentary loudness must still read the tone level (-23 LUFS).
NL_TEST (wrap_integrationSaturatesAt2e31)
{
    const double fs = 48000.0;
    const Buf sig = tool::master::tones (fs, { { -23.0, 4.0 } });
    auto hub = tool::master::makeHub (fs, 512);
    hub->seedCountersForTest (0, 0, 0, 0x7fffffe0u);
    tool::master::feed (*hub, sig, 512);
    const MeterSnapshot s = hub->read();
    tool::check (std::abs (s.integrated + 23.0) < 0.2 && std::abs (s.momentary + 23.0) < 0.2,
                 "integration past the 2^31 sub-block count still reads the tone level",
                 "I " + juce::String (s.integrated, 2) + ", M " + juce::String (s.momentary, 2));
}

// The ring generation (bumped by prepare) is seeded at 0xffffffff, so the next prepare wraps it to 0. A reader must
// still see the change once, and only once.
NL_TEST (wrap_ringGenerationAcrossTheWrap)
{
    auto p = tool::makeProc (48000.0, 64);
    auto& strip = p->getStrip();
    strip.seedCountersForTest (0xffffffffu, 0);
    MeterFrame frames[4];
    bool wasReset = false;
    (void) strip.popMeterFrames (frames, 4, &wasReset); // the reader records generation 0xffffffff
    p->prepareToPlay (48000.0, 64);                     // prepare bumps the generation: 0xffffffff -> 0
    (void) strip.popMeterFrames (frames, 4, &wasReset);
    tool::check (wasReset, "a reader sees the generation change when it wraps from 0xffffffff to 0", "");
    (void) strip.popMeterFrames (frames, 4, &wasReset);
    tool::check (! wasReset, "...and does not report a reset again afterwards", "");
}

// The compressor's parameter update counter is seeded 16 short of 2^32. The threshold changes on every one of 20 blocks,
// so the counter must advance by exactly 20 and carry on past the wrap.
NL_TEST (wrap_paramUpdateCountAcrossTheWrap)
{
    auto p = tool::makeProc (48000.0, 64);
    auto& strip = p->getStrip();
    tool::setParam (*p, P::compOn, 1);
    tool::setParam (*p, P::compThresh, -10.0f);
    Buf in (2, 64);
    juce::Random rng (3);
    tool::fillNoise (in, rng, 0.1f);
    tool::render (*p, in, 64); // settles at -10 dB, not counted
    strip.seedCountersForTest (0, 0xfffffff0u);
    const uint32_t before = strip.getParamUpdateCount (modComp);
    const int blocks = 20;
    for (int b = 0; b < blocks; ++b)
    {
        tool::setParam (*p, P::compThresh, (b % 2) != 0 ? -20.0f : -30.0f);
        tool::render (*p, in, 64);
    }
    const uint32_t after = strip.getParamUpdateCount (modComp);
    tool::check ((uint32_t) (after - before) == (uint32_t) blocks && after < before,
                 "the parameter update count passes 2^32 and keeps counting",
                 "before " + juce::String ((juce::int64) before) + ", after " + juce::String ((juce::int64) after));
}

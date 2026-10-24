// Master column UI (roadmap 8.4 meters, 8.5 MASTER row, 7.7 UI side): loudness target maths,
// meter scales, the Level Match text, the vectorscope ring and its decimated feed, the column layout.
#include "MasterSignals.h"
#include "gui/MasterPanel.h"
#include <cmath>
#include <iterator>
#include <limits>

using namespace nl;

namespace tool
{
using namespace master;

namespace
{
constexpr double kFs = 48000.0;

bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

// Sample values that are exact in float, so the decimated feed can be compared bit for bit.
float ramp (int i) { return 1.0e-4f * (float) i; }
} // namespace

//==============================================================================
NL_TEST (masterUi_targetDifferenceMaths)
{
    check (near (loudness::differenceLu (-16.0, -14.0), -2.0, 1e-12), "integrated minus target, LU (below the target is negative)");
    check (near (loudness::differenceLu (-13.5, -14.0), 0.5, 1e-12), "integrated minus target, LU (above the target is positive)");
    check (std::isnan (loudness::differenceLu (-200.0, -14.0)), "no reading gives no difference");
    check (loudness::integratedStatus (-14.9, -14.0) == loudness::Status::OnTarget, "within 1 LU of the target reads on target");
    check (loudness::integratedStatus (-15.1, -14.0) == loudness::Status::OffTarget, "beyond 1 LU reads off target");
    check (loudness::integratedStatus (-200.0, -14.0) == loudness::Status::NoReading, "silence has no status");
    check (loudness::truePeakOver (-0.9, -1.0), "true peak above the ceiling is an over");
    check (! loudness::truePeakOver (-0.97, -1.0), "true peak within the 0.05 dB slack is not an over");
    check (! loudness::truePeakOver (-200.0, -1.0), "no true-peak reading is not an over");

    check (loudness::kNumTargets == 6 && loudness::kCustomTarget == 5, "six targets, Custom last");
    const auto& t = loudness::kTargets;
    check (near (t[0].integratedLufs, -14.0, 1e-12) && near (t[0].truePeakDb, -1.0, 1e-12), "Streaming is -14 LUFS / -1 dBTP");
    check (near (t[1].integratedLufs, -16.0, 1e-12) && near (t[1].truePeakDb, -1.0, 1e-12), "Apple Music is -16 LUFS / -1 dBTP");
    check (near (t[2].integratedLufs, -16.0, 1e-12) && near (t[2].truePeakDb, -1.0, 1e-12), "Podcast is -16 LUFS / -1 dBTP");
    check (near (t[3].integratedLufs, -23.0, 1e-12) && near (t[3].truePeakDb, -1.0, 1e-12), "EBU R128 is -23 LUFS / -1 dBTP");
    check (near (t[4].integratedLufs, -24.0, 1e-12) && near (t[4].truePeakDb, -2.0, 1e-12), "ATSC A/85 is -24 LUFS / -2 dBTP");

    check (near (loudness::clampCustom (-14.2), -14.0, 1e-12) && near (loudness::clampCustom (-14.3), -14.5, 1e-12), "custom target snaps to 0.5 LU");
    check (near (loudness::clampCustom (-100.0), loudness::kCustomMinLufs, 1e-12) && near (loudness::clampCustom (3.0), loudness::kCustomMaxLufs, 1e-12),
           "custom target is clamped to its range");
}

NL_TEST (masterUi_levelMatchStatusText)
{
    LevelMatchStatus s;
    check (levelmatch::text (s) == "Idle", "Level Match idle");
    s.state = LevelMatchStatus::Learning;
    s.progress = 0.42f;
    check (levelmatch::text (s) == "Learning 42 %", "Level Match learning shows progress", levelmatch::text (s));
    s.state = LevelMatchStatus::Learned;
    s.learnedTrimDb = 1.5f;
    check (levelmatch::text (s) == "Learned 1.5 dB", "Level Match learned shows the trim", levelmatch::text (s));
    s.state = LevelMatchStatus::Live;
    s.gainDb = -0.4f;
    check (levelmatch::text (s) == "Live -0.4 dB", "Level Match live shows the applied gain", levelmatch::text (s));
    check (levelMatchColour (LevelMatchStatus::Learning) == colours::amber, "Level Match is amber while learning");
    check (levelMatchColour (LevelMatchStatus::Learning) != colours::meterRed, "...and never red");
}

NL_TEST (masterUi_meterScalesAndGrHistory)
{
    check (meter::levelFraction (3.0f) == 1.0f && meter::levelFraction (-60.0f) == 0.0f, "level scale runs from -60 dB to +3 dB");
    check (near (meter::levelFraction (-30.0f), 30.0 / 63.0, 1e-6), "level scale is linear in dB");
    check (meter::levelFraction (-100.0f) == 0.0f && meter::levelFraction (10.0f) == 1.0f, "level scale clamps");
    check (meter::grFraction (-20.0f) == 1.0f && meter::grFraction (-5.0f) == 0.25f && meter::grFraction (0.0f) == 0.0f, "GR bar runs 0 to -20 dB");
    check (meter::grFraction (-40.0f) == 1.0f, "GR deeper than the bar stays at the end");
    const float ticks[] { 1.0f, 3.0f, 6.0f, 10.0f, 20.0f };
    bool ticksOk = true;
    for (int i = 0; i < 5; ++i) ticksOk &= meter::kGrTicksDb[i] == ticks[i];
    check (ticksOk, "GR ticks at 1, 3, 6, 10 and 20 dB");

    const float pts[] { 0.0f, -1.0f, -5.0f, -2.0f, 0.0f, -3.0f };
    float cols[3] {};
    meter::columnMinima (pts, 6, cols, 3);
    check (cols[0] == -1.0f && cols[1] == -5.0f && cols[2] == -3.0f, "GR history keeps the deepest value of each column", juce::String (cols[0]) + " " + juce::String (cols[1]) + " " + juce::String (cols[2]));
    float none[2] { 9.0f, 9.0f };
    meter::columnMinima (pts, 0, none, 2);
    check (none[0] == 0.0f && none[1] == 0.0f, "an empty history reads 0");
}

NL_TEST (masterUi_vectorscopeMapping)
{
    const auto mono = meter::scopePoint (0.3f, 0.3f);
    check (mono.x == 0.0f && near (mono.y, 0.3, 1e-6), "mono is a vertical line on the mid axis");
    const auto left = meter::scopePoint (1.0f, 0.0f);
    check (near (left.x, -0.5, 1e-6) && near (left.y, 0.5, 1e-6), "left only sits on the upper-left diagonal, half way out");
    const auto out = meter::scopePoint (1.0f, -1.0f);
    check (near (out.x, -1.0, 1e-6) && near (out.y, 0.0, 1e-6), "full anti-phase is on the side axis at the edge");
}

//==============================================================================
NL_TEST (masterUi_pairRingWrapAndOverrun)
{
    PairRing<8> ring;
    ScopePair out[64];

    // Wrap: 20 pairs through an 8-slot ring, read in batches, in order and nothing lost.
    std::vector<float> seen;
    for (int k = 0; k < 20; ++k)
    {
        ring.push ((float) k, -(float) k);
        if (k % 5 == 4)
        {
            const size_t got = ring.pop (out, 64);
            for (size_t i = 0; i < got; ++i) seen.push_back (out[i].l);
        }
    }
    bool ordered = seen.size() == 20;
    for (size_t i = 0; ordered && i < seen.size(); ++i) ordered = seen[i] == (float) i;
    check (ordered && ring.droppedCount() == 0, "ring wraps around and keeps order when read in time");

    // Overrun: 20 pairs with nothing read drops the 12 oldest and keeps the newest 8.
    PairRing<8> over;
    for (int k = 0; k < 20; ++k) over.push ((float) k, 0.0f);
    const size_t got = over.pop (out, 64);
    check (got == 8 && out[0].l == 12.0f && out[7].l == 19.0f, "overrun drops the oldest pairs", juce::String ((int) got) + " pairs, first " + juce::String (out[0].l));
    check (over.droppedCount() == 12, "dropped pairs are counted", juce::String ((int) over.droppedCount()));
    check (over.pop (out, 64) == 0, "nothing is left after the overrun is read");

    // A short read leaves the rest for the next call.
    PairRing<8> part;
    for (int k = 0; k < 3; ++k) part.push ((float) k, 0.0f);
    const size_t first = part.pop (out, 2);
    const size_t second = part.pop (out, 10);
    check (first == 2 && second == 1 && out[0].l == 2.0f, "a partial read leaves the remaining pairs");
}

NL_TEST (masterUi_pairRingDoesNotAllocate)
{
    PairRing<256> ring;
    ScopePair out[64];
    allocationCount = 0;
    countAllocations = true;
    for (int k = 0; k < 2000; ++k)
    {
        ring.push ((float) k, 1.0f);
        if (k % 37 == 0) (void) ring.pop (out, 64);
    }
    countAllocations = false;
    check (allocationCount.load() == 0, "pair ring does not allocate in push or pop", juce::String (allocationCount.load()) + " allocations");
}

//==============================================================================
// The MeterHub feed is exactly every fourth output pair, whatever the block sizes.
NL_TEST (masterUi_vectorscopeFeedIsDecimatedSamples)
{
    auto hub = makeHub (kFs, 512);
    const int n = 4000; // a multiple of the decimation, so the count is exact
    Buf b (2, n);
    for (int i = 0; i < n; ++i)
    {
        b.setSample (0, i, ramp (i));
        b.setSample (1, i, -0.5f * ramp (i));
    }
    b.setSample (0, 8, std::numeric_limits<float>::quiet_NaN()); // non-finite input measures as silence

    const int sizes[] { 37, 64, 1, 512, 250 };
    int pos = 0, step = 0;
    while (pos < n)
    {
        const int len = std::min (sizes[step++ % 5], n - pos);
        feed (*hub, b, len, pos, pos + len);
        pos += len;
    }

    std::vector<ScopePair> pairs (4096);
    const size_t got = hub->scope().pop (pairs.data(), pairs.size());
    check (got == (size_t) (n / MeterHub::kScopeDecimation), "vectorscope feed holds one pair per 4 samples", juce::String ((int) got));

    int mismatches = 0;
    for (size_t k = 0; k < got && k < (size_t) n / 4; ++k)
    {
        const int i = (int) k * MeterHub::kScopeDecimation;
        const float expectL = i == 8 ? 0.0f : ramp (i);
        const float expectR = -0.5f * ramp (i);
        if (pairs[k].l != expectL || pairs[k].r != expectR) ++mismatches;
    }
    check (mismatches == 0, "every feed pair is the decimated output sample, bit for bit", juce::String (mismatches) + " mismatches");
}

NL_TEST (masterUi_scopeFeedDoesNotAllocate)
{
    auto hub = makeHub (kFs, 512);
    const Buf b = tones (kFs, { { -6, 0.2 } });
    ScopePair out[256];
    feed (*hub, b, 64, 0, 4096); // warm-up
    (void) hub->scope().pop (out, 256);
    allocationCount = 0;
    countAllocations = true;
    feed (*hub, b, 64, 4096, 4096 + 8192);
    (void) hub->scope().pop (out, 256);
    countAllocations = false;
    check (allocationCount.load() == 0, "meter feed and vectorscope read do not allocate", juce::String (allocationCount.load()) + " allocations");
}

NL_TEST (masterUi_meterHubOptionsAreReadable)
{
    auto hub = makeHub (kFs, 512);
    check (! hub->pauseIntegrationWhenStopped() && ! hub->resetIntegrationOnPlay(), "integration options default to off");
    hub->setPauseIntegrationWhenStopped (true);
    hub->setResetIntegrationOnPlay (true);
    check (hub->pauseIntegrationWhenStopped() && hub->resetIntegrationOnPlay(), "integration options read back what was set");
}

//==============================================================================
// The master column is 300 px wide at 100 %, 260 to 360 px in use, and 736 px high.
NL_TEST (masterUi_masterColumnLayoutFits)
{
    auto proc = makeProc();
    MasterPanel panel (*proc);
    const char* order[] { "gains", "choices", "meters", "limiter", "history", "match", "loudness" };
    for (int width : { 260, 300, 360 })
    {
        panel.setBounds (0, 0, width, 736);
        panel.resized();
        const auto& blocks = panel.layoutBlocks();
        auto find = [&] (const char* name) -> juce::Rectangle<int>
        {
            for (auto& b : blocks)
                if (b.name == name) return b.bounds;
            return {};
        };

        bool inside = true;
        for (auto& b : blocks) inside &= b.bounds.getX() >= 0 && b.bounds.getRight() <= width && b.bounds.getBottom() <= 736 - 8 && b.bounds.getY() >= 0;
        check (inside, juce::String ("every block of the column is inside a ") + juce::String (width) + " x 736 panel");

        bool stacked = true;
        for (int i = 0; i + 1 < (int) std::size (order); ++i)
            stacked &= find (order[i]).getBottom() <= find (order[i + 1]).getY();
        check (stacked, juce::String ("the blocks stack without overlap at ") + juce::String (width) + " px");

        const auto readouts = find ("readouts"), scope = find ("scope");
        check (readouts.getWidth() / 2 >= 60 && readouts.getRight() <= scope.getX(), juce::String ("readouts keep 60 px columns at ") + juce::String (width) + " px",
               juce::String (readouts.getWidth() / 2) + " px");
        check (find ("meters").getWidth() >= 120 && find ("meters").getWidth() <= width, juce::String ("meters and VU fit the width at ") + juce::String (width) + " px");
    }
}
} // namespace tool

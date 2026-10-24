#pragma once

#include "Widgets.h"
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

namespace nl
{
//==============================================================================
// Pure helpers (tested headlessly in tools/tests/TestMasterUi.cpp).
namespace meter
{
constexpr float kLevelTopDb = 3.0f, kLevelFloorDb = -60.0f; // level meters: +3 headroom down to -60
constexpr float kGrRangeDb = 20.0f;                          // gain reduction bar: 0 to -20 dB
constexpr float kGrTicksDb[] { 1.0f, 3.0f, 6.0f, 10.0f, 20.0f };
constexpr float kLevelTicksDb[] { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };

// Position of a level on the meter, 0 at the bottom, 1 at the top of the scale.
inline float levelFraction (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - kLevelFloorDb) / (kLevelTopDb - kLevelFloorDb));
}

// Gain-reduction depth (dB, <= 0) as a fraction of the bar, 0 at the zero end.
inline float grFraction (float db) noexcept { return juce::jlimit (0.0f, 1.0f, -db / kGrRangeDb); }

// Vectorscope position of a stereo pair: x = side (R - L), y = mid (L + R), each halved, so a
// full-scale pair stays inside the unit square and a mono signal is a vertical line.
inline juce::Point<float> scopePoint (float l, float r) noexcept { return { 0.5f * (r - l), 0.5f * (l + r) }; }

// Deepest (most negative) value of each of `numCols` slices of pts[0..n). Empty slices read 0.
inline void columnMinima (const float* pts, int n, float* cols, int numCols) noexcept
{
    for (int c = 0; c < numCols; ++c)
    {
        const int a = (int) ((long long) c * n / numCols);
        const int b = (int) ((long long) (c + 1) * n / numCols);
        float m = 0.0f;
        for (int i = a; i < b && i < n; ++i) m = std::min (m, pts[i]);
        cols[c] = m;
    }
}
} // namespace meter

namespace loudness
{
struct Target
{
    const char* name;
    double integratedLufs;
    double truePeakDb;
};

// Delivery targets. The last entry is Custom, whose integrated value the panel sets (see clampCustom).
inline constexpr Target kTargets[] {
    { "Streaming", -14.0, -1.0 },
    { "Apple Music", -16.0, -1.0 },
    { "Podcast", -16.0, -1.0 },
    { "EBU R128", -23.0, -1.0 },
    { "ATSC A/85", -24.0, -2.0 },
    { "Custom", -14.0, -1.0 },
};
inline constexpr int kNumTargets = (int) std::size (kTargets);
inline constexpr int kCustomTarget = kNumTargets - 1;
inline constexpr double kCustomDefaultLufs = -14.0, kCustomStepLu = 0.5, kCustomMinLufs = -40.0, kCustomMaxLufs = -5.0;
inline constexpr double kOnTargetLu = 1.0; // within this many LU, the integrated loudness reads as on target
inline constexpr double kTruePeakSlackDb = 0.05;

inline double clampCustom (double lufs) noexcept { return juce::jlimit (kCustomMinLufs, kCustomMaxLufs, std::round (lufs / kCustomStepLu) * kCustomStepLu); }

// The MeterHub reports -200 when there is no reading yet.
inline bool hasReading (double lufs) noexcept { return lufs > -199.0; }

// Integrated loudness minus the target, LU. NaN when there is no reading.
inline double differenceLu (double integratedLufs, double targetLufs) noexcept
{
    return hasReading (integratedLufs) ? integratedLufs - targetLufs : std::numeric_limits<double>::quiet_NaN();
}

enum class Status { NoReading, OnTarget, OffTarget };

inline Status integratedStatus (double integratedLufs, double targetLufs) noexcept
{
    if (! hasReading (integratedLufs)) return Status::NoReading;
    return std::abs (integratedLufs - targetLufs) <= kOnTargetLu ? Status::OnTarget : Status::OffTarget;
}

// True peak above the target ceiling (red: this is an over).
inline bool truePeakOver (double truePeakDb, double ceilingDb) noexcept
{
    return hasReading (truePeakDb) && truePeakDb > ceilingDb + kTruePeakSlackDb;
}
} // namespace loudness

namespace levelmatch
{
inline juce::String text (const LevelMatchStatus& s)
{
    switch (s.state)
    {
        case LevelMatchStatus::Learning: return "Learning " + juce::String (juce::roundToInt (s.progress * 100.0f)) + " %";
        case LevelMatchStatus::Learned: return "Learned " + juce::String (s.learnedTrimDb, 1) + " dB";
        case LevelMatchStatus::Live: return "Live " + juce::String (s.gainDb, 1) + " dB";
        case LevelMatchStatus::Idle:
        default: return "Idle";
    }
}
} // namespace levelmatch

//==============================================================================
// Components.

// Stereo peak meter with peak hold and RMS marker. Red only above 0 dBFS (clip), with a clip LED.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLevels (float peakL, float peakR, float rmsL, float rmsR);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setTitle (const juce::String& t) { title = t; }

private:
    float peakDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f }, rmsDb[2] { -100.0f, -100.0f };
    int holdFrames[2] {};
    bool clip = false;
    juce::String title;
};

// dB scale for the level meters, drawn beside them.
class MeterScale : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

// Gain reduction bar, 0 to -20 dB, ticks at 1/3/6/10/20 dB, the value at the zero end.
class GrMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setGr (float db);
    void paint (juce::Graphics&) override;

private:
    float value = 0.0f;
};

// VU needle referenced to the operating level: 0 VU is the operating level, in dBFS.
class VuFace : public juce::Component
{
public:
    void setValue (float vu);
    void setLabel (const juce::String& s);
    void setReference (float opLevelDbfs);
    void paint (juce::Graphics&) override;

private:
    float value = -20.0f, reference = -18.0f;
    juce::String label { "VU" };
};

// Gain reduction over the last 10 s, oldest on the left. Each pixel column shows its deepest value.
class GrHistoryView : public juce::Component
{
public:
    static constexpr int kMaxPoints = 1000;
    void setHistory (const float* pts, int n);
    void paint (juce::Graphics&) override;

private:
    std::array<float, kMaxPoints> points {};
    std::array<float, 512> columns {};
    int count = 0;
};

// Lissajous display, rotated 45 degrees, with persistence. The trail is an image that fades each
// update; points come from the MeterHub decimated feed.
class Vectorscope : public juce::Component, public juce::SettableTooltipClient
{
public:
    void resized() override;
    void pushPairs (const ScopePair* pairs, size_t n);
    void paint (juce::Graphics&) override;

private:
    juce::Image trail;
};

// Correlation from -1 to +1, centred. Negative (phase trouble) in amber, not red.
class CorrelationBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setValue (float v);
    void paint (juce::Graphics&) override;

private:
    float value = 0.0f;
};

// Level Match status: state text, progress while learning (amber), learned or live gain.
class LevelMatchView : public juce::Component
{
public:
    void setStatus (const LevelMatchStatus& s);
    void paint (juce::Graphics&) override;

private:
    LevelMatchStatus status;
};

// The loudness readouts: M, S, I, LRA, TP, PLR, overs, and I against the selected target.
class LoudnessReadouts : public juce::Component
{
public:
    struct Values
    {
        double momentary = -200.0, shortTerm = -200.0, integrated = -200.0, lra = 0.0, truePeakDb = -200.0, plr = 0.0;
        uint32_t overs = 0;
    };

    void setValues (const Values& v, const loudness::Target& target);
    void paint (juce::Graphics&) override;

private:
    Values values;
    double targetLufs = -14.0, ceilingDb = -1.0;
    juce::String targetName;
};

// Level Match text colour and loudness target colour, from the design tokens.
juce::Colour levelMatchColour (LevelMatchStatus::State state);
juce::Colour loudnessStatusColour (loudness::Status status);
} // namespace nl

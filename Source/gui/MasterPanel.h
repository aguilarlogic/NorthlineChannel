#pragma once

#include "MeterViews.h"

namespace nl
{
struct MeterSummary
{
    float inPeak[2] {}, outPeak[2] {}, inRms[2] {}, outRms[2] {};
    float gateGr = 0, ds1Gr = 0, ds2Gr = 0, compGr = 0, limGr = 0;
};

// The master column (300 px wide at 100 %, 260 to 360 px; 736 px high). It reads the MeterHub
// itself on a 30 Hz timer; the editor only feeds it the per-block meter frames (updateMeters).
class MasterPanel : public juce::Component, private juce::Timer
{
public:
    explicit MasterPanel (NorthlineProcessor& p);
    ~MasterPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Per-block levels from the editor (in/out peak and RMS, gain reduction per module).
    void updateMeters (const MeterSummary& m);

    // Compatibility: loudness now comes from the MeterHub, read by this panel. No-ops.
    void updateLoudness (double momentary, double shortTerm, double integrated, float truePeak, float correlation);
    std::function<void()> onResetLoudness;

    // Loudness target (stored in the panel until the session can hold it, see the integration request).
    int getTargetIndex() const noexcept { return targetIndex; }
    double getCustomTargetLufs() const noexcept { return customLufs; }

    // Named blocks of the last layout (for the layout test).
    struct Block
    {
        juce::String name;
        juce::Rectangle<int> bounds;
    };
    const std::vector<Block>& layoutBlocks() const noexcept { return blocks; }

private:
    void timerCallback() override;
    void layoutBlocksFrom (juce::Rectangle<int> r);
    void selectTarget (int index);
    void showOptionsMenu();
    int vuSourceIndex() const;
    float opLevelDb() const;

    template <typename T, typename... Args>
    T* add (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        return c;
    }

    NorthlineProcessor& proc;
    juce::OwnedArray<juce::Component> owned;
    Knob *inGain, *outGain, *vuRef, *ceiling, *limRelease, *matchTrim;
    Chip *limOn;
    ChoiceBox *phase, *lookahead, *match, *vuSource, *monitor;

    LevelMeter inMeter, outMeter;
    MeterScale meterScale;
    VuFace vu;
    GrMeter limGr;
    GrHistoryView grHistory;
    Vectorscope scope;
    CorrelationBar correlation;
    LevelMatchView matchView;
    LoudnessReadouts readouts;

    juce::ComboBox targetBox;
    juce::TextButton customDown { "-" }, customUp { "+" }, resetButton { "RESET" }, optionsButton { "OPTIONS" }, relearnButton { "RELEARN" };

    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
    std::vector<Block> blocks;
    juce::Rectangle<int> scopeArea;

    int targetIndex = 0;
    double customLufs = loudness::kCustomDefaultLufs;
    float vuValue = -20.0f;
    std::array<float, GrHistoryView::kMaxPoints> grBuf {};
    std::array<ScopePair, 256> scopeBuf {};
};
} // namespace nl

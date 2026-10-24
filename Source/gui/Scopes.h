#pragma once

#include "../PluginProcessor.h"
#include "../dsp/ScopeTap.h"
#include "LookAndFeel.h"
#include "Widgets.h"
#include <algorithm>
#include <array>

namespace nl
{
// Scrolling level scope of one module, fed from its ScopeTap(s) on the 5 ms grid. The newest cell is on
// the right and the window shows the last kWindowSeconds. The taps are drained by this component only.
//   Comp   : input and output levels, threshold line (draggable: drag or wheel), GR strip.
//   Gate   : key level, threshold and hysteresis lines, open spans shaded.
//   DeEss  : key level of both bands with their thresholds, GR strip for both bands.
class LevelScope : public juce::Component, private juce::Timer
{
public:
    enum class Kind { Comp, Gate, DeEss };

    LevelScope (NorthlineProcessor& p, Kind kind);
    ~LevelScope() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // The newest cell of the module's tap (all zero before any audio).
    const ScopePoint& latest() const noexcept { return last; }
    static constexpr float kWindowSeconds = 4.0f;

    // Sets the comp threshold from a dB value (used by the drag and the wheel; clamped to the parameter range).
    void setCompThreshold (float db);

private:
    static constexpr int kHistory = 1024;   // cells kept per tap (about 5 s)
    struct History
    {
        std::array<ScopePoint, kHistory> cells {};
        int head = 0, count = 0;
        void push (const ScopePoint& p) noexcept { cells[(size_t) head] = p; head = (head + 1) % kHistory; count = std::min (count + 1, kHistory); }
        const ScopePoint& ageOf (int age) const noexcept { return cells[(size_t) ((head - 1 - age + 2 * kHistory) % kHistory)]; }
    };

    void timerCallback() override;
    void drainTap (ScopeTap& tap, History& h);
    float yForDb (float db) const noexcept;
    float dbForY (float y) const noexcept;
    void paintBackground();
    void paintGrStrip (juce::Graphics& g, const History& h, juce::Colour c, juce::Rectangle<float> strip);
    void paintLines (juce::Graphics& g, float db, juce::Colour c, const juce::String& label, bool dashed);

    NorthlineProcessor& proc;
    Kind kind;
    ScopeTap* tapA = nullptr;
    ScopeTap* tapB = nullptr;
    History histA, histB;
    ScopePoint last;
    float floorDb = -60.0f;
    bool dragging = false;
    float dragStartDb = 0.0f;
    juce::Rectangle<float> plot, gr;
    juce::Image background;
};

// Static gain curve of the compressor (input level to output level) with the operating point taken from
// the scope's newest cell.
class CompCurve : public juce::Component, private juce::Timer
{
public:
    CompCurve (NorthlineProcessor& p, const LevelScope& scope);
    ~CompCurve() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    NorthlineProcessor& proc;
    const LevelScope& scope;
};
} // namespace nl

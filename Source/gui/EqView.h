#pragma once

#include "../PluginProcessor.h"
#include "Analyser.h"
#include "LookAndFeel.h"

namespace nl
{
//==============================================================================
// Display geometry and handle mapping. Pure functions, shared by the drawing, the mouse handling and the tests.
struct EqGeometry
{
    float width = 1.0f, height = 1.0f;

    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr float kRangeDb = 18.0f; // band gain range

    float xForFreq (double hz) const noexcept;
    double freqForX (float x) const noexcept; // clamped to 20 Hz .. 20 kHz
    float yForDb (float db) const noexcept;   // +18 dB at the top, -18 dB at the bottom
    float dbForY (float y) const noexcept;    // clamped to +-18 dB
};

// Handle position of a band (frequency, gain) and the band values under a handle position.
juce::Point<float> handleFor (const EqGeometry& g, double freqHz, float gainDb) noexcept;
struct BandValues { double freqHz; float gainDb; };
BandValues valuesAt (const EqGeometry& g, juce::Point<float> p) noexcept;

// Q after one wheel notch: each notch scales Q by 2^(+-0.25), or by 2^(+-0.05) with Shift. Limited to the
// parameter range 0.2 .. 10.
float qAfterWheel (float q, float wheelDelta, bool fine) noexcept;

//==============================================================================
// The EQ's large display: the static curve (Mid and Side in M/S mode), the live curve with the dynamic
// band gains, Q rings and draggable band handles, and the pre / post analyser behind them.
//   drag: frequency and gain (Shift: fine), wheel: Q (Shift: fine), double-click: gain to 0 dB.
class EqView : public juce::Component, private juce::Timer
{
public:
    EqView (NorthlineProcessor& p, EqAnalyser& analyser);
    ~EqView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Shows or hides the analyser overlay (the analyser thread is controlled by the owner).
    void setAnalyserShown (bool shown);

private:
    void timerCallback() override;
    void rebuild();
    void rebuildAnalyser();
    EqGeometry geometry() const noexcept;
    int nearestHandle (juce::Point<float> p, float maxDistance) const noexcept;
    void beginBand (int b);
    void endBand (int b);
    void setBand (int band, int field, float value);

    NorthlineProcessor& proc;
    EqAnalyser& analyser;
    bool analyserShown = false;
    uint32_t analyserFrames = 0;
    std::vector<float> lastValues;

    juce::Path staticMid, staticSide, liveMid, liveSide, fillMid;
    juce::Path preCurve, postCurve, holdCurve;
    std::array<juce::Point<float>, 4> handles {};
    std::array<bool, 4> handleOn {};
    std::array<float, 4> handleQ {};
    bool msMode = false;
    int dragBand = -1;
};
} // namespace nl

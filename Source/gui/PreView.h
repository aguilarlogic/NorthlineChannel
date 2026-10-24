#pragma once

#include "../PluginProcessor.h"
#include "../dsp/PreModule.h"
#include "Analyser.h"
#include "LookAndFeel.h"

namespace nl
{
// Static transfer curve of the saturation section at the given drive, type, tone, auto gain and operating
// level: output level against input level for a 1 kHz sine from -60 to 0 dBFS (1 dB steps). It runs a
// private engine (sat::State and sat::Coefs), never the audio thread's.
struct TransferCurve
{
    static constexpr int kPoints = 61;
    float inDb[kPoints] {}, outDb[kPoints] {};
};
void computeTransferCurve (const PreParams& pp, double fs, TransferCurve& out);

// Magnitude in dB of the PRE high-pass and low-pass (the ones switched on) at each frequency.
void computeFilterCurveDb (const PreParams& pp, double fs, const double* freqHz, float* outDb, int n);

// PRE's display: transfer curve, live harmonic bars (from the PRE output tap, on the analyser thread) and
// the high-pass / low-pass curve. Owned by PrePanel; the harmonic analyser is owned there too.
class PreDisplay : public juce::Component, private juce::Timer
{
public:
    PreDisplay (NorthlineProcessor& p, HarmonicAnalyser& harmonics);
    ~PreDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void recomputeCurves (const PreParams& pp);
    void paintTransfer (juce::Graphics&, juce::Rectangle<float>) const;
    void paintHarmonics (juce::Graphics&, juce::Rectangle<float>) const;
    void paintFilters (juce::Graphics&, juce::Rectangle<float>) const;

    NorthlineProcessor& proc;
    HarmonicAnalyser& harmonics;
    PreParams lastPre;
    bool haveCurves = false;
    uint32_t lastRecompute = 0;
    bool pendingRecompute = false;
    TransferCurve transfer;
    std::vector<float> filterDb;
    std::array<float, 1 + spectrum::kHarmonics> levels {};
    bool haveLevels = false;
    juce::Rectangle<float> transferArea, harmArea, filterArea;
};
} // namespace nl

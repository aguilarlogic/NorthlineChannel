#pragma once

#include "MasterPanel.h"
#include "ModulePanels.h"
#include "TopBar.h"

namespace nl
{
class NorthlineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NorthlineEditor (NorthlineProcessor&);
    ~NorthlineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1350, kBaseH = 640;

private:
    void timerCallback() override;
    void layoutModules();
    void applyScale();
    void updateFocus();

    NorthlineProcessor& proc;
    NorthlineLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content;
    TopBar topBar;
    std::array<std::unique_ptr<ModulePanel>, numModules> panels;
    MasterPanel master;

    LoudnessMeter loudness;
    double meterRate = 0.0;
    std::vector<MeterFrame> frameBuf;
    std::vector<StereoSample> sampleBuf;
    int lastGeneration = -1, tick = 0;
    ChannelStrip::Order lastOrder {};
    bool lastFocusMode = false;
    std::vector<int> lastFocus;
    float cpuAvg = 0.0f;
};
} // namespace nl

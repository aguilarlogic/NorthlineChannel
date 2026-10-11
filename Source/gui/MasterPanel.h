#pragma once

#include "Widgets.h"

namespace nl
{
struct MeterSummary
{
    float inPeak[2] {}, outPeak[2] {}, inRms[2] {}, outRms[2] {};
    float gateGr = 0, ds1Gr = 0, ds2Gr = 0, compGr = 0, limGr = 0;
};

class MasterPanel : public juce::Component
{
public:
    explicit MasterPanel (NorthlineProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

    void updateMeters (const MeterSummary& m);
    void updateLoudness (double momentary, double shortTerm, double integrated, float truePeak, float correlation);
    std::function<void()> onResetLoudness;

private:
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
    Knob *inGain, *outGain, *vuRef, *ceiling, *limRelease;
    Chip *limOn;
    ChoiceBox *phase, *lookahead, *match, *vuSource, *monitor;
    StereoMeter inMeter, outMeter;
    VuMeter vu;
    GrBar limGr { colours::master, 12.0f };
    juce::TextButton resetLoudness { "RESET" };
    juce::Rectangle<int> loudnessArea, meterLabelArea;
    juce::String lufsM { "-inf" }, lufsS { "-inf" }, lufsI { "-inf" }, tp { "-inf" };
    float corr = 0.0f, vuValue = -20.0f;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};
} // namespace nl

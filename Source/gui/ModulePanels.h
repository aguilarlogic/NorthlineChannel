#pragma once

#include "Widgets.h"

namespace nl
{
class ModulePanel : public juce::Component
{
public:
    ModulePanel (NorthlineProcessor& p, int moduleId, int onParam, int modeParam);
    ~ModulePanel() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    int getModuleId() const noexcept { return moduleId; }
    virtual int preferredWidth() const = 0;
    virtual void updateMeters (const MeterFrame&) {}
    void setCpuUs (float us, float blockUs);
    std::function<void (int moduleId, int delta)> onMove;

protected:
    virtual void layoutBody (juce::Rectangle<int> body) = 0;

    Knob* knob (int index, const juce::String& label);
    Chip* chip (int index, const juce::String& text);
    ChoiceBox* choice (int index);
    Segmented* segmented (int index);
    static void row (juce::Rectangle<int> r, std::initializer_list<juce::Component*> items, int gap = 4);
    juce::Rectangle<int> sectionLabel (juce::Rectangle<int>& body, const juce::String& text);

    NorthlineProcessor& proc;
    const int moduleId;
    const juce::Colour accent;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;

private:
    void updateBodyEnabled();

    std::unique_ptr<Chip> power;
    std::unique_ptr<Segmented> mode;
    juce::TextButton moveLeft { "<" }, moveRight { ">" };
    juce::OwnedArray<juce::Component> owned;
    juce::String cpuText;
    std::unique_ptr<juce::ParameterAttachment> onWatcher;
};

//==============================================================================
class PrePanel : public ModulePanel
{
public:
    explicit PrePanel (NorthlineProcessor& p);
    int preferredWidth() const override { return 214; }

private:
    void layoutBody (juce::Rectangle<int>) override;
    Chip *satOn, *hq, *hpOn, *lpOn, *thumpOn;
    ChoiceBox *satType, *hpSlope, *lpSlope, *target, *thumpAmt;
    Knob *drive, *hpFreq, *hpRes, *lpFreq, *lpRes;
};

class GatePanel : public ModulePanel
{
public:
    explicit GatePanel (NorthlineProcessor& p);
    int preferredWidth() const override { return 178; }
    void updateMeters (const MeterFrame& f) override { gr.setGr (f.gateGr); }

private:
    void layoutBody (juce::Rectangle<int>) override;
    Segmented* type;
    GrBar gr { colours::gate, 60.0f };
    Knob *thresh, *floorK, *hyst, *attack, *hold, *release, *ratio, *scHp, *scLp;
    Chip *ext, *listen;
};

class DsPanel : public ModulePanel
{
public:
    explicit DsPanel (NorthlineProcessor& p);
    int preferredWidth() const override { return 186; }
    void updateMeters (const MeterFrame& f) override { gr1.setGr (f.ds1Gr); gr2.setGr (f.ds2Gr); }

private:
    void layoutBody (juce::Rectangle<int>) override;
    Chip *on1, *on2, *relative, *ext;
    ChoiceBox *type1, *type2, *listen;
    Knob *freq1, *thresh1, *range1, *freq2, *thresh2, *range2, *release, *out;
    GrBar gr1 { colours::ds, 24.0f }, gr2 { colours::ds, 24.0f };
};

class EqDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (NorthlineProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    void rebuild();
    float pv (int index) const { return proc.param (index)->convertFrom0to1 (proc.param (index)->getValue()); }
    void setParam (int index, float plain);

    NorthlineProcessor& proc;
    juce::Path curve, fill;
    std::array<juce::Path, 4> bandCurves;
    std::array<juce::Point<float>, 4> handles {};
    std::vector<float> lastValues;
    int dragBand = -1;
    static constexpr float kRange = 18.0f;
};

class EqPanel : public ModulePanel
{
public:
    explicit EqPanel (NorthlineProcessor& p);
    int preferredWidth() const override { return 300; }

private:
    void layoutBody (juce::Rectangle<int>) override;
    EqDisplay display;
    struct Band { Chip* on; ChoiceBox* type; Knob *freq, *gain, *q; ChoiceBox* target; };
    std::array<Band, 4> bands {};
};

class CompPanel : public ModulePanel
{
public:
    explicit CompPanel (NorthlineProcessor& p);
    int preferredWidth() const override { return 204; }
    void updateMeters (const MeterFrame& f) override { gr.setGr (f.compGr); }

private:
    void layoutBody (juce::Rectangle<int>) override;
    Segmented* type;
    GrBar gr { colours::comp, 24.0f };
    Knob *thresh, *ratio, *attack, *release, *mix, *out, *scHp, *scFreq, *scGain;
    Chip *makeup, *ext, *listen;
};
} // namespace nl

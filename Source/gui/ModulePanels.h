#pragma once

#include "Analyser.h"
#include "EqView.h"
#include "PreView.h"
#include "Scopes.h"
#include "Widgets.h"
#include <functional>

namespace nl
{
//==============================================================================
// A transient button (listen, Delta): bound to a parameter, lit in amber while its value is `active`.
// Clicking sets the value to `active`, or back to 0 when it is already active.
class ParamButton : public juce::TextButton, public FocusTarget
{
public:
    ParamButton (NorthlineProcessor& p, int paramIndex, int activeValue, const juce::String& text);
    int focusParam() const override { return index; }
    void setFocusState (bool highlighted, bool dimmed) override;

private:
    NorthlineProcessor& proc;
    int index, active;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// The collapsible "Sidechain" heading of a module, with the key status on the right when EXT is on:
// green while a key signal was seen in the last 2 s, amber with "no key signal" after that.
class DrawerHeader : public juce::Component, private juce::Timer
{
public:
    DrawerHeader (NorthlineProcessor& p, int extParam, std::function<void()> onToggle);
    ~DrawerHeader() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setOpen (bool open);

private:
    enum class Status { off, keyOk, noKey };
    void timerCallback() override;

    NorthlineProcessor& proc;
    int ext;
    std::function<void()> toggle;
    bool open = false;
    Status status = Status::off;
    uint32_t lastKeySeen = 0;
};

//==============================================================================
class ModulePanel : public juce::Component
{
public:
    ModulePanel (NorthlineProcessor& p, int moduleId, int onParam, int modeParam);
    ~ModulePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override { showingChanged(); }
    void parentHierarchyChanged() override { showingChanged(); }

    int getModuleId() const noexcept { return moduleId; }
    virtual int preferredWidth() const = 0;
    virtual void updateMeters (const MeterFrame&) {}
    void setCpuUs (float us, float blockUs);
    std::function<void (int moduleId, int delta)> onMove;

protected:
    // Body layout: the body is split into a top row, the display (takes the rest), an optional meter row,
    // the primary knobs (60 px dials), the secondary knobs (38 px dials), a flow row, and a collapsible
    // Sidechain drawer. In a narrow (console) column the knob grids wrap into more rows.
    struct Item { juce::Component* comp; float minWidth; };
    struct Plan
    {
        std::vector<Item> top;                          // chips and choices above the display
        juce::Component* display = nullptr;             // takes the remaining height
        int displayMin = 120;
        std::vector<juce::Component*> meters;           // 14 px bars under the display
        std::vector<juce::Component*> primary;          // 60 px dials
        std::vector<juce::Component*> secondary;        // 38 px dials
        std::vector<Item> bottom;                       // chips and choices below the knobs
        bool drawer = false;                            // Sidechain drawer (its heading is always shown)
        std::vector<Item> drawerTop;                    // chips and transient buttons in the drawer
        std::vector<juce::Component*> drawerKnobs;      // 38 px dials in the drawer
    };
    void applyPlan (juce::Rectangle<int> body, const Plan& plan);

    virtual void layoutBody (juce::Rectangle<int> body) = 0;
    virtual void showingChanged() {}

    Knob* knob (int index, const juce::String& label);
    Chip* chip (int index, const juce::String& text);
    ChoiceBox* choice (int index);
    Segmented* segmented (int index);
    ParamButton* paramButton (int index, int activeValue, const juce::String& text);

    // A display or other child owned by the panel.
    template <class C, class... Args>
    C* make (Args&&... args)
    {
        auto* c = new C (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        return c;
    }

    // Re-lays the panel out when a parameter that changes the layout (M/S, expander mode) changes.
    void relayoutOn (int paramIndex);
    // Creates the Sidechain heading, with the key status for an EXT parameter.
    DrawerHeader* makeDrawer (int extParam);

    static int chipWidth (const juce::String& text) noexcept;

    NorthlineProcessor& proc;
    const int moduleId;
    const juce::Colour accent;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;

    juce::OwnedArray<juce::Component> owned;
    std::unique_ptr<DrawerHeader> drawerBar;
    bool drawerOpen = false;

private:
    void updateBodyEnabled();

    std::unique_ptr<Chip> power;
    std::unique_ptr<Segmented> mode;
    juce::TextButton moveLeft { "<" }, moveRight { ">" };
    juce::String cpuText;
    std::unique_ptr<juce::ParameterAttachment> onWatcher;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> layoutWatchers;
};

//==============================================================================
class PrePanel : public ModulePanel
{
public:
    explicit PrePanel (NorthlineProcessor& p);
    ~PrePanel() override;
    int preferredWidth() const override { return 214; }

private:
    void layoutBody (juce::Rectangle<int>) override;
    void showingChanged() override;

    HarmonicAnalyser harmonics;
    PreDisplay* display = nullptr;
    Chip *satOn, *hq, *satAuto, *hpOn, *lpOn, *thumpOn;
    ChoiceBox *satType, *hpSlope, *lpSlope, *target, *thumpAmt;
    Knob *drive, *tone, *hpFreq, *lpFreq, *mix, *hpRes, *lpRes, *thumpFreq;
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
    LevelScope* display = nullptr;
    Knob *thresh, *floorK, *hyst, *attack, *hold, *release, *ratio, *knee, *scHp, *scLp;
    Chip* ext;
    ParamButton* listen;
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
    ChoiceBox *type1, *type2;
    Knob *freq1, *thresh1, *range1, *freq2, *thresh2, *range2, *release, *out;
    ParamButton *listenB1, *listenB2, *listenDelta;
    LevelScope* display = nullptr;
    GrBar gr1 { colours::ds, 24.0f }, gr2 { colours::ds, 24.0f };
};

class EqPanel : public ModulePanel
{
public:
    explicit EqPanel (NorthlineProcessor& p);
    ~EqPanel() override;
    int preferredWidth() const override { return 300; }

private:
    void layoutBody (juce::Rectangle<int>) override;
    void showingChanged() override;
    void updateAnalyser();

    EqAnalyser analyser;
    EqView* display = nullptr;
    juce::TextButton analyserOn { "ANALYSER" }, frozen { "FREEZE" };
    ChoiceBox* curve = nullptr;
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
    LevelScope* scope = nullptr;
    CompCurve* curveInset = nullptr;
    Knob *thresh, *ratio, *attack, *release, *knee, *range, *link, *mix, *out, *scHp, *scLp, *scFreq, *scGain;
    Chip *makeup, *autoRel, *ext;
    ParamButton *listenKey, *listenDelta;
};
} // namespace nl

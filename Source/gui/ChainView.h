#pragma once

#include "Widgets.h"

namespace nl
{
// Pure chain geometry and order logic (tested in TestUiShell.cpp).
namespace chain
{
using Order = ChannelStrip::Order;

// The order with `moduleId` moved to `targetIndex` (the other modules keep their order).
Order moveTo (const Order& order, int moduleId, int targetIndex);

// Index of the slot whose centre is nearest to `centreX`, clamped to 0..count-1.
int slotForCentre (float centreX, float firstLeft, float pitch, int count);

// Left edge of slot `index`.
float slotLeft (int index, float firstLeft, float pitch);

constexpr int kCardGap = 6;
constexpr int kIconSize = 28;
constexpr int kIconsWidth = 2 * kIconSize + 4;
}

class ChainCard;

// Strip of one card per module, in the processor's order. Drag the handle to reorder (with an
// insertion marker and a 150 ms slide, committed through proc.setOrder). Click a card to select it.
class ChainView : public juce::Component
{
public:
    explicit ChainView (NorthlineProcessor& p);
    ~ChainView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void refreshOrder();              // re-reads the processor's order and slides the cards
    void setSelected (int moduleId);
    int getSelected() const noexcept { return selected; }
    void updateMeters (const MeterFrame&);
    void refreshValues();             // macro values and modes after a state change

    std::function<void (int moduleId)> onSelect;
    std::function<void()> onOrderChanged, onOpenSettings;

    // Used by the cards.
    void handleDragEvent (int moduleId, int phase, const juce::MouseEvent& e);
    void moveSelected (int delta);

    static constexpr int kHeight = 72;

private:
    void layoutCards (bool animate);
    juce::Rectangle<int> slotBounds (int slot) const;
    void dragTo (float x);
    void dragEnd();

    NorthlineProcessor& proc;
    std::array<std::unique_ptr<ChainCard>, numModules> cards;
    std::unique_ptr<juce::Component> backdrop;
    std::unique_ptr<juce::Button> helpButton, settingsButton;
    juce::ComponentAnimator animator;

    chain::Order order {};
    int selected = modPre;

    int cardW = 100;
    int dragId = -1, dragTarget = -1;
    float dragOffset = 0.0f;
};

// One module: power, name, stereo mode, three macro values and a gain-reduction bar.
class ChainCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChainCard (NorthlineProcessor& p, int moduleId, std::function<void (int, int, const juce::MouseEvent&)> onHandle);
    ~ChainCard() override;

    int getModuleId() const noexcept { return moduleId; }
    void setSelected (bool on);
    void setGr (float db);
    void refreshValues() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::function<void (int moduleId)> onSelect;
    std::function<void (int moduleId, int delta)> onMoveKey, onNavigate;

    juce::String modeText() const;
    juce::String macroValue (int i) const;
    bool isOn() const;
    juce::String macroLabel (int i) const;

private:
    NorthlineProcessor& proc;
    const int moduleId;
    std::function<void (int, int, const juce::MouseEvent&)> handle;
    std::unique_ptr<Chip> power;
    std::unique_ptr<juce::Component> dragHandle;
    std::unique_ptr<GrBar> grBar;
    std::vector<std::unique_ptr<ParamBinding>> listeners; // mode and macro values: repaint on change
    std::array<int, 3> macros {};
    int modeParam = -1;
    bool selectedState = false;
};

// Small floating panel with the per-module CPU, shown when Settings -> Show CPU is on.
class CpuInfoPanel : public juce::Component
{
public:
    CpuInfoPanel();
    void setValues (const std::array<float, numModules>& us, float blockUs);
    void paint (juce::Graphics&) override;
    static constexpr int kWidth = 200, kHeight = 116;

private:
    std::array<float, numModules> us {};
    float blockUs = 0.0f;
};
} // namespace nl

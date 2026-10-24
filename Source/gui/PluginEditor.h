#pragma once

#include "ChainView.h"
#include "MasterPanel.h"
#include "ModulePanels.h"
#include "SettingsOverlay.h"
#include "TopBar.h"
#include "UiSettings.h"

namespace nl
{
// Layout contract (roadmap 8.2) at 100 %, in the editor's base canvas of 1320 x 780 px. Everything
// sits on an 8 px margin. The detail area is 996 x 632 and the master column 300 px wide (the
// contract's "about 1020 x 664" becomes 996 x 632 with the margins).
namespace shell
{
struct Geometry
{
    juce::Rectangle<int> topBar, chain, detail, master;
};
Geometry geometry();

constexpr int kBaseW = 1320, kBaseH = 780;
constexpr int kMasterW = 300, kMargin = 8, kTopBarH = 44, kChainH = 72;
}

class NorthlineEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit NorthlineEditor (NorthlineProcessor&);
    ~NorthlineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kBaseW = shell::kBaseW, kBaseH = shell::kBaseH;

    // Overlay host: dims the content and shows `overlay` above it, sized to the content area.
    // Esc or closeOverlay() removes it (deleted after the current event, so an overlay can close
    // itself from its own callback).
    void showOverlay (std::unique_ptr<juce::Component> overlay);
    void closeOverlay();
    bool hasOverlay() const noexcept { return overlay != nullptr; }

    // Window size: 75 % to 200 % of the base size, always the base aspect ratio.
    void setScale (float scale, bool storeInProcessor);
    float getScale() const noexcept { return scale; }

    int selectedModule() const noexcept { return selected; }
    void selectModule (int moduleId);
    bool isConsoleLayout() const noexcept { return console; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void onFrame();
    void buildContent();
    void layoutAll();
    void layoutDetail();
    void drainMeters();
    void housekeeping();
    void settingsChanged();
    void openSettings();
    void openOnboarding();
    void updateFocus();

    NorthlineProcessor& proc;
    NorthlineLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content; // the 1320 x 780 canvas, scaled to the window
    std::unique_ptr<TopBar> topBar;
    std::unique_ptr<ChainView> chain;
    std::array<std::unique_ptr<ModulePanel>, numModules> panels;
    std::unique_ptr<MasterPanel> master;

    juce::Component detail;     // area under the chain strip
    juce::Viewport consoleView; // console layout: all five panels, scrolls horizontally
    juce::Component consoleBody;
    std::unique_ptr<CpuInfoPanel> cpuInfo;

    juce::Component overlayLayer;  // hidden unless an overlay is open
    juce::Component overlayHolder; // base-size canvas, scaled like the content
    std::unique_ptr<juce::Component> overlay;

    std::unique_ptr<juce::VBlankAttachment> vblank;
    double lastFrameMs = 0.0;

    int selected = modPre;
    bool console = false;
    float scale = 1.0f;
    int appliedSerial = 0;

    std::vector<MeterFrame> frameBuf;
    int lastGeneration = -1, tick = 0, cpuTick = 0;
    ChannelStrip::Order lastOrder {};
    bool lastFocusMode = false;
    std::vector<int> lastFocus;
    float cpuAvg = 0.0f;
};
} // namespace nl

#pragma once

#include "Widgets.h"

namespace nl
{
// The 44 px top bar: preset navigation, Save menu, A/B, undo history, Smart Start, Spotlight,
// Eco/HQ, latency, hub and settings. Callbacks are wired by the editor.
class TopBar : public juce::Component
{
public:
    explicit TopBar (NorthlineProcessor& p);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();            // preset name and position, A/B, undo state, latency, Spotlight state
    void setCpu (float percent);

    bool isFocusMode() const;          // Spotlight focus: dims every control that is not in the Spotlight
    bool isSpotlightEditMode() const;  // while on, clicks on controls toggle their Spotlight membership
    void setSpotlightEditMode (bool on);

    // Ctrl/Cmd+Z (undo), Ctrl/Cmd+Shift+Z and Ctrl+Y (redo). The editor forwards key presses here.
    bool handleKeyPress (const juce::KeyPress& key);

    std::function<void()> onFocusChanged, onScaleChanged;
    std::function<void()> onSmartStart, onOpenHub, onOpenSettings;
    std::function<void()> onOpenPresetBrowser; // clicking the preset name; without it, a preset menu opens
    std::function<void (bool)> onSpotlightEditChanged;

private:
    class SpotlightEditLayer;
    void showPresetMenu();
    void showSaveMenu();
    void showHistoryMenu();
    void stepPreset (int delta);
    void saveUserPresetAs();
    void copyModule (int moduleId);
    void pasteModule();
    void saveModulePresetAs (int moduleId);
    void loadModulePresetFrom();
    void setEditLayerVisible (bool on);

    NorthlineProcessor& proc;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
    juce::TextButton slotA { "A" }, slotB { "B" }, copyAB { "A>B" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, historyButton { "History" }, resetButton { "Reset" };
    juce::TextButton smartStartButton { "Smart Start" }, spotlightButton { "Spotlight" }, editSpotlightButton { "Edit Spotlight" };
    juce::TextButton hubButton { "Hub" }, settingsButton { "Settings" };
    juce::ComboBox scale;
    std::unique_ptr<Segmented> quality;
    juce::String cpuText { "CPU --" }, latencyText { "0 smp" };
    std::unique_ptr<SpotlightEditLayer> editLayer;
    juce::String lastPresetName;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace nl

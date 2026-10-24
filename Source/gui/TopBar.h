#pragma once

#include "Widgets.h"
#include "Transient.h"

namespace nl
{
// The interface size as the top bar shows it: the percentage (any size, including the ones not in the list).
juce::String scalePercentText (float scale);

// Undo and redo: a filled curved arrow drawn as a path, so it reads at every interface size.
class ArrowButton : public juce::Button
{
public:
    explicit ArrowButton (bool redo);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    bool mirrored;
};

// The 44 px top bar: preset navigation, Save menu, A/B, undo history, Smart Start, Spotlight,
// Eco/HQ, latency, hub and settings. Callbacks are wired by the editor.
// COMPARE: a momentary control. Held, it compares the whole strip (loudness-matched); released, it goes back.
// Shift-click latches the comparison on until the next click.
class CompareButton : public juce::TextButton
{
public:
    explicit CompareButton (NorthlineProcessor& p);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh(); // lit while a compare is on, whoever turned it on

private:
    NorthlineProcessor& proc;
    nl::transient::CompareHold hold;
};

class TopBar : public juce::Component
{
public:
    explicit TopBar (NorthlineProcessor& p);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();            // preset name and position, A/B, undo state, latency, Spotlight state
    void refreshTransient();   // the COMPARE button (transient, not a parameter): called a few times a second
    // The interface size the editor is showing (the combo shows it, whether or not it is stored in the processor).
    void setShownScale (float s) { shownScale = s; }
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
    void saveCurrentPreset();
    void saveUserPresetAs();
    void copyModule (int moduleId);
    void pasteModule();
    void saveModulePresetAs (int moduleId);
    void loadModulePresetFrom();
    void setEditLayerVisible (bool on);

    NorthlineProcessor& proc;
    CompareButton compareButton;
    float shownScale = 1.0f;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
    juce::TextButton slotA { "A" }, slotB { "B" }, copyAB { "A>B" };
    ArrowButton undoButton { false }, redoButton { true };
    juce::TextButton historyButton { "History" };
    juce::TextButton smartStartButton { "Smart Start" }, spotlightButton { "Spotlight" }, editSpotlightButton { "Edit Spotlight" };
    juce::TextButton hubButton { "Hub" }, settingsButton { "Settings" };
    juce::ComboBox scale;
    std::unique_ptr<Segmented> quality;
    juce::String cpuText { "CPU --" }, latencyText { "Lat 0 smp" };
    juce::Rectangle<int> latencyBounds, cpuBounds;
    std::unique_ptr<SpotlightEditLayer> editLayer;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace nl

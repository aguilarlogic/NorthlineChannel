#pragma once

#include "Widgets.h"

namespace nl
{
class TopBar : public juce::Component
{
public:
    explicit TopBar (NorthlineProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();                 // preset name, A/B and undo state
    void setCpu (float percent);
    bool isFocusMode() const { return focusButton.getToggleState(); }

    std::function<void()> onFocusChanged, onScaleChanged;

private:
    void showPresetMenu();
    void stepPreset (int delta);
    void savePreset();

    NorthlineProcessor& proc;
    juce::TextButton prev { "<" }, next { ">" }, presetButton, save { "SAVE" };
    juce::TextButton slotA { "A" }, slotB { "B" }, copyAB { "A>B" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, focusButton { "FOCUS" }, resetButton { "RESET" };
    juce::ComboBox scale;
    juce::String cpuText { "CPU --" };
    int factoryIndex = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace nl

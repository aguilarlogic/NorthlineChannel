#pragma once

#include "UiSettings.h"
#include "Widgets.h"

namespace nl
{
// Settings body, shown as an overlay card (the card supplies the title and close button). Theme,
// layout, size presets, the default size with "Apply to all instances", Show CPU, and the first-run tour.
// Global values are written to UiSettings; the size of this instance goes through onInstanceSize.
class SettingsOverlay : public juce::Component
{
public:
    SettingsOverlay (NorthlineProcessor& p, std::function<void (float)> onInstanceSize, std::function<void()> onReplayTour);
    ~SettingsOverlay() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void refresh();   // reads the current values back into the controls

private:
    void addSizeRow (juce::OwnedArray<juce::TextButton>& row, std::function<void (float)> onPick);

    NorthlineProcessor& proc;
    std::function<void (float)> instanceSize;
    std::function<void()> replayTour;

    juce::TextButton themeDark { "Dark" }, themeHigh { "High contrast" };
    juce::TextButton layoutChain { "Chain and detail" }, layoutConsole { "Console (all five)" };
    juce::OwnedArray<juce::TextButton> sizeInstance, sizeDefault;
    juce::TextButton applyAll { "Apply default to all open instances" };
    juce::TextButton showCpu { "Show CPU (Info overlay)" };
    juce::TextButton tour { "Show the first-run tour again" };
};
} // namespace nl

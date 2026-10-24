#pragma once

#include "../PluginProcessor.h"
#include "../Presets.h"
#include "LookAndFeel.h"

namespace nl
{
// Preset browser overlay (roadmap 8.6). A plain component: the editor shows it above everything,
// sized to the editor's content area, and hides it when onClose fires.
class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser (NorthlineProcessor& p);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    void refreshLibrary();  // rescans the factory and user presets (also called when shown)
    void showSavePanel();   // opens the inline save form with the current preset name

    std::function<void()> onClose;          // Escape, or the close button
    std::function<void()> onPresetLoaded;   // after a preset loads, so the top bar can refresh

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace nl

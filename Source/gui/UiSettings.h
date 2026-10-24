#pragma once

#include "Tokens.h"
#include <juce_data_structures/juce_data_structures.h>
#include <memory>

namespace nl
{
// Global interface settings, shared by every instance in the process and saved in the user's
// application-data folder (NorthlineChannel/ui.settings). Changes are broadcast to the open
// editors, which apply them. Message thread only.
class UiSettings : public juce::ChangeBroadcaster
{
public:
    static constexpr float kMinScale = 0.75f, kMaxScale = 2.0f;
    static constexpr float kDefaultScale = 1.0f;

    // The process-wide settings, stored in the user's application-data folder.
    static UiSettings& get();

    // A settings store backed by `file` (used by tests and by get()).
    explicit UiSettings (const juce::File& file);
    ~UiSettings() override;

    // Points the store at another file and reloads it (tests use a temporary folder).
    void useFile (const juce::File& file);
    juce::File getFile() const;

    tokens::Theme theme() const;
    void setTheme (tokens::Theme);

    bool consoleLayout() const;
    void setConsoleLayout (bool);

    bool showCpu() const;
    void setShowCpu (bool);

    // Size used by editors that have not been sized by the user yet (see the editor for the rule).
    float defaultScale() const;
    void setDefaultScale (float);

    // Sets the default and tells every open editor to resize to it.
    void applyDefaultScaleToAllInstances();
    int applyAllSerial() const;

    bool onboardingDone() const;
    void setOnboardingDone (bool);

    static float clampScale (float scale);

private:
    void load();
    void store();

    juce::File file;
    std::unique_ptr<juce::PropertiesFile> props;
    int serial = 0;
};
} // namespace nl

#include "SettingsOverlay.h"

namespace nl
{
namespace
{
constexpr float kScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };

void styleToggle (juce::TextButton& b, bool on)
{
    b.setClickingTogglesState (false);
    b.setToggleState (on, juce::dontSendNotification);
}
} // namespace

SettingsOverlay::SettingsOverlay (NorthlineProcessor& p, std::function<void (float)> onInstanceSize, std::function<void()> onReplayTour)
    : proc (p), instanceSize (std::move (onInstanceSize)), replayTour (std::move (onReplayTour))
{
    setWantsKeyboardFocus (true);
    setTitle ("Settings");
    for (auto* b : { &themeDark, &themeHigh, &layoutChain, &layoutConsole, &applyAll, &showCpu, &tour })
        addAndMakeVisible (b);

    for (int i = 0; i < 5; ++i)
    {
        const auto label = juce::String (juce::roundToInt (kScales[i] * 100)) + "%";
        auto* a = sizeInstance.add (new juce::TextButton (label));
        auto* d = sizeDefault.add (new juce::TextButton (label));
        addAndMakeVisible (a);
        addAndMakeVisible (d);
        a->setTooltip ("Size of this instance (saved with the session)");
        d->setTooltip ("Size new instances open at when they have not been sized yet");
        a->setTitle ("Size " + label);
        d->setTitle ("Default size " + label);
        a->onClick = [this, s = kScales[i]] { if (instanceSize) instanceSize (s); refresh(); };
        d->onClick = [this, s = kScales[i]] { UiSettings::get().setDefaultScale (s); refresh(); };
    }

    themeDark.onClick = [this] { UiSettings::get().setTheme (tokens::Theme::dark); refresh(); };
    themeHigh.onClick = [this] { UiSettings::get().setTheme (tokens::Theme::highContrast); refresh(); };
    layoutChain.onClick = [this] { UiSettings::get().setConsoleLayout (false); refresh(); };
    layoutConsole.onClick = [this] { UiSettings::get().setConsoleLayout (true); refresh(); };
    applyAll.setTooltip ("Sets the default size and resizes every open instance to it");
    applyAll.onClick = [this] { UiSettings::get().applyDefaultScaleToAllInstances(); refresh(); };
    showCpu.setTooltip ("Shows per-module CPU in a small panel instead of the module headers");
    showCpu.onClick = [this] { UiSettings::get().setShowCpu (! UiSettings::get().showCpu()); refresh(); };
    tour.onClick = [this] { if (replayTour) replayTour(); };

    refresh();
}

SettingsOverlay::~SettingsOverlay() = default;

void SettingsOverlay::refresh()
{
    auto& s = UiSettings::get();
    styleToggle (themeDark, s.theme() == tokens::Theme::dark);
    styleToggle (themeHigh, s.theme() == tokens::Theme::highContrast);
    styleToggle (layoutChain, ! s.consoleLayout());
    styleToggle (layoutConsole, s.consoleLayout());
    styleToggle (showCpu, s.showCpu());
    for (int i = 0; i < 5; ++i)
    {
        styleToggle (*sizeInstance[i], std::abs (proc.getUiScale() - kScales[i]) < 0.01f);
        styleToggle (*sizeDefault[i], std::abs (s.defaultScale() - kScales[i]) < 0.01f);
    }
    repaint();
}

void SettingsOverlay::resized()
{
    // The card gives the body its padding and the title row; sections are laid out from the top of the body.
    auto p = getLocalBounds();
    auto row = [&] (int y, int h = 26) { return juce::Rectangle<int> (p.getX(), p.getY() + y, p.getWidth(), h); };
    // Two equal halves with a 6 px gap. Split in sequence: argument evaluation order is unspecified.
    auto half = [] (juce::Rectangle<int> r)
    {
        auto left = r.removeFromLeft (r.getWidth() / 2 - 3);
        r.removeFromLeft (6);
        return std::make_pair (left, r);
    };

    auto t = half (row (14));
    themeDark.setBounds (t.first);
    themeHigh.setBounds (t.second);

    auto l = half (row (80));
    layoutChain.setBounds (l.first);
    layoutConsole.setBounds (l.second);

    auto fiveAcross = [] (juce::Rectangle<int> r, juce::OwnedArray<juce::TextButton>& btns)
    {
        const int w = (r.getWidth() - 4 * 6) / 5;
        for (auto* b : btns)
        {
            b->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (6);
        }
    };
    fiveAcross (row (146), sizeInstance);
    fiveAcross (row (212), sizeDefault);
    applyAll.setBounds (row (246));
    showCpu.setBounds (row (318));
    tour.setBounds (row (382));
}

void SettingsOverlay::paint (juce::Graphics& g)
{
    auto p = getLocalBounds();

    auto section = [&] (int y, const char* text)
    {
        g.setColour (colours::dim);
        g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
        g.drawText (text, juce::Rectangle<int> (p.getX(), p.getY() + y, p.getWidth(), 14), juce::Justification::centredLeft);
    };
    section (0, "APPEARANCE");
    section (66, "LAYOUT");
    section (132, "SIZE OF THIS INSTANCE");
    section (198, "DEFAULT SIZE FOR NEW INSTANCES");
    section (304, "DISPLAY");
    section (368, "HELP");

    g.setColour (colours::faint);
    g.setFont (NorthlineLookAndFeel::font (tokens::type::label));
    g.drawText ("Console shows all five modules side by side. The default size applies to new instances.",
                juce::Rectangle<int> (p.getX(), p.getY() + 276, p.getWidth(), 26), juce::Justification::centredLeft);
}

bool SettingsOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose) onClose();
        return true;
    }
    return false;
}
} // namespace nl

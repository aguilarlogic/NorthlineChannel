#include "SettingsOverlay.h"

namespace nl
{
namespace
{
constexpr float kScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
constexpr int kPanelW = 560, kPanelH = 520;

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
    for (auto* b : { &close, &themeDark, &themeHigh, &layoutChain, &layoutConsole, &applyAll, &showCpu, &tour })
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

    close.setTooltip ("Close (Esc)");
    close.onClick = [this] { if (onClose) onClose(); };
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

juce::Rectangle<int> SettingsOverlay::panelBounds() const
{
    return juce::Rectangle<int> (kPanelW, kPanelH).withCentre (getLocalBounds().getCentre());
}

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
    auto p = panelBounds().reduced (18, 14);
    close.setBounds (p.removeFromBottom (30).removeFromRight (120));

    // Title row and sections are laid out from the top of the panel.
    p.removeFromTop (30);
    auto row = [&] (int y, int h = 26) { return juce::Rectangle<int> (p.getX(), p.getY() + y, p.getWidth(), h); };
    auto half = [] (juce::Rectangle<int> r) { return std::make_pair (r.removeFromLeft (r.getWidth() / 2 - 3), r.withTrimmedLeft (6)); };

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
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillAll();

    const auto panel = panelBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (panel, tokens::radius::panel + 2.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (panel.reduced (0.5f), tokens::radius::panel + 2.0f, 1.0f);

    auto p = panelBounds().reduced (18, 14);
    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (tokens::type::title + 2.0f, true));
    g.drawText ("Settings", p.removeFromTop (30), juce::Justification::centredLeft);

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
    g.drawText ("Console layout puts all five modules side by side. The default size applies to instances that have not been sized yet.",
                juce::Rectangle<int> (p.getX(), p.getY() + 276, p.getWidth(), 26), juce::Justification::centredLeft);
}

void SettingsOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition()) && onClose) onClose();
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

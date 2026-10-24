#include "Onboarding.h"
#include "LookAndFeel.h"

namespace nl
{
namespace
{
constexpr int kCardW = 520, kCardH = 250;
}

OnboardingCoach::OnboardingCoach()
{
    setWantsKeyboardFocus (true);
    setTitle ("First-run tour");
    addAndMakeVisible (back);
    addAndMakeVisible (next);
    addAndMakeVisible (skip);
    back.onClick = [this] { setStep (step - 1); };
    next.onClick = [this] { if (step == kSteps - 1) finish(); else setStep (step + 1); };
    skip.onClick = [this] { finish(); };
    skip.setTooltip ("Skip the tour. You can show it again from Settings.");
    setStep (0);
}

OnboardingCoach::~OnboardingCoach() = default;

juce::String OnboardingCoach::titleFor (int s)
{
    static const char* titles[] { "1 of 4: The chain", "2 of 4: Editing a control", "3 of 4: Right-click and Spotlight", "4 of 4: Settings and help" };
    return titles[juce::jlimit (0, kSteps - 1, s)];
}

juce::String OnboardingCoach::bodyFor (int s)
{
    static const char* bodies[] {
        "Each card in the top strip is one module. Click a card to open its controls in the large area. Drag the handle on the left of a card to change the order; Undo reverses it.",
        "Drag a knob up or down (or sideways). Hold Shift for fine steps. Double-click to type a value such as 3k, -18, 50% or 100ms. Alt-click (or Ctrl-click) resets a control to its default.",
        "Right-click any control to reset it, copy or paste its value, or add it to Spotlight. Spotlight marks the controls a preset wants you to notice.",
        "The gear opens Settings: theme (including high contrast), interface size, console layout and the CPU readout. The ? button turns on help mode, which explains a control on hover."
    };
    return bodies[juce::jlimit (0, kSteps - 1, s)];
}

void OnboardingCoach::setStep (int s)
{
    step = juce::jlimit (0, kSteps - 1, s);
    back.setEnabled (step > 0);
    next.setButtonText (step == kSteps - 1 ? "Done" : "Next");
    repaint();
}

void OnboardingCoach::finish()
{
    UiSettings::get().setOnboardingDone (true);
    if (onClose) onClose();
}

void OnboardingCoach::resized()
{
    const auto card = juce::Rectangle<int> (kCardW, kCardH).withCentre (getLocalBounds().getCentre());
    auto r = card.reduced (20, 18).removeFromBottom (30);
    skip.setBounds (r.removeFromLeft (80));
    next.setBounds (r.removeFromRight (100));
    r.removeFromRight (8);
    back.setBounds (r.removeFromRight (100));
}

void OnboardingCoach::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillAll();

    const auto card = juce::Rectangle<int> (kCardW, kCardH).withCentre (getLocalBounds().getCentre()).toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (card, tokens::radius::panel + 2.0f);
    g.setColour (colours::focus);
    g.drawRoundedRectangle (card.reduced (0.5f), tokens::radius::panel + 2.0f, 1.0f);

    auto r = card.toNearestInt().reduced (20, 18);
    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (tokens::type::title + 2.0f, true));
    g.drawText (titleFor (step), r.removeFromTop (26), juce::Justification::centredLeft);

    g.setColour (colours::dim);
    g.setFont (NorthlineLookAndFeel::font (tokens::type::value));
    g.drawFittedText (bodyFor (step), r.removeFromTop (110).reduced (0, 6), juce::Justification::topLeft, 6);

    // step dots
    auto dots = juce::Rectangle<int> (r.getX(), card.getBottom() - 74, 80, 12);
    for (int i = 0; i < kSteps; ++i)
    {
        g.setColour (i == step ? colours::focus : colours::controlEdge);
        g.fillEllipse (juce::Rectangle<float> ((float) (dots.getX() + i * 16), (float) dots.getY(), 9.0f, 9.0f));
    }
}

bool OnboardingCoach::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { finish(); return true; }
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey) { next.triggerClick(); return true; }
    if (key == juce::KeyPress::leftKey) { back.triggerClick(); return true; }
    return false;
}
} // namespace nl

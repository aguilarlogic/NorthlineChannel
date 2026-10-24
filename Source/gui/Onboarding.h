#pragma once

#include "UiSettings.h"

namespace nl
{
// First-run coach: four short steps, shown as an overlay card until it is finished or skipped. The
// flag is kept in UiSettings (onboardingDone), so it appears once per user.
class OnboardingCoach : public juce::Component
{
public:
    static constexpr int kSteps = 4;

    OnboardingCoach();
    ~OnboardingCoach() override;

    std::function<void()> onClose;

    int getStep() const noexcept { return step; }
    static juce::String titleFor (int step);
    static juce::String bodyFor (int step);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
    bool keyPressed (const juce::KeyPress&) override;

private:
    void setStep (int s);
    void finish();

    int step = 0;
    juce::TextButton back { "Back" }, next { "Next" }, skip { "Skip" };
};

} // namespace nl

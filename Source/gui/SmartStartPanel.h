#pragma once

#include "../PluginProcessor.h"
#include "LookAndFeel.h"
#include <functional>
#include <memory>

namespace nl
{
// Smart Start overlay (roadmap 9.1). Pick the source, press Listen while it plays. The analysis runs in
// the background, its result is applied as one undo step with Spotlight on the changed parameters, and
// the list below says what changed and why. The editor shows it as an overlay card; onClose fires on Escape.
class SmartStartPanel : public juce::Component, private juce::Timer
{
public:
    explicit SmartStartPanel (NorthlineProcessor& processor);
    ~SmartStartPanel() override;

    void resized() override;

    std::function<void()> onClose;

private:
    class ResultList;

    void timerCallback() override;
    void listenOrCancel();
    void showResult();

    NorthlineProcessor& proc;
    NorthlineLookAndFeel lookAndFeel;
    juce::Label subtitle, status;
    juce::TextButton listenButton { "Listen" };
    juce::OwnedArray<juce::TextButton> sourceButtons;
    juce::Viewport resultViewport;
    std::unique_ptr<ResultList> results;
    smart::Source source = smart::Source::Vocal;
    smart::SmartStart::Phase lastPhase = smart::SmartStart::Phase::Idle;
    bool listenShowsBusy = false;
};
} // namespace nl

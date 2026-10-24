#include "SmartStartPanel.h"

namespace nl
{
namespace
{
constexpr int kRowHeight = 44;
constexpr int kSourceButtonHeight = 28;
constexpr int kPad = 16;
} // namespace

// The list of applied changes: parameter, value before and after, and the one-line reason.
class SmartStartPanel::ResultList : public juce::Component
{
public:
    // The width is set by the panel's layout; this only sets the height the rows need.
    void setChanges (const std::vector<smart::Change>& changes)
    {
        rows = changes;
        setSize (getWidth(), juce::jmax (1, (int) rows.size()) * kRowHeight + kPad);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        if (rows.empty())
        {
            g.setColour (colours::dim);
            g.setFont (NorthlineLookAndFeel::font (13.0f));
            g.drawText ("No changes. Smart Start leaves the chain as it is.", getLocalBounds().reduced (kPad), juce::Justification::centredLeft);
            return;
        }
        const int textWidth = getWidth() - 2 * kPad;
        int y = kPad / 2;
        for (const auto& c : rows)
        {
            g.setColour (colours::edge);
            g.drawHorizontalLine (y + kRowHeight - 1, (float) kPad, (float) (getWidth() - kPad));

            g.setColour (colours::text);
            g.setFont (NorthlineLookAndFeel::font (13.0f, true));
            g.drawText (paramDef (c.param).name, juce::Rectangle<int> (kPad, y + 2, textWidth / 2, kRowHeight / 2),
                        juce::Justification::centredLeft, true);
            g.setFont (NorthlineLookAndFeel::font (13.0f));
            g.drawText (formatValue (c.param, c.from) + "  to  " + formatValue (c.param, c.to),
                        juce::Rectangle<int> (kPad + textWidth / 2, y + 2, textWidth - textWidth / 2, kRowHeight / 2),
                        juce::Justification::centredRight, true);

            g.setColour (colours::dim);
            g.setFont (NorthlineLookAndFeel::font (11.0f));
            g.drawText (c.reason, juce::Rectangle<int> (kPad, y + kRowHeight / 2, textWidth, kRowHeight / 2 - 3),
                        juce::Justification::centredLeft, true);
            y += kRowHeight;
        }
    }

private:
    std::vector<smart::Change> rows;
};

SmartStartPanel::SmartStartPanel (NorthlineProcessor& processor)
    : proc (processor), results (std::make_unique<ResultList>())
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (false);

    subtitle.setText ("Pick the source, press Listen while it plays. Smart Start sets a starting point for the chain. "
                      "It is one undo step, and the changes are highlighted in Spotlight.",
                      juce::dontSendNotification);
    subtitle.setFont (NorthlineLookAndFeel::font (12.0f));
    subtitle.setColour (juce::Label::textColourId, colours::dim);
    subtitle.setJustificationType (juce::Justification::topLeft);

    status.setFont (NorthlineLookAndFeel::font (13.0f));
    status.setColour (juce::Label::textColourId, colours::text);

    addAndMakeVisible (subtitle);
    addAndMakeVisible (status);
    addAndMakeVisible (listenButton);
    addAndMakeVisible (resultViewport);
    resultViewport.setViewedComponent (results.get(), false);
    resultViewport.setScrollBarsShown (true, false);

    for (int i = 0; i < smart::kNumSources; ++i)
    {
        auto* b = sourceButtons.add (new juce::TextButton (smart::sourceName (smart::sourceFromIndex (i))));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->onClick = [this, i] { source = smart::sourceFromIndex (i); };
        addAndMakeVisible (b);
    }
    source = proc.getSmartStart().getSource();
    sourceButtons[(int) source]->setToggleState (true, juce::dontSendNotification);

    listenButton.onClick = [this] { listenOrCancel(); };

    lastPhase = proc.getSmartStart().getPhase();
    if (lastPhase == smart::SmartStart::Phase::Done) showResult();
    timerCallback();
    startTimerHz (30);
}

SmartStartPanel::~SmartStartPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
    resultViewport.setViewedComponent (nullptr, false);
}

void SmartStartPanel::listenOrCancel()
{
    const auto phase = proc.getSmartStart().getPhase();
    if (phase == smart::SmartStart::Phase::Capturing || phase == smart::SmartStart::Phase::Analysing)
        proc.smartCancel();
    else
        proc.smartListen (source);
    timerCallback();
}

void SmartStartPanel::showResult()
{
    results->setChanges (proc.getSmartStart().lastResult().changes);
}

void SmartStartPanel::timerCallback()
{
    const auto& ss = proc.getSmartStart();
    const auto phase = ss.getPhase();
    if (phase == smart::SmartStart::Phase::Done && lastPhase != smart::SmartStart::Phase::Done)
        showResult();
    lastPhase = phase;

    const bool busy = phase == smart::SmartStart::Phase::Capturing || phase == smart::SmartStart::Phase::Analysing;
    if (busy != listenShowsBusy)
    {
        listenShowsBusy = busy;
        listenButton.setButtonText (busy ? "Cancel" : "Listen");
        listenButton.setColour (juce::TextButton::buttonColourId, busy ? colours::focus : colours::track); // amber while listening
        listenButton.setColour (juce::TextButton::textColourOffId, busy ? colours::background : colours::text);
    }
    status.setText (ss.statusText(), juce::dontSendNotification);
}

void SmartStartPanel::resized()
{
    auto area = getLocalBounds();

    subtitle.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);

    // Sources: three columns.
    const int cols = 3;
    const int colWidth = juce::jmax (1, area.getWidth() / cols);
    for (int i = 0; i < sourceButtons.size(); ++i)
    {
        const int r = i / cols, c = i % cols;
        sourceButtons[i]->setBounds (area.getX() + c * colWidth, area.getY() + r * (kSourceButtonHeight + 6), colWidth - 6, kSourceButtonHeight);
    }
    area.removeFromTop (((sourceButtons.size() + cols - 1) / cols) * (kSourceButtonHeight + 6) + 8);

    auto controls = area.removeFromTop (40);
    listenButton.setBounds (controls.removeFromLeft (160));
    controls.removeFromLeft (12);
    status.setBounds (controls);
    area.removeFromTop (10);

    resultViewport.setBounds (area);
    results->setSize (juce::jmax (1, area.getWidth() - resultViewport.getScrollBarThickness()), results->getHeight());
}
} // namespace nl

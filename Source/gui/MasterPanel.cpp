#include "MasterPanel.h"

namespace nl
{
namespace
{
constexpr int kHeader = 32, kPad = 8, kGap = 4, kKnobH = 56, kRowH = 20, kLabelH = 14;
constexpr int kMeterH = 132, kGrH = 24, kHistH = 48, kLoudH = 132, kCorrH = 18;
constexpr int kMeterScaleW = 22, kMeterW = 40, kTrimW = 84, kCustomBtnW = 22, kResetW = 46, kOptionsW = 60;
constexpr float kScopeShare = 0.38f;   // share of the column given to the vectorscope
constexpr float kVuSmoothing = 0.36f;  // VU ballistics for the input and GR sources (about 300 ms at 30 Hz)
} // namespace

MasterPanel::MasterPanel (NorthlineProcessor& p) : proc (p)
{
    const auto a = colours::master;
    inGain = add<Knob> (p, P::inGain, "Input", a);
    outGain = add<Knob> (p, P::outGain, "Output", a);
    vuRef = add<Knob> (p, P::opLevel, "Op Level", a);
    phase = add<ChoiceBox> (p, P::inPolarity);
    vuSource = add<ChoiceBox> (p, P::vuSource);
    monitor = add<ChoiceBox> (p, P::monitor);
    limOn = add<Chip> (p, P::limOn, "LIMIT", a);
    lookahead = add<ChoiceBox> (p, P::limMode);
    lookahead->setTooltip ("Limiter mode. Safety Clip: zero latency. Brickwall / True Peak: 1.5 ms lookahead, reported to the host");
    ceiling = add<Knob> (p, P::limCeiling, "Ceiling", a);
    limRelease = add<Knob> (p, P::limRelease, "Release", a);
    match = add<ChoiceBox> (p, P::matchMode);
    match->setTooltip ("Matches output loudness to input loudness so you judge processing, not volume");
    matchTrim = add<Knob> (p, P::matchTrim, "Trim", a);

    for (auto* c : std::initializer_list<juce::Component*> { &inMeter, &outMeter, &meterScale, &vu, &limGr, &grHistory, &scope, &correlation, &matchView,
                                                              &readouts, &targetBox, &customDown, &customUp, &resetButton, &optionsButton, &relearnButton })
        addAndMakeVisible (c);

    inMeter.setTitle ("IN");
    outMeter.setTitle ("OUT");
    inMeter.setTooltip ("Input peak / RMS (click to clear clip)");
    outMeter.setTooltip ("Output peak / RMS (click to clear clip)");
    vu.setLabel ("VU OUT");
    limGr.setTooltip ("Limiter gain reduction (the number is the deepest reduction shown)");
    scope.setTooltip ("Vectorscope: left and right, rotated 45 degrees. Mono is a vertical line");
    correlation.setTooltip ("Correlation of left and right: +1 mono, 0 wide, -1 out of phase");

    for (int i = 0; i < loudness::kNumTargets; ++i)
        targetBox.addItem (loudness::kTargets[i].name + juce::String (i == loudness::kCustomTarget ? "" : "  " + juce::String (loudness::kTargets[i].integratedLufs, 0) + " LUFS / "
                                                                       + juce::String (loudness::kTargets[i].truePeakDb, 0) + " dBTP"),
                           i + 1);
    {
        const auto [savedIndex, savedCustom] = proc.getLoudnessTarget();
        targetIndex = juce::jlimit (0, loudness::kNumTargets - 1, savedIndex);
        customLufs = loudness::clampCustom (savedCustom);
    }
    targetBox.setSelectedId (targetIndex + 1, juce::dontSendNotification);
    targetBox.setTooltip ("Loudness target: integrated loudness and true-peak ceiling");
    targetBox.onChange = [this] { selectTarget (targetBox.getSelectedId() - 1); };
    customDown.setTooltip ("Custom target: down 0.5 LU");
    customUp.setTooltip ("Custom target: up 0.5 LU");
    customDown.onClick = [this] { customLufs = loudness::clampCustom (customLufs - loudness::kCustomStepLu); proc.setLoudnessTarget (targetIndex, customLufs); };
    customUp.onClick = [this] { customLufs = loudness::clampCustom (customLufs + loudness::kCustomStepLu); proc.setLoudnessTarget (targetIndex, customLufs); };
    resetButton.setTooltip ("Reset integrated loudness, LRA, maxima and the overs counter");
    resetButton.onClick = [this] { proc.getStrip().meterHub().resetIntegrated(); };
    optionsButton.setTooltip ("Loudness integration options");
    optionsButton.onClick = [this] { showOptionsMenu(); };
    relearnButton.setTooltip ("Restart the Level Match learn pass");
    relearnButton.onClick = [this] { proc.relearnLevelMatch(); };
    selectTarget (targetIndex);

    startTimerHz (30);
}

MasterPanel::~MasterPanel()
{
    stopTimer();
}

void MasterPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelHeader);
    g.fillRoundedRectangle (r.withHeight ((float) kHeader), 6.0f);
    g.fillRect (r.withTop (kHeader - 6.0f).withHeight (6.0f));
    g.setColour (colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (13.0f, true));
    g.drawText ("MASTER", getLocalBounds().removeFromTop (kHeader).reduced (10, 0), juce::Justification::centredLeft);

    for (auto& [lr, text] : labels) drawSectionLabel (g, lr, text, colours::master);
}

void MasterPanel::resized()
{
    labels.clear();
    blocks.clear();
    layoutBlocksFrom (getLocalBounds());
}

void MasterPanel::layoutBlocksFrom (juce::Rectangle<int> whole)
{
    auto r = whole;
    r.removeFromTop (kHeader + kGap);
    r = r.reduced (kPad, 0);
    r.removeFromBottom (kPad);

    // Gains: input, output, operating level.
    {
        auto gains = r.removeFromTop (kKnobH);
        blocks.push_back ({ "gains", gains });
        const int third = gains.getWidth() / 3;
        inGain->setBounds (gains.removeFromLeft (third));
        outGain->setBounds (gains.removeFromLeft (third));
        vuRef->setBounds (gains);
    }
    r.removeFromTop (kGap);

    // Polarity, VU source, monitor.
    {
        auto choices = r.removeFromTop (kRowH);
        blocks.push_back ({ "choices", choices });
        const int w = (choices.getWidth() - 2 * kGap) / 3;
        phase->setBounds (choices.removeFromLeft (w));
        choices.removeFromLeft (kGap);
        vuSource->setBounds (choices.removeFromLeft (w));
        choices.removeFromLeft (kGap);
        monitor->setBounds (choices);
    }
    r.removeFromTop (kGap);

    // Level meters (IN, OUT) with their dB scale, and the VU needle.
    {
        auto meters = r.removeFromTop (kMeterH);
        blocks.push_back ({ "meters", meters });
        meterScale.setBounds (meters.removeFromLeft (kMeterScaleW));
        inMeter.setBounds (meters.removeFromLeft (kMeterW));
        meters.removeFromLeft (kGap);
        outMeter.setBounds (meters.removeFromLeft (kMeterW));
        meters.removeFromLeft (8);
        vu.setBounds (meters);
    }
    r.removeFromTop (kGap);

    // Limiter: mode, ceiling, release, gain reduction bar and the 10 s history.
    {
        labels.push_back ({ r.removeFromTop (kLabelH), "LIMITER" });
        r.removeFromTop (2);
        auto row = r.removeFromTop (kRowH);
        limOn->setBounds (row.removeFromLeft (row.getWidth() * 2 / 5));
        row.removeFromLeft (kGap);
        lookahead->setBounds (row);
        r.removeFromTop (2);
        auto knobs = r.removeFromTop (kKnobH);
        blocks.push_back ({ "limiter", knobs });
        ceiling->setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
        limRelease->setBounds (knobs);
        r.removeFromTop (2);
        limGr.setBounds (r.removeFromTop (kGrH));
        r.removeFromTop (kGap);
        auto hist = r.removeFromTop (kHistH);
        blocks.push_back ({ "history", hist });
        grHistory.setBounds (hist);
        r.removeFromTop (kGap);
    }

    // Level Match: mode and relearn, then the trim and the status.
    {
        labels.push_back ({ r.removeFromTop (kLabelH), "LEVEL MATCH" });
        r.removeFromTop (2);
        auto row = r.removeFromTop (kRowH);
        match->setBounds (row.removeFromLeft (row.getWidth() / 2));
        row.removeFromLeft (kGap);
        relearnButton.setBounds (row);
        r.removeFromTop (2);
        auto area = r.removeFromTop (kKnobH);
        blocks.push_back ({ "match", area });
        matchTrim->setBounds (area.removeFromLeft (kTrimW));
        area.removeFromLeft (kGap);
        matchView.setBounds (area.withTrimmedTop (8));
        r.removeFromTop (kGap);
    }

    // Loudness: label with RESET and OPTIONS, target selector, readouts, vectorscope and correlation.
    {
        auto labelRow = r.removeFromTop (kLabelH);
        optionsButton.setBounds (labelRow.removeFromRight (kOptionsW).withSizeKeepingCentre (kOptionsW, kLabelH));
        resetButton.setBounds (labelRow.removeFromRight (kResetW).withSizeKeepingCentre (kResetW, kLabelH));
        labels.push_back ({ labelRow.withTrimmedRight (4), "LOUDNESS" });
        r.removeFromTop (2);

        const bool custom = targetIndex == loudness::kCustomTarget;
        auto tRow = r.removeFromTop (kRowH);
        customUp.setVisible (custom);
        customDown.setVisible (custom);
        if (custom)
        {
            customUp.setBounds (tRow.removeFromRight (kCustomBtnW));
            tRow.removeFromRight (2);
            customDown.setBounds (tRow.removeFromRight (kCustomBtnW));
            tRow.removeFromRight (kGap);
        }
        targetBox.setBounds (tRow);
        r.removeFromTop (2);

        auto loud = r.removeFromTop (kLoudH);
        blocks.push_back ({ "loudness", loud });
        auto sc = loud.removeFromRight (juce::roundToInt ((float) loud.getWidth() * kScopeShare));
        loud.removeFromRight (kGap + 2);
        readouts.setBounds (loud);
        blocks.push_back ({ "readouts", loud });

        scopeArea = sc;
        auto corr = sc.removeFromBottom (kCorrH);
        const int side = juce::jmin (sc.getWidth(), sc.getHeight());
        scope.setBounds (sc.withSizeKeepingCentre (side, side));
        correlation.setBounds (corr);
        blocks.push_back ({ "scope", scope.getBounds() });
        blocks.push_back ({ "correlation", corr });
    }
}

void MasterPanel::setTarget (int index, double customTargetLufs)
{
    customLufs = loudness::clampCustom (customTargetLufs);
    targetBox.setSelectedId (juce::jlimit (0, loudness::kNumTargets - 1, index) + 1, juce::dontSendNotification);
    selectTarget (index);
}

void MasterPanel::selectTarget (int index)
{
    targetIndex = juce::jlimit (0, loudness::kNumTargets - 1, index);
    proc.setLoudnessTarget (targetIndex, customLufs);
    resized();
    repaint();
}

void MasterPanel::showOptionsMenu()
{
    auto& hub = proc.getStrip().meterHub();
    juce::PopupMenu menu;
    menu.addItem (1, "Pause integration when transport stops", true, hub.pauseIntegrationWhenStopped());
    menu.addItem (2, "Reset on play", true, hub.resetIntegrationOnPlay());
    juce::Component::SafePointer<MasterPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton), [safe] (int result)
    {
        if (safe == nullptr) return;
        auto& h = safe->proc.getStrip().meterHub();
        if (result == 1) h.setPauseIntegrationWhenStopped (! h.pauseIntegrationWhenStopped());
        if (result == 2) h.setResetIntegrationOnPlay (! h.resetIntegrationOnPlay());
    });
}

int MasterPanel::vuSourceIndex() const
{
    auto* prm = proc.param (P::vuSource);
    return (int) std::lround (prm->convertFrom0to1 (prm->getValue()));
}

float MasterPanel::opLevelDb() const
{
    auto* prm = proc.param (P::opLevel);
    return prm->convertFrom0to1 (prm->getValue());
}

void MasterPanel::updateMeters (const MeterSummary& m)
{
    inMeter.setLevels (m.inPeak[0], m.inPeak[1], m.inRms[0], m.inRms[1]);
    outMeter.setLevels (m.outPeak[0], m.outPeak[1], m.outRms[0], m.outRms[1]);
    limGr.setGr (m.limGr);

    const int src = vuSourceIndex();
    const float ref = opLevelDb();
    vu.setReference (ref);
    if (src == 1) return; // output VU: the MeterHub's calibrated ballistics, read by the timer

    float target;
    if (src == 2)
    {
        target = m.gateGr + m.ds1Gr + m.ds2Gr + m.compGr + m.limGr; // total gain reduction
        vu.setLabel ("GR");
    }
    else
    {
        const float power = 0.5f * (m.inRms[0] * m.inRms[0] + m.inRms[1] * m.inRms[1]);
        target = 10.0f * std::log10 (std::max (power, 1.0e-12f)) - ref;
        vu.setLabel ("VU IN");
    }
    vuValue += (target - vuValue) * kVuSmoothing;
    vu.setValue (vuValue);
}

void MasterPanel::updateLoudness (double, double, double, float, float) {}

void MasterPanel::timerCallback()
{
    auto& hub = proc.getStrip().meterHub();
    const MeterSnapshot s = hub.read();

    if (vuSourceIndex() == 1) vu.setValue (s.vuDb - opLevelDb());

    correlation.setValue (s.correlation);

    loudness::Target target = loudness::kTargets[(size_t) targetIndex];
    if (targetIndex == loudness::kCustomTarget) target.integratedLufs = customLufs;
    readouts.setValues ({ s.momentary, s.shortTerm, s.integrated, s.lra, s.truePeakDb, s.plr, s.overs }, target);

    grHistory.setHistory (grBuf.data(), hub.readGrHistory (grBuf.data(), (int) grBuf.size()));

    for (;;)
    {
        const size_t got = hub.scope().pop (scopeBuf.data(), scopeBuf.size());
        if (got > 0) scope.pushPairs (scopeBuf.data(), got);
        if (got < scopeBuf.size()) break;
    }

    matchView.setStatus (proc.getLevelMatchStatus());
}
} // namespace nl

#include "PluginEditor.h"

namespace nl
{
NorthlineEditor::NorthlineEditor (NorthlineProcessor& p)
    : AudioProcessorEditor (p), proc (p), topBar (p), master (p)
{
    setLookAndFeel (&lnf);
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (master);

    panels[modPre] = std::make_unique<PrePanel> (p);
    panels[modGate] = std::make_unique<GatePanel> (p);
    panels[modDs] = std::make_unique<DsPanel> (p);
    panels[modEq] = std::make_unique<EqPanel> (p);
    panels[modComp] = std::make_unique<CompPanel> (p);
    for (auto& panel : panels)
    {
        content.addAndMakeVisible (*panel);
        panel->onMove = [this] (int id, int delta) { proc.moveModule (id, delta); layoutModules(); topBar.refresh(); };
    }

    topBar.onFocusChanged = [this] { updateFocus(); };
    topBar.onScaleChanged = [this] { applyScale(); };
    master.onResetLoudness = [this] { loudness.resetIntegrated(); };

    frameBuf.resize (256);
    sampleBuf.resize (16384);
    proc.getStrip().setSampleStreamingEnabled (true);

    applyScale();
    startTimerHz (30);
}

NorthlineEditor::~NorthlineEditor()
{
    stopTimer();
    proc.getStrip().setSampleStreamingEnabled (false);
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void NorthlineEditor::applyScale()
{
    const float s = juce::jlimit (0.5f, 2.0f, proc.getUiScale());
    content.setTransform (juce::AffineTransform::scale (s));
    setSize (juce::roundToInt (kBaseW * s), juce::roundToInt (kBaseH * s));
}

void NorthlineEditor::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void NorthlineEditor::resized()
{
    content.setBounds (0, 0, kBaseW, kBaseH);
    topBar.setBounds (8, 8, kBaseW - 16, 44);
    master.setBounds (kBaseW - 8 - 222, 60, 222, kBaseH - 68);
    layoutModules();
}

void NorthlineEditor::layoutModules()
{
    const auto order = proc.getOrder();
    int x = 8;
    for (int id : order)
    {
        auto& panel = panels[(size_t) id];
        panel->setBounds (x, 60, panel->preferredWidth(), kBaseH - 68);
        x += panel->preferredWidth() + 6;
    }
    lastOrder = order;
}

void NorthlineEditor::updateFocus()
{
    const auto& focus = proc.getFocus();
    const bool focusMode = topBar.isFocusMode() && ! focus.empty();
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        if (auto* t = dynamic_cast<FocusTarget*> (&c))
        {
            const bool hl = std::find (focus.begin(), focus.end(), t->focusParam()) != focus.end();
            t->setFocusState (hl, focusMode && ! hl);
        }
        for (auto* child : c.getChildren()) visit (*child);
    };
    visit (content);
    lastFocus = focus;
    lastFocusMode = topBar.isFocusMode();
}

void NorthlineEditor::timerCallback()
{
    auto& strip = proc.getStrip();

    // Drain per-block meter frames.
    MeterSummary sum;
    float inMsAcc[2] {}, outMsAcc[2] {};
    int samplesAcc = 0;
    MeterFrame last;
    bool any = false;
    for (;;)
    {
        const size_t got = strip.meterFrames().pop (frameBuf.data(), frameBuf.size());
        for (size_t i = 0; i < got; ++i)
        {
            const auto& f = frameBuf[i];
            for (int c = 0; c < 2; ++c)
            {
                sum.inPeak[c] = std::max (sum.inPeak[c], f.inPeak[c]);
                sum.outPeak[c] = std::max (sum.outPeak[c], f.outPeak[c]);
                inMsAcc[c] += f.inMs[c] * (float) f.numSamples;
                outMsAcc[c] += f.outMs[c] * (float) f.numSamples;
            }
            samplesAcc += f.numSamples;
            sum.gateGr = std::min (sum.gateGr, f.gateGr);
            sum.ds1Gr = std::min (sum.ds1Gr, f.ds1Gr);
            sum.ds2Gr = std::min (sum.ds2Gr, f.ds2Gr);
            sum.compGr = std::min (sum.compGr, f.compGr);
            sum.limGr = std::min (sum.limGr, f.limGr);
            last = f;
            any = true;
            if (f.blockUs > 0.0f) cpuAvg = 0.97f * cpuAvg + 0.03f * (100.0f * f.totalUs / f.blockUs);
        }
        if (got < frameBuf.size()) break;
    }
    if (samplesAcc > 0)
        for (int c = 0; c < 2; ++c)
        {
            sum.inRms[c] = std::sqrt (inMsAcc[c] / (float) samplesAcc);
            sum.outRms[c] = std::sqrt (outMsAcc[c] / (float) samplesAcc);
        }
    master.updateMeters (sum);
    MeterFrame gr;
    gr.gateGr = sum.gateGr;
    gr.ds1Gr = sum.ds1Gr;
    gr.ds2Gr = sum.ds2Gr;
    gr.compGr = sum.compGr;
    for (auto& panel : panels) panel->updateMeters (gr);

    // Loudness / true peak from the output sample stream.
    if (strip.getSampleRate() != meterRate)
    {
        meterRate = strip.getSampleRate();
        loudness.prepare (meterRate);
    }
    for (;;)
    {
        const size_t got = strip.outputSamples().pop (sampleBuf.data(), sampleBuf.size());
        if (got > 0) loudness.process (sampleBuf.data(), got, strip.getNumChannels());
        if (got < sampleBuf.size()) break;
    }
    master.updateLoudness (loudness.getMomentary(), loudness.getShortTerm(), loudness.getIntegrated(), loudness.getTruePeakDb(), loudness.getCorrelation());

    // CPU readouts at ~5 Hz.
    if (any && (++tick % 6) == 0)
    {
        for (auto& panel : panels) panel->setCpuUs (last.moduleUs[panel->getModuleId()], last.blockUs);
        topBar.setCpu (cpuAvg);
    }

    // Whole-state changes (presets, undo, A/B, host state restore).
    const int gen = proc.stateGeneration.load();
    if (gen != lastGeneration)
    {
        lastGeneration = gen;
        topBar.refresh();
        if (proc.getOrder() != lastOrder) layoutModules();
        updateFocus();
    }
    if (topBar.isFocusMode() != lastFocusMode) updateFocus();
    if ((tick % 10) == 0) topBar.refresh();
}
} // namespace nl

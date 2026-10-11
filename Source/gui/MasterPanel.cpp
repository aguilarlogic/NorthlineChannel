#include "MasterPanel.h"

namespace nl
{
namespace
{
constexpr int kHeader = 32, kPad = 8, kKnobH = 64, kRowH = 22, kGap = 6;

juce::String fmtLufs (double v) { return v <= -199.0 ? juce::String ("-inf") : juce::String (v, 1); }
} // namespace

MasterPanel::MasterPanel (NorthlineProcessor& p) : proc (p)
{
    const auto a = colours::master;
    inGain = add<Knob> (p, P::inGain, "Input", a);
    outGain = add<Knob> (p, P::outGain, "Output", a);
    vuRef = add<Knob> (p, P::opLevel, "Op Level", a);
    phase = add<ChoiceBox> (p, P::inPolarity);
    vuSource = add<ChoiceBox> (p, P::vuSource);
    limOn = add<Chip> (p, P::limOn, "LIMIT", a);
    lookahead = add<ChoiceBox> (p, P::limMode);
    lookahead->setTooltip ("Limiter mode. Safety Clip: zero latency. Brickwall / True Peak: 1.5 ms lookahead, reported to the host");
    ceiling = add<Knob> (p, P::limCeiling, "Ceiling", a);
    limRelease = add<Knob> (p, P::limRelease, "Release", a);
    match = add<ChoiceBox> (p, P::matchMode);
    match->setTooltip ("Matches output loudness to input loudness so you judge processing, not volume");
    monitor = add<ChoiceBox> (p, P::monitor);
    for (auto* c : std::initializer_list<juce::Component*> { &inMeter, &outMeter, &vu, &limGr, &resetLoudness })
        addAndMakeVisible (c);
    resetLoudness.setTooltip ("Reset integrated loudness and true-peak hold");
    resetLoudness.onClick = [this] { if (onResetLoudness) onResetLoudness(); };
    inMeter.setTooltip ("Input peak / RMS (click to clear clip)");
    outMeter.setTooltip ("Output peak / RMS (click to clear clip)");
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

    g.setFont (NorthlineLookAndFeel::font (10.0f, true));
    g.setColour (colours::dim);
    auto ml = meterLabelArea;
    g.drawText ("IN", ml.removeFromLeft (ml.getWidth() / 2), juce::Justification::centred);
    g.drawText ("OUT", ml, juce::Justification::centred);

    // Loudness readouts.
    auto la = loudnessArea;
    auto cell = [&] (juce::Rectangle<int> c, const juce::String& name, const juce::String& value, const juce::String& unit)
    {
        g.setColour (colours::dim);
        g.setFont (NorthlineLookAndFeel::font (10.0f, true));
        g.drawText (name, c.removeFromTop (12), juce::Justification::centredLeft);
        g.setColour (colours::text);
        g.setFont (NorthlineLookAndFeel::font (15.0f, true));
        g.drawText (value, c.removeFromLeft (52), juce::Justification::centredLeft);
        g.setColour (colours::faint);
        g.setFont (NorthlineLookAndFeel::font (10.0f));
        g.drawText (unit, c, juce::Justification::centredLeft);
    };
    auto r1 = la.removeFromTop (30);
    cell (r1.removeFromLeft (r1.getWidth() / 2), "MOMENTARY", lufsM, "LUFS");
    cell (r1, "SHORT-TERM", lufsS, "LUFS");
    auto r2 = la.removeFromTop (30);
    cell (r2.removeFromLeft (r2.getWidth() / 2), "INTEGRATED", lufsI, "LUFS");
    cell (r2, "TRUE PEAK", tp, "dBTP");
    la.removeFromTop (4);
    auto cr = la.removeFromTop (12).toFloat();
    g.setColour (colours::dim);
    g.setFont (NorthlineLookAndFeel::font (10.0f, true));
    g.drawText ("CORR", cr.removeFromLeft (34.0f), juce::Justification::centredLeft);
    g.setColour (colours::track);
    g.fillRoundedRectangle (cr, 2.0f);
    const float cx = cr.getCentreX(), px = cx + corr * cr.getWidth() * 0.5f;
    g.setColour (corr < 0.0f ? colours::meterRed : colours::meterGreen);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, px), cr.getY(), juce::jmax (cx, px), cr.getBottom()));
    g.setColour (colours::text.withAlpha (0.5f));
    g.drawVerticalLine ((int) cx, cr.getY(), cr.getBottom());
}

void MasterPanel::resized()
{
    labels.clear();
    auto r = getLocalBounds();
    r.removeFromTop (kHeader);
    r = r.reduced (kPad, kGap);

    {
        auto k = r.removeFromTop (kKnobH);
        const int w = k.getWidth() / 3;
        inGain->setBounds (k.removeFromLeft (w));
        outGain->setBounds (k.removeFromLeft (w));
        vuRef->setBounds (k);
    }
    r.removeFromTop (4);
    {
        auto row = r.removeFromTop (kRowH);
        phase->setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
        row.removeFromLeft (4);
        vuSource->setBounds (row);
    }
    r.removeFromTop (kGap);
    {
        auto m = r.removeFromTop (128);
        auto meters = m.removeFromLeft (64);
        meterLabelArea = meters.removeFromBottom (12);
        inMeter.setBounds (meters.removeFromLeft (30));
        meters.removeFromLeft (4);
        outMeter.setBounds (meters.removeFromLeft (30));
        m.removeFromLeft (8);
        vu.setBounds (m.withSizeKeepingCentre (m.getWidth(), juce::jmin (m.getHeight(), 96)));
    }
    r.removeFromTop (kGap);

    labels.push_back ({ r.removeFromTop (14), "LIMITER" });
    r.removeFromTop (4);
    {
        auto row = r.removeFromTop (kRowH);
        limOn->setBounds (row.removeFromLeft (row.getWidth() * 2 / 5));
        row.removeFromLeft (4);
        lookahead->setBounds (row);
    }
    r.removeFromTop (4);
    {
        auto k = r.removeFromTop (kKnobH);
        ceiling->setBounds (k.removeFromLeft (k.getWidth() / 2));
        limRelease->setBounds (k);
    }
    r.removeFromTop (3);
    limGr.setBounds (r.removeFromTop (11));
    r.removeFromTop (kGap);
    {
        auto row = r.removeFromTop (kRowH);
        match->setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
        row.removeFromLeft (4);
        monitor->setBounds (row);
    }
    r.removeFromTop (kGap);
    auto lh = r.removeFromTop (14);
    resetLoudness.setBounds (lh.removeFromRight (48).withSizeKeepingCentre (48, 14));
    labels.push_back ({ lh, "LOUDNESS" });
    r.removeFromTop (4);
    loudnessArea = r.removeFromTop (82);
}

void MasterPanel::updateMeters (const MeterSummary& m)
{
    inMeter.setLevels (m.inPeak[0], m.inPeak[1], m.inRms[0], m.inRms[1]);
    outMeter.setLevels (m.outPeak[0], m.outPeak[1], m.outRms[0], m.outRms[1]);
    limGr.setGr (m.limGr);

    const int src = (int) std::lround (proc.param (P::vuSource)->convertFrom0to1 (proc.param (P::vuSource)->getValue()));
    const float ref = proc.param (P::opLevel)->convertFrom0to1 (proc.param (P::opLevel)->getValue());
    float target;
    if (src == 2)
    {
        target = m.gateGr + m.ds1Gr + m.ds2Gr + m.compGr + m.limGr; // total gain reduction
        vu.setLabel ("GR");
    }
    else
    {
        const float* rms = src == 0 ? m.inRms : m.outRms;
        const float power = 0.5f * (rms[0] * rms[0] + rms[1] * rms[1]);
        target = 10.0f * std::log10 (std::max (power, 1.0e-12f)) - ref;
        vu.setLabel (src == 0 ? "VU IN" : "VU OUT");
    }
    // VU ballistics: ~300 ms to 99 % at a 30 Hz refresh.
    vuValue += (target - vuValue) * 0.36f;
    vu.setValue (vuValue);
}

void MasterPanel::updateLoudness (double momentary, double shortTerm, double integrated, float truePeak, float correlation)
{
    const auto m = fmtLufs (momentary), s = fmtLufs (shortTerm), i = fmtLufs (integrated);
    const auto t = truePeak <= -199.0f ? juce::String ("-inf") : juce::String (truePeak, 1);
    if (m != lufsM || s != lufsS || i != lufsI || t != tp || std::abs (correlation - corr) > 0.01f)
    {
        lufsM = m; lufsS = s; lufsI = i; tp = t; corr = correlation;
        repaint (loudnessArea);
    }
}
} // namespace nl

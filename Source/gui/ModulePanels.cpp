#include "ModulePanels.h"
#include "../dsp/EqModule.h"

namespace nl
{
namespace
{
constexpr int kHeader = 32, kFooter = 18, kPad = 8, kKnobH = 64, kRowH = 22, kGap = 6;
}

//==============================================================================
ModulePanel::ModulePanel (NorthlineProcessor& p, int id, int onParam, int modeParam)
    : proc (p), moduleId (id), accent (colours::forModule (id))
{
    power = std::make_unique<Chip> (p, onParam, moduleName (id), accent);
    addAndMakeVisible (*power);
    if (modeParam >= 0)
    {
        mode = std::make_unique<Segmented> (p, modeParam, accent);
        addAndMakeVisible (*mode);
    }
    for (auto* b : { &moveLeft, &moveRight })
    {
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        addAndMakeVisible (b);
    }
    moveLeft.setTooltip ("Move module earlier in the chain");
    moveRight.setTooltip ("Move module later in the chain");
    moveLeft.onClick = [this] { if (onMove) onMove (moduleId, -1); };
    moveRight.onClick = [this] { if (onMove) onMove (moduleId, +1); };

    onWatcher = std::make_unique<juce::ParameterAttachment> (*p.param (onParam), [this] (float) { updateBodyEnabled(); });
}

void ModulePanel::updateBodyEnabled()
{
    const bool on = proc.param (power->focusParam())->getValue() >= 0.5f;
    for (auto* c : owned) c->setEnabled (on);
    repaint();
}

Knob* ModulePanel::knob (int index, const juce::String& label)
{
    auto* k = new Knob (proc, index, label, accent);
    owned.add (k);
    addAndMakeVisible (k);
    return k;
}

Chip* ModulePanel::chip (int index, const juce::String& text)
{
    auto* c = new Chip (proc, index, text, accent);
    owned.add (c);
    addAndMakeVisible (c);
    return c;
}

ChoiceBox* ModulePanel::choice (int index)
{
    auto* c = new ChoiceBox (proc, index);
    owned.add (c);
    addAndMakeVisible (c);
    return c;
}

Segmented* ModulePanel::segmented (int index)
{
    auto* s = new Segmented (proc, index, accent);
    owned.add (s);
    addAndMakeVisible (s);
    return s;
}

void ModulePanel::row (juce::Rectangle<int> r, std::initializer_list<juce::Component*> items, int gap)
{
    const int n = (int) items.size();
    if (n == 0) return;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    for (auto* c : items)
    {
        auto cell = r.removeFromLeft (w);
        r.removeFromLeft (gap);
        if (c != nullptr) c->setBounds (cell);
    }
}

juce::Rectangle<int> ModulePanel::sectionLabel (juce::Rectangle<int>& body, const juce::String& text)
{
    auto r = body.removeFromTop (14);
    labels.push_back ({ r, text });
    body.removeFromTop (4);
    return r;
}

void ModulePanel::setCpuUs (float us, float blockUs)
{
    const auto t = us < 0.05f ? juce::String ("off - 0 us")
                              : (us < 9.95f ? juce::String (us, 1) : juce::String (juce::roundToInt (us))) + " us  " + juce::String (blockUs > 0.0f ? 100.0f * us / blockUs : 0.0f, 2) + "%";
    if (t != cpuText) { cpuText = t; repaint (getLocalBounds().removeFromBottom (kFooter)); }
}

void ModulePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelHeader);
    g.fillRoundedRectangle (r.withHeight ((float) kHeader), 6.0f);
    g.fillRect (r.withTop (kHeader - 6.0f).withHeight (6.0f));
    const bool on = proc.param (power->focusParam())->getValue() >= 0.5f;
    g.setColour (accent.withAlpha (on ? 0.9f : 0.25f));
    g.fillRect (r.withHeight (2.0f).reduced (6.0f, 0.0f));
    g.setColour (colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    for (auto& [lr, text] : labels) drawSectionLabel (g, lr, text, accent.withAlpha (on ? 1.0f : 0.5f));

    g.setColour (colours::faint);
    g.setFont (NorthlineLookAndFeel::font (10.5f));
    g.drawText ("CPU " + cpuText, getLocalBounds().removeFromBottom (kFooter).reduced (kPad, 0), juce::Justification::centredRight);
}

void ModulePanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeader).reduced (6, 5);
    power->setBounds (header.removeFromLeft (juce::jmax (40, 18 + 9 * juce::String (moduleName (moduleId)).length())));
    header.removeFromLeft (6);
    if (mode) mode->setBounds (header.removeFromRight (juce::jmin (header.getWidth(), mode->getNumChildComponents() * 34)));

    // Chain-order buttons live in the footer, left of the CPU readout.
    auto footer = r.removeFromBottom (kFooter).reduced (6, 2);
    moveLeft.setBounds (footer.removeFromLeft (18));
    footer.removeFromLeft (2);
    moveRight.setBounds (footer.removeFromLeft (18));
    labels.clear();
    layoutBody (r.reduced (kPad, kGap));
    updateBodyEnabled();
}

//==============================================================================
PrePanel::PrePanel (NorthlineProcessor& p) : ModulePanel (p, modPre, P::preOn, P::preMode)
{
    satOn = chip (P::satOn, "SAT");
    satType = choice (P::satType);
    hq = chip (P::quality, "HQ");
    hq->setTooltip ("Global quality. HQ oversamples the nonlinear stages (lower aliasing)");
    drive = knob (P::satDrive, "Drive");
    thumpOn = chip (P::weightOn, "WEIGHT");
    thumpAmt = choice (P::weightAmt);
    hpOn = chip (P::hpOn, "HP");
    hpSlope = choice (P::hpSlope);
    lpOn = chip (P::lpOn, "LP");
    lpSlope = choice (P::lpSlope);
    hpFreq = knob (P::hpFreq, "HP Freq");
    hpRes = knob (P::hpRes, "Res");
    lpFreq = knob (P::lpFreq, "LP Freq");
    lpRes = knob (P::lpRes, "Res");
    target = choice (P::filtTarget);
    target->setTooltip ("Filter target in MS mode: both, mid only or side only (e.g. high-pass the side for mono bass)");
}

void PrePanel::layoutBody (juce::Rectangle<int> r)
{
    sectionLabel (r, "SATURATION");
    row (r.removeFromTop (kRowH), { satOn, satType, hq });
    r.removeFromTop (kGap);
    auto kr = r.removeFromTop (kKnobH);
    drive->setBounds (kr.removeFromLeft (kr.getWidth() / 2));
    kr.removeFromLeft (8);
    auto stack = kr.withSizeKeepingCentre (kr.getWidth(), kRowH * 2 + 6);
    thumpOn->setBounds (stack.removeFromTop (kRowH));
    stack.removeFromTop (6);
    thumpAmt->setBounds (stack);
    r.removeFromTop (kGap + 4);

    sectionLabel (r, "FILTERS");
    auto fr = r.removeFromTop (kRowH);
    row (fr, { hpOn, hpSlope, lpOn, lpSlope });
    r.removeFromTop (kGap);
    row (r.removeFromTop (kKnobH), { hpFreq, hpRes, lpFreq, lpRes }, 2);
    r.removeFromTop (kGap);
    auto tr = r.removeFromTop (kRowH);
    target->setBounds (tr.removeFromRight (tr.getWidth() / 2));
    labels.push_back ({ tr, "MS TARGET" });
}

//==============================================================================
GatePanel::GatePanel (NorthlineProcessor& p) : ModulePanel (p, modGate, P::gateOn, P::gateMode)
{
    type = segmented (P::gateType);
    addAndMakeVisible (gr);
    thresh = knob (P::gateThresh, "Thresh");
    floorK = knob (P::gateFloor, "Floor");
    hyst = knob (P::gateHyst, "Hyst");
    attack = knob (P::gateAttack, "Attack");
    hold = knob (P::gateHold, "Hold");
    release = knob (P::gateRelease, "Release");
    ratio = knob (P::gateRatio, "Ratio");
    scHp = knob (P::gateScHp, "SC HP");
    scLp = knob (P::gateScLp, "SC LP");
    ext = chip (P::gateExt, "EXT SC");
    listen = chip (P::gateListen, "LISTEN");
}

void GatePanel::layoutBody (juce::Rectangle<int> r)
{
    type->setBounds (r.removeFromTop (kRowH));
    r.removeFromTop (kGap);
    gr.setBounds (r.removeFromTop (12));
    r.removeFromTop (kGap);
    row (r.removeFromTop (kKnobH), { thresh, floorK, hyst }, 2);
    r.removeFromTop (kGap);
    row (r.removeFromTop (kKnobH), { attack, hold, release }, 2);
    r.removeFromTop (kGap);
    sectionLabel (r, "SIDECHAIN");
    row (r.removeFromTop (kKnobH), { ratio, scHp, scLp }, 2);
    r.removeFromTop (kGap);
    row (r.removeFromTop (kRowH), { ext, listen });
}

//==============================================================================
DsPanel::DsPanel (NorthlineProcessor& p) : ModulePanel (p, modDs, P::dsOn, P::dsMode)
{
    on1 = chip (P::ds1On, "BAND 1");
    type1 = choice (P::ds1Type);
    freq1 = knob (P::ds1Freq, "Freq");
    thresh1 = knob (P::ds1Thresh, "Thresh");
    range1 = knob (P::ds1Range, "Range");
    on2 = chip (P::ds2On, "BAND 2");
    type2 = choice (P::ds2Type);
    freq2 = knob (P::ds2Freq, "Freq");
    thresh2 = knob (P::ds2Thresh, "Thresh");
    range2 = knob (P::ds2Range, "Range");
    release = knob (P::dsRelease, "Release");
    out = knob (P::dsOut, "Output");
    relative = chip (P::dsRelative, "REL");
    relative->setTooltip ("Relative detection: threshold follows the overall level, so de-essing stays consistent on loud and quiet passages");
    ext = chip (P::dsExt, "EXT");
    listen = choice (P::dsListen);
    addAndMakeVisible (gr1);
    addAndMakeVisible (gr2);
}

void DsPanel::layoutBody (juce::Rectangle<int> r)
{
    auto band = [&] (Chip* on, ChoiceBox* type, Knob* f, Knob* t, Knob* rg, GrBar& g)
    {
        auto top = r.removeFromTop (kRowH);
        on->setBounds (top.removeFromLeft (top.getWidth() / 2 - 2));
        top.removeFromLeft (4);
        type->setBounds (top);
        r.removeFromTop (4);
        row (r.removeFromTop (kKnobH), { f, t, rg }, 2);
        r.removeFromTop (3);
        g.setBounds (r.removeFromTop (11));
        r.removeFromTop (kGap + 2);
    };
    band (on1, type1, freq1, thresh1, range1, gr1);
    band (on2, type2, freq2, thresh2, range2, gr2);
    row (r.removeFromTop (kKnobH), { release, out }, 2);
    r.removeFromTop (kGap);
    auto br = r.removeFromTop (kRowH);
    row (br, { relative, ext, listen });
}

//==============================================================================
EqDisplay::EqDisplay (NorthlineProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (20);
}

float EqDisplay::xForFreq (float f) const
{
    return (float) getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float EqDisplay::freqForX (float x) const
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / (float) getWidth()));
}

float EqDisplay::yForDb (float db) const
{
    const float h = (float) getHeight() - 4.0f;
    return 2.0f + h * 0.5f * (1.0f - db / kRange);
}

float EqDisplay::dbForY (float y) const
{
    const float h = (float) getHeight() - 4.0f;
    return kRange * (1.0f - 2.0f * (y - 2.0f) / h);
}

void EqDisplay::timerCallback()
{
    std::vector<float> v;
    v.reserve (40);
    for (int i = P::eqOn; i <= P::eqHighTarget; ++i) v.push_back (pv (i));
    for (int i : { (int) P::preOn, (int) P::hpOn, (int) P::hpFreq, (int) P::hpSlope, (int) P::hpRes,
                   (int) P::lpOn, (int) P::lpFreq, (int) P::lpSlope, (int) P::lpRes })
        v.push_back (pv (i));
    v.push_back ((float) getWidth());
    if (v != lastValues)
    {
        lastValues = std::move (v);
        rebuild();
        repaint();
    }
}

void EqDisplay::rebuild()
{
    const double fs = 48000.0; // display reference rate
    const int w = juce::jmax (2, getWidth());
    curve.clear();
    fill.clear();
    for (auto& b : bandCurves) b.clear();

    std::array<SvfCoefs, 4> coefs {};
    std::array<bool, 4> active {};
    const bool eqOn = pv (P::eqOn) >= 0.5f;
    for (int b = 0; b < 4; ++b)
    {
        const int o = P::eqLowOn + b * 6;
        active[(size_t) b] = eqOn && pv (o) >= 0.5f && std::abs (pv (o + 3)) > 0.01f;
        coefs[(size_t) b] = EqModule::bandCoefs (b, (int) std::lround (pv (o + 1)), fs, pv (o + 2), pv (o + 3), pv (o + 4));
        handles[(size_t) b] = { xForFreq (pv (o + 2)), yForDb (pv (o) >= 0.5f ? pv (o + 3) : 0.0f) };
    }

    const bool preOn = pv (P::preOn) >= 0.5f;
    SlopeFilter hp, lp;
    const bool hpOn = preOn && pv (P::hpOn) >= 0.5f, lpOn = preOn && pv (P::lpOn) >= 0.5f;
    if (hpOn) { hp.prepare (fs); hp.setParams (true, pv (P::hpFreq), (int) std::lround (pv (P::hpSlope)), pv (P::hpRes)); hp.reset(); hp.updateControl(); }
    if (lpOn) { lp.prepare (fs); lp.setParams (false, pv (P::lpFreq), (int) std::lround (pv (P::lpSlope)), pv (P::lpRes)); lp.reset(); lp.updateControl(); }

    for (int x = 0; x <= w; x += 2)
    {
        const float f = freqForX ((float) x);
        float total = 1.0f;
        for (int b = 0; b < 4; ++b)
        {
            const float m = coefs[(size_t) b].magnitude (f, fs);
            if (active[(size_t) b])
            {
                total *= m;
                const float y = yForDb (juce::Decibels::gainToDecibels (m));
                if (x == 0) bandCurves[(size_t) b].startNewSubPath ((float) x, y);
                else bandCurves[(size_t) b].lineTo ((float) x, y);
            }
        }
        if (hpOn) total *= hp.magnitude (f);
        if (lpOn) total *= lp.magnitude (f);
        const float y = juce::jlimit (-10.0f, (float) getHeight() + 10.0f, yForDb (juce::Decibels::gainToDecibels (total, -60.0f)));
        if (x == 0) curve.startNewSubPath ((float) x, y);
        else curve.lineTo ((float) x, y);
    }
    fill = curve;
    fill.lineTo ((float) w, yForDb (0.0f));
    fill.lineTo (0.0f, yForDb (0.0f));
    fill.closeSubPath();
}

void EqDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff14171c));
    g.fillRoundedRectangle (r, 4.0f);

    g.setFont (NorthlineLookAndFeel::font (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (colours::edge.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (colours::faint);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 2, r.getBottom() - 12, 30, 11), juce::Justification::left);
    }
    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        g.setColour (colours::edge.withAlpha (0.5f));
        g.drawHorizontalLine ((int) yForDb (db), r.getX(), r.getRight());
    }
    g.setColour (colours::edge);
    g.drawHorizontalLine ((int) yForDb (0.0f), r.getX(), r.getRight());

    for (auto& b : bandCurves)
    {
        g.setColour (colours::eq.withAlpha (0.25f));
        g.strokePath (b, juce::PathStrokeType (1.0f));
    }
    g.setColour (colours::eq.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (colours::eq);
    g.strokePath (curve, juce::PathStrokeType (1.8f));

    static const char* names[] { "L", "M", "T", "H" };
    for (int b = 0; b < 4; ++b)
    {
        const auto h = handles[(size_t) b];
        const bool on = pv (P::eqLowOn + b * 6) >= 0.5f;
        g.setColour (on ? colours::eq : colours::faint);
        g.fillEllipse (h.x - 6.0f, h.y - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colours::black);
        g.setFont (NorthlineLookAndFeel::font (9.0f, true));
        g.drawText (names[b], juce::Rectangle<float> (12, 12).withCentre (h), juce::Justification::centred);
    }
}

void EqDisplay::setParam (int index, float plain)
{
    auto* p = proc.param (index);
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragBand = -1;
    float best = 14.0f;
    for (int b = 0; b < 4; ++b)
    {
        const float d = handles[(size_t) b].getDistanceFrom (e.position);
        if (d < best) { best = d; dragBand = b; }
    }
    if (dragBand >= 0)
    {
        const int o = P::eqLowOn + dragBand * 6;
        proc.param (o + 2)->beginChangeGesture();
        proc.param (o + 3)->beginChangeGesture();
        if (pv (o) < 0.5f) { proc.param (o)->beginChangeGesture(); setParam (o, 1.0f); proc.param (o)->endChangeGesture(); }
    }
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    const int o = P::eqLowOn + dragBand * 6;
    setParam (o + 2, juce::jlimit (20.0f, 20000.0f, freqForX (e.position.x)));
    setParam (o + 3, juce::jlimit (-18.0f, 18.0f, dbForY (e.position.y)));
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragBand < 0) return;
    const int o = P::eqLowOn + dragBand * 6;
    proc.param (o + 2)->endChangeGesture();
    proc.param (o + 3)->endChangeGesture();
    dragBand = -1;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int b = 0; b < 4; ++b)
        if (handles[(size_t) b].getDistanceFrom (e.position) < 10.0f)
        {
            const int o = P::eqLowOn + b * 6;
            proc.param (o + 3)->beginChangeGesture();
            setParam (o + 3, 0.0f);
            proc.param (o + 3)->endChangeGesture();
        }
}

//==============================================================================
EqPanel::EqPanel (NorthlineProcessor& p) : ModulePanel (p, modEq, P::eqOn, P::eqMode), display (p)
{
    addAndMakeVisible (display);
    static const char* names[] { "LOW", "MID", "TONE", "HIGH" };
    for (int b = 0; b < 4; ++b)
    {
        const int o = P::eqLowOn + b * 6;
        auto& band = bands[(size_t) b];
        band.on = chip (o, names[b]);
        band.type = choice (o + 1);
        band.freq = knob (o + 2, "Freq");
        band.gain = knob (o + 3, "Gain");
        band.q = knob (o + 4, "Q");
        band.target = choice (o + 5);
        band.target->setTooltip ("Band target in MS mode: both, mid only or side only");
    }
}

void EqPanel::layoutBody (juce::Rectangle<int> r)
{
    display.setBounds (r.removeFromTop (138));
    r.removeFromTop (kGap + 2);
    const int colW = (r.getWidth() - 3 * 4) / 4;
    for (int b = 0; b < 4; ++b)
    {
        auto c = r.removeFromLeft (colW);
        r.removeFromLeft (4);
        auto& band = bands[(size_t) b];
        band.on->setBounds (c.removeFromTop (kRowH));
        c.removeFromTop (4);
        band.type->setBounds (c.removeFromTop (kRowH));
        c.removeFromTop (4);
        band.freq->setBounds (c.removeFromTop (kKnobH));
        band.gain->setBounds (c.removeFromTop (kKnobH));
        band.q->setBounds (c.removeFromTop (kKnobH));
        c.removeFromTop (4);
        band.target->setBounds (c.removeFromTop (kRowH));
    }
}

//==============================================================================
CompPanel::CompPanel (NorthlineProcessor& p) : ModulePanel (p, modComp, P::compOn, P::compMode)
{
    type = segmented (P::compType);
    addAndMakeVisible (gr);
    thresh = knob (P::compThresh, "Thresh");
    ratio = knob (P::compRatio, "Ratio");
    attack = knob (P::compAttack, "Attack");
    release = knob (P::compRelease, "Release");
    mix = knob (P::compMix, "Mix");
    out = knob (P::compOut, "Output");
    scHp = knob (P::compScHp, "SC HP");
    scFreq = knob (P::compScBellFreq, "SC Freq");
    scGain = knob (P::compScBellGain, "SC Gain");
    makeup = chip (P::compMakeup, "AUTO GAIN");
    makeup->setTooltip ("Automatic make-up gain. Off by default so the compressor can be used for ducking");
    ext = chip (P::compExt, "EXT");
    listen = chip (P::compListen, "LISTEN");
}

void CompPanel::layoutBody (juce::Rectangle<int> r)
{
    type->setBounds (r.removeFromTop (kRowH));
    r.removeFromTop (kGap);
    gr.setBounds (r.removeFromTop (12));
    r.removeFromTop (kGap);
    row (r.removeFromTop (kKnobH), { thresh, ratio, attack }, 2);
    r.removeFromTop (kGap);
    row (r.removeFromTop (kKnobH), { release, mix, out }, 2);
    r.removeFromTop (kGap);
    sectionLabel (r, "SIDECHAIN");
    row (r.removeFromTop (kKnobH), { scHp, scFreq, scGain }, 2);
    r.removeFromTop (kGap);
    auto cr = r.removeFromTop (kRowH);
    makeup->setBounds (cr.removeFromLeft (cr.getWidth() * 4 / 9));
    cr.removeFromLeft (4);
    row (cr, { ext, listen });
}
} // namespace nl

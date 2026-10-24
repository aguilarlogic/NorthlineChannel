#include "ModulePanels.h"
#include "ViewUtil.h"
#include "Transient.h"

namespace nl
{
namespace
{
constexpr int kHeader = 32, kFooter = 18, kPad = 8, kGap = 6;
constexpr int kRowH = 22;       // chips, choice boxes, flow rows
constexpr int kRowGap = 4;      // between and inside flow rows
constexpr int kMeterH = 14;     // GR bars
constexpr int kDrawerBar = 18;  // Sidechain heading
// Knob cells: the Knob draws its label in 13 px above and below the dial.
constexpr int kPrimaryDial = 60, kPrimaryCell = kPrimaryDial + 26, kPrimaryMinW = kPrimaryDial + 8;
constexpr int kSecondaryDial = 38, kSecondaryCell = kSecondaryDial + 26, kSecondaryMinW = kSecondaryDial + 6;
constexpr double kMaxFlowStretch = 2.5; // a flow item grows to at most this multiple of its minimum width

// Flow rows: items keep their minimum width and wrap when a row is full.
std::vector<std::vector<int>> flowRows (int width, const std::vector<float>& minW)
{
    std::vector<std::vector<int>> rows;
    std::vector<int> cur;
    float used = 0.0f;
    for (int i = 0; i < (int) minW.size(); ++i)
    {
        float need = minW[(size_t) i] + (cur.empty() ? 0.0f : (float) kRowGap);
        if (! cur.empty() && used + need > (float) width)
        {
            rows.push_back (cur);
            cur.clear();
            used = 0.0f;
            need = minW[(size_t) i];
        }
        cur.push_back (i);
        used += need;
    }
    if (! cur.empty()) rows.push_back (cur);
    return rows;
}

int flowHeight (int width, const std::vector<float>& minW)
{
    const int rows = (int) flowRows (width, minW).size();
    return rows == 0 ? 0 : rows * kRowH + (rows - 1) * kRowGap;
}

void placeFlow (juce::Rectangle<int> area, const std::vector<juce::Component*>& comps, const std::vector<float>& minW)
{
    for (const auto& row : flowRows (area.getWidth(), minW))
    {
        auto line = area.removeFromTop (kRowH);
        area.removeFromTop (kRowGap);
        float sum = 0.0f;
        for (int i : row) sum += minW[(size_t) i];
        const float avail = (float) (line.getWidth() - kRowGap * ((int) row.size() - 1));
        const double scale = juce::jmin (kMaxFlowStretch, (double) avail / (double) juce::jmax (1.0f, sum));
        for (size_t k = 0; k < row.size(); ++k)
        {
            const int i = row[k];
            const int w = (int) std::round (minW[(size_t) i] * scale);
            if (comps[(size_t) i] != nullptr) comps[(size_t) i]->setBounds (line.removeFromLeft (w));
            else line.removeFromLeft (w);
            line.removeFromLeft (kRowGap);
        }
    }
}

int gridCols (int width, int n, int minCellW)
{
    return juce::jlimit (1, juce::jmax (1, n), (width + kRowGap) / (minCellW + kRowGap));
}

int gridHeight (int width, int n, int cellH, int minCellW)
{
    if (n == 0) return 0;
    const int cols = gridCols (width, n, minCellW);
    const int rows = (n + cols - 1) / cols;
    return rows * cellH + (rows - 1) * kRowGap;
}

void placeGrid (juce::Rectangle<int> area, const std::vector<juce::Component*>& comps, int cellH, int minCellW)
{
    const int n = (int) comps.size();
    if (n == 0) return;
    const int cols = gridCols (area.getWidth(), n, minCellW);
    const int cellW = (area.getWidth() - kRowGap * (cols - 1)) / cols;
    for (int i = 0; i < n; ++i)
    {
        const int row = i / cols, col = i % cols;
        if (comps[(size_t) i] != nullptr)
            comps[(size_t) i]->setBounds (area.getX() + col * (cellW + kRowGap), area.getY() + row * (cellH + kRowGap), cellW, cellH);
    }
}

// The sidechain drawer's content for one module, as its fixed block sizes.
int drawerContentHeight (int width, const std::vector<float>& topMinW, int knobCount)
{
    int h = 0;
    if (! topMinW.empty()) h += flowHeight (width, topMinW) + kGap;
    if (knobCount > 0) h += gridHeight (width, knobCount, kSecondaryCell, kSecondaryMinW);
    return h;
}

} // namespace

//==============================================================================
ParamButton::ParamButton (NorthlineProcessor& p, int paramIndex, int activeValue, const juce::String& text)
    : juce::TextButton (text), proc (p), index (paramIndex), active (activeValue)
{
    setClickingTogglesState (false);
    setColour (juce::TextButton::buttonOnColourId, colours::amber); // transient: amber while on
    setTooltip (paramDef (paramIndex).name);
    attachment = std::make_unique<juce::ParameterAttachment> (*p.param (paramIndex), [this] (float v) {
        setToggleState (juce::roundToInt (v) == active, juce::dontSendNotification);
    });
    attachment->sendInitialUpdate();
    onClick = [this] {
        const bool isOn = juce::roundToInt (view::plain (proc, index)) == active;
        attachment->setValueAsCompleteGesture (isOn ? 0.0f : (float) active);
    };
}

void ParamButton::setFocusState (bool highlighted, bool dimmed)
{
    getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    repaint();
}

//==============================================================================
DrawerHeader::DrawerHeader (NorthlineProcessor& p, int extParam, std::function<void()> onToggle)
    : proc (p), ext (extParam), toggle (std::move (onToggle)), lastKeySeen (juce::Time::getMillisecondCounter())
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Show or hide the sidechain controls");
    startTimerHz (5);
}

DrawerHeader::~DrawerHeader() { stopTimer(); }

void DrawerHeader::setOpen (bool o)
{
    if (o == open) return;
    open = o;
    repaint();
}

void DrawerHeader::mouseDown (const juce::MouseEvent&)
{
    if (toggle) toggle();
}

void DrawerHeader::timerCallback()
{
    const bool extOn = view::plain (proc, ext) >= 0.5f;
    const auto now = juce::Time::getMillisecondCounter();
    if (proc.getStrip().getSidechainDb() > -60.0f) lastKeySeen = now;
    const Status s = ! extOn ? Status::off : ((now - lastKeySeen) < 2000u ? Status::keyOk : Status::noKey);
    if (s != status)
    {
        status = s;
        repaint();
    }
}

void DrawerHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::edge);
    g.drawHorizontalLine (0, 0.0f, r.getWidth());

    juce::Path arrow;
    const float cy = r.getCentreY();
    if (open) arrow.addTriangle (4.0f, cy - 3.0f, 12.0f, cy - 3.0f, 8.0f, cy + 3.0f);
    else arrow.addTriangle (5.0f, cy - 4.0f, 5.0f, cy + 4.0f, 10.0f, cy);
    g.setColour (isMouseOver() ? colours::text : colours::dim);
    g.fillPath (arrow);

    g.setFont (NorthlineLookAndFeel::font (11.5f, true));
    g.drawText ("SIDECHAIN", r.withTrimmedLeft (18.0f), juce::Justification::centredLeft);

    if (status != Status::off)
    {
        const bool ok = status == Status::keyOk;
        const juce::String text = ok ? juce::String ("key signal") : juce::String ("no key signal");
        g.setFont (NorthlineLookAndFeel::font (11.0f));
        const float textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const auto textArea = r.removeFromRight (textW + 4.0f);
        g.setColour (colours::dim);
        g.drawText (text, textArea, juce::Justification::centredRight);
        g.setColour (ok ? colours::meterGreen : colours::amber);
        g.fillEllipse (textArea.getX() - 12.0f, cy - 4.0f, 8.0f, 8.0f);
    }
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
    compareToggle.setButtonText ("CMP");
    compareToggle.setTooltip ("Compare this module: hear the strip without it, loudness-matched (transient, amber while on)");
    deltaToggle.setButtonText (juce::String::fromUTF8 ("\xce\x94") /* Greek capital delta */);
    deltaToggle.setTooltip ("Delta: hear only what this module removes or adds (transient, amber while on)");
    for (auto* b : { &compareToggle, &deltaToggle })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, colours::amber);
        addAndMakeVisible (b);
    }
    compareToggle.onClick = [this] { proc.setCompare (transient::toggleModuleCompare (proc.getCompare(), moduleId)); refreshTransient(); };
    deltaToggle.onClick = [this] { proc.setDelta (moduleId, ! proc.getDelta (moduleId)); refreshTransient(); };
    moveLeft.setTooltip ("Move module earlier in the chain");
    moveRight.setTooltip ("Move module later in the chain");
    moveLeft.onClick = [this] { if (onMove) onMove (moduleId, -1); };
    moveRight.onClick = [this] { if (onMove) onMove (moduleId, +1); };

    onWatcher = std::make_unique<juce::ParameterAttachment> (*p.param (onParam), [this] (float) { updateBodyEnabled(); });
}

ModulePanel::~ModulePanel() = default;

void ModulePanel::refreshTransient()
{
    compareToggle.setToggleState (proc.getCompare() == transient::moduleCompareTarget (moduleId), juce::dontSendNotification);
    deltaToggle.setToggleState (proc.getDelta (moduleId), juce::dontSendNotification);
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

ParamButton* ModulePanel::paramButton (int index, int activeValue, const juce::String& text)
{
    auto* b = new ParamButton (proc, index, activeValue, text);
    owned.add (b);
    addAndMakeVisible (b);
    return b;
}

void ModulePanel::relayoutOn (int paramIndex)
{
    layoutWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*proc.param (paramIndex), [this] (float) { resized(); }));
}

DrawerHeader* ModulePanel::makeDrawer (int extParam)
{
    drawerBar = std::make_unique<DrawerHeader> (proc, extParam, [this] {
        drawerOpen = ! drawerOpen;
        resized();
    });
    addAndMakeVisible (*drawerBar);
    return drawerBar.get();
}

int ModulePanel::chipWidth (const juce::String& text) noexcept
{
    return juce::jmax (44, 16 + 7 * text.length());
}

void ModulePanel::setShowCpu (bool on)
{
    if (on == showCpu) return;
    showCpu = on;
    repaint (getLocalBounds().removeFromBottom (kFooter));
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

    if (showCpu)
    {
        g.setColour (colours::faint);
        g.setFont (NorthlineLookAndFeel::font (tokens::type::label));
        figures::draw (g, "CPU " + cpuText, getLocalBounds().removeFromBottom (kFooter).reduced (kPad, 0).toFloat(),
                       juce::Justification::centredRight, NorthlineLookAndFeel::font (tokens::type::label));
    }
}

void ModulePanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeader).reduced (6, 5);
    power->setBounds (header.removeFromLeft (juce::jmax (40, 18 + 9 * juce::String (moduleName (moduleId)).length())));
    header.removeFromLeft (6);
    compareToggle.setBounds (header.removeFromLeft (36));
    header.removeFromLeft (2);
    deltaToggle.setBounds (header.removeFromLeft (26));
    header.removeFromLeft (4);
    if (mode) mode->setBounds (header.removeFromRight (juce::jmin (header.getWidth(), mode->getNumChildComponents() * 40)));

    // Chain-order buttons live in the footer, left of the CPU readout.
    auto footer = r.removeFromBottom (kFooter).reduced (6, 2);
    moveLeft.setBounds (footer.removeFromLeft (18));
    footer.removeFromLeft (2);
    moveRight.setBounds (footer.removeFromLeft (18));
    labels.clear();
    layoutBody (r.reduced (kPad, kGap));
    updateBodyEnabled();
}

void ModulePanel::applyPlan (juce::Rectangle<int> body, const Plan& pl)
{
    const int w = body.getWidth();
    std::vector<float> topW, bottomW, drawerTopW;
    for (const auto& it : pl.top) topW.push_back (it.minWidth);
    for (const auto& it : pl.bottom) bottomW.push_back (it.minWidth);
    for (const auto& it : pl.drawerTop) drawerTopW.push_back (it.minWidth);

    std::vector<juce::Component*> topC, bottomC, drawerTopC;
    for (const auto& it : pl.top) topC.push_back (it.comp);
    for (const auto& it : pl.bottom) bottomC.push_back (it.comp);
    for (const auto& it : pl.drawerTop) drawerTopC.push_back (it.comp);

    const int topH = topW.empty() ? 0 : flowHeight (w, topW) + kGap;
    const int bottomH = bottomW.empty() ? 0 : flowHeight (w, bottomW) + kGap;
    const int meterH = (int) pl.meters.size() * (kMeterH + kGap);
    const int primH = pl.primary.empty() ? 0 : gridHeight (w, (int) pl.primary.size(), kPrimaryCell, kPrimaryMinW) + kGap;
    const int secH = pl.secondary.empty() ? 0 : gridHeight (w, (int) pl.secondary.size(), kSecondaryCell, kSecondaryMinW) + kGap;
    const bool drawerShown = pl.drawer && drawerBar != nullptr;
    int drawerH = 0;
    if (drawerShown)
    {
        drawerH = kDrawerBar;
        if (drawerOpen) drawerH += kGap + drawerContentHeight (w, drawerTopW, (int) pl.drawerKnobs.size());
    }
    // Every block carries its trailing gap, the display's included, so the display gets exactly what is left.
    const int fixed = topH + meterH + primH + secH + bottomH + drawerH + (pl.display != nullptr ? kGap : 0);
    const int displayH = pl.display != nullptr ? juce::jmax (pl.displayMin, body.getHeight() - fixed) : 0;

    std::vector<juce::Component*> placed;
    auto r = body;
    if (! topC.empty())
    {
        placeFlow (r.removeFromTop (topH - kGap), topC, topW);
        r.removeFromTop (kGap);
        placed.insert (placed.end(), topC.begin(), topC.end());
    }
    if (pl.display != nullptr)
    {
        pl.display->setBounds (r.removeFromTop (displayH));
        r.removeFromTop (kGap);
        placed.push_back (pl.display);
    }
    for (auto* m : pl.meters)
    {
        m->setBounds (r.removeFromTop (kMeterH));
        r.removeFromTop (kGap);
        placed.push_back (m);
    }
    if (! pl.primary.empty())
    {
        placeGrid (r.removeFromTop (primH - kGap), pl.primary, kPrimaryCell, kPrimaryMinW);
        r.removeFromTop (kGap);
        placed.insert (placed.end(), pl.primary.begin(), pl.primary.end());
    }
    if (! pl.secondary.empty())
    {
        placeGrid (r.removeFromTop (secH - kGap), pl.secondary, kSecondaryCell, kSecondaryMinW);
        r.removeFromTop (kGap);
        placed.insert (placed.end(), pl.secondary.begin(), pl.secondary.end());
    }
    if (! bottomC.empty())
    {
        placeFlow (r.removeFromTop (bottomH - kGap), bottomC, bottomW);
        r.removeFromTop (kGap);
        placed.insert (placed.end(), bottomC.begin(), bottomC.end());
    }
    if (drawerShown)
    {
        drawerBar->setBounds (r.removeFromTop (kDrawerBar));
        drawerBar->setOpen (drawerOpen);
        placed.push_back (drawerBar.get());
        if (drawerOpen)
        {
            r.removeFromTop (kGap);
            if (! drawerTopC.empty())
            {
                const int h = flowHeight (w, drawerTopW);
                placeFlow (r.removeFromTop (h), drawerTopC, drawerTopW);
                r.removeFromTop (kGap);
                placed.insert (placed.end(), drawerTopC.begin(), drawerTopC.end());
            }
            if (! pl.drawerKnobs.empty())
            {
                placeGrid (r.removeFromTop (gridHeight (w, (int) pl.drawerKnobs.size(), kSecondaryCell, kSecondaryMinW)),
                           pl.drawerKnobs, kSecondaryCell, kSecondaryMinW);
                placed.insert (placed.end(), pl.drawerKnobs.begin(), pl.drawerKnobs.end());
            }
        }
    }
    else if (drawerBar != nullptr)
    {
        drawerBar->setVisible (false);
    }

    // Every owned control that the plan does not place (contextual hiding) is hidden.
    for (auto* c : owned)
        c->setVisible (std::find (placed.begin(), placed.end(), c) != placed.end());
}

//==============================================================================
PrePanel::PrePanel (NorthlineProcessor& p) : ModulePanel (p, modPre, P::preOn, P::preMode), harmonics (p.getStrip())
{
    satOn = chip (P::satOn, "SAT");
    satType = choice (P::satType);
    hq = chip (P::quality, "HQ");
    hq->setTooltip ("Global quality. HQ oversamples the nonlinear stages (lower aliasing)");
    satAuto = chip (P::satAutoGain, "AUTO");
    satAuto->setTooltip ("Calibrated make-up gain: holds the operating level as the drive moves");
    drive = knob (P::satDrive, "Drive");
    tone = knob (P::satTone, "Tone");
    hpFreq = knob (P::hpFreq, "HP Freq");
    lpFreq = knob (P::lpFreq, "LP Freq");
    mix = knob (P::satMix, "Mix");
    hpRes = knob (P::hpRes, "HP Res");
    lpRes = knob (P::lpRes, "LP Res");
    thumpFreq = knob (P::weightFreq, "Wt Freq");
    hpOn = chip (P::hpOn, "HP");
    hpSlope = choice (P::hpSlope);
    lpOn = chip (P::lpOn, "LP");
    lpSlope = choice (P::lpSlope);
    thumpOn = chip (P::weightOn, "WEIGHT");
    thumpAmt = choice (P::weightAmt);
    target = choice (P::filtTarget);
    target->setTooltip ("Filter target in MS mode: both, mid only or side only (e.g. high-pass the side for mono bass)");
    display = make<PreDisplay> (p, harmonics);
    relayoutOn (P::preMode);
}

PrePanel::~PrePanel() { harmonics.detach(); }

void PrePanel::setActive (bool a)
{
    if (a) harmonics.attach();
    else harmonics.detach();
}

void PrePanel::layoutBody (juce::Rectangle<int> r)
{
    const bool ms = view::plain (proc, P::preMode) >= 0.5f;
    Plan pl;
    pl.top = { { satOn, (float) chipWidth ("SAT") }, { satType, 80.0f }, { hq, (float) chipWidth ("HQ") }, { satAuto, (float) chipWidth ("AUTO") } };
    pl.display = display;
    pl.displayMin = 150;
    pl.primary = { drive, tone, hpFreq, lpFreq };
    pl.secondary = { mix, hpRes, lpRes, thumpFreq };
    pl.bottom = { { hpOn, (float) chipWidth ("HP") }, { hpSlope, 64.0f }, { lpOn, (float) chipWidth ("LP") }, { lpSlope, 64.0f },
                  { thumpOn, (float) chipWidth ("WEIGHT") }, { thumpAmt, 64.0f } };
    if (ms) pl.bottom.push_back ({ target, 80.0f });
    applyPlan (r, pl);
}

//==============================================================================
GatePanel::GatePanel (NorthlineProcessor& p) : ModulePanel (p, modGate, P::gateOn, P::gateMode)
{
    type = segmented (P::gateType);
    addAndMakeVisible (gr);
    thresh = knob (P::gateThresh, "Thresh");
    floorK = knob (P::gateFloor, "Range");
    attack = knob (P::gateAttack, "Attack");
    release = knob (P::gateRelease, "Release");
    hyst = knob (P::gateHyst, "Hyst");
    hold = knob (P::gateHold, "Hold");
    ratio = knob (P::gateRatio, "Ratio");
    knee = knob (P::gateKnee, "Knee");
    scHp = knob (P::gateScHp, "SC HP");
    scLp = knob (P::gateScLp, "SC LP");
    ext = chip (P::gateExt, "EXT SC");
    listen = paramButton (P::gateListen, 1, "LISTEN");
    listen->setTooltip ("Hear the key signal (transient, amber while on)");
    display = make<LevelScope> (p, LevelScope::Kind::Gate);
    makeDrawer (P::gateExt);
    relayoutOn (P::gateType);
}

void GatePanel::layoutBody (juce::Rectangle<int> r)
{
    const bool expander = juce::roundToInt (view::plain (proc, P::gateType)) == 1; // expander ratio and knee only
    Plan pl;
    pl.top = { { type, 190.0f } };
    pl.display = display;
    pl.displayMin = 140;
    pl.meters = { &gr };
    pl.primary = { thresh, floorK, attack, release };
    pl.secondary = { hyst, hold };
    if (expander) { pl.secondary.push_back (ratio); pl.secondary.push_back (knee); }
    pl.drawer = true;
    pl.drawerTop = { { ext, (float) chipWidth ("EXT SC") }, { listen, (float) chipWidth ("LISTEN") } };
    pl.drawerKnobs = { scHp, scLp };
    applyPlan (r, pl);
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
    listenB1 = paramButton (P::dsListen, 1, "BAND 1");
    listenB2 = paramButton (P::dsListen, 2, "BAND 2");
    listenDelta = paramButton (P::dsListen, 3, "DELTA");
    listenB1->setTooltip ("Hear band 1 (transient, amber while on)");
    listenB2->setTooltip ("Hear band 2 (transient, amber while on)");
    listenDelta->setTooltip ("Hear what the de-esser removes (transient, amber while on)");
    display = make<LevelScope> (p, LevelScope::Kind::DeEss);
    makeDrawer (P::dsExt);
}

void DsPanel::layoutBody (juce::Rectangle<int> r)
{
    Plan pl;
    pl.top = { { on1, (float) chipWidth ("BAND 1") }, { type1, 80.0f }, { on2, (float) chipWidth ("BAND 2") }, { type2, 80.0f },
               { relative, (float) chipWidth ("REL") } };
    pl.display = display;
    pl.displayMin = 140;
    pl.primary = { thresh1, thresh2, release, out };
    pl.secondary = { freq1, range1, freq2, range2 };
    pl.drawer = true;
    pl.drawerTop = { { ext, (float) chipWidth ("EXT") }, { listenB1, (float) chipWidth ("BAND 1") },
                     { listenB2, (float) chipWidth ("BAND 2") }, { listenDelta, (float) chipWidth ("DELTA") } };
    applyPlan (r, pl);
}

//==============================================================================
EqPanel::EqPanel (NorthlineProcessor& p) : ModulePanel (p, modEq, P::eqOn, P::eqMode), analyser (p.getStrip())
{
    analyserOn.setClickingTogglesState (true);
    analyserOn.setToggleState (true, juce::dontSendNotification);
    analyserOn.setTooltip ("Show the pre and post spectrum of the EQ (analysed on a background thread)");
    analyserOn.onClick = [this] { updateAnalyser(); };
    frozen.setClickingTogglesState (true);
    frozen.setTooltip ("Freeze the analyser display");
    frozen.onClick = [this] { analyser.setFrozen (frozen.getToggleState()); };
    addAndMakeVisible (analyserOn);
    addAndMakeVisible (frozen);

    curve = choice (P::eqChar);
    static const char* names[] { "LOW", "MID", "TONE", "HIGH" };
    for (int b = 0; b < 4; ++b)
    {
        const int o = P::eqLowOn + b * P::kEqBandStride;
        auto& band = bands[(size_t) b];
        band.on = chip (o, names[b]);
        band.type = choice (o + 1);
        band.gain = knob (o + 3, "Gain");
        band.freq = knob (o + 2, "Freq");
        band.q = knob (o + 4, "Q");
        band.target = choice (o + 5);
        band.target->setTooltip ("Band target in MS mode: both, mid only or side only");
        relayoutOn (o + 1); // the band type decides whether Q does anything
    }
    display = make<EqView> (p, analyser);
    relayoutOn (P::eqMode);
}

EqPanel::~EqPanel() { analyser.detach(); }

void EqPanel::setActive (bool a)
{
    active = a;
    updateAnalyser();
}

void EqPanel::updateAnalyser()
{
    const bool shown = analyserOn.getToggleState();
    if (shown && active) analyser.attach();
    else analyser.detach();
    if (display != nullptr) display->setAnalyserShown (shown);
}

void EqPanel::layoutBody (juce::Rectangle<int> r)
{
    const bool ms = view::plain (proc, P::eqMode) >= 0.5f;

    // Shelves and the Wide / Narrow types ignore Q: dim the Q knob and say why, but keep it hoverable for the tooltip.
    for (int b = 0; b < 4; ++b)
    {
        const bool usesQ = transient::eqBandUsesQ (juce::roundToInt (view::plain (proc, P::eqLowOn + b * P::kEqBandStride + 1)));
        bands[(size_t) b].q->setInert (! usesQ);
        bands[(size_t) b].q->setTooltip (usesQ ? "Q: how wide the band is (higher is narrower)"
                                               : "Q has no effect with this band type: shelves and Wide or Narrow set their own width. Pick Bell or Param to use Q.");
    }

    auto top = r.removeFromTop (kRowH);
    analyserOn.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (kRowGap);
    frozen.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (kRowGap);
    curve->setBounds (top);
    r.removeFromTop (kGap);

    // Band columns: type, gain (primary dial), freq and Q (secondary dials), and the M/S target.
    const int bandsH = kRowH + kRowGap + kRowH + kRowGap + kPrimaryCell + kRowGap + kSecondaryCell + kRowGap + kRowH;
    auto bandsArea = r.removeFromBottom (bandsH);
    r.removeFromBottom (kGap);
    display->setBounds (r);

    const int colW = (bandsArea.getWidth() - kRowGap * 3) / 4;
    for (int b = 0; b < 4; ++b)
    {
        auto c = bandsArea.removeFromLeft (colW);
        bandsArea.removeFromLeft (kRowGap);
        auto& band = bands[(size_t) b];
        band.on->setBounds (c.removeFromTop (kRowH));
        c.removeFromTop (kRowGap);
        band.type->setBounds (c.removeFromTop (kRowH));
        c.removeFromTop (kRowGap);
        band.gain->setBounds (c.removeFromTop (kPrimaryCell));
        c.removeFromTop (kRowGap);
        auto fq = c.removeFromTop (kSecondaryCell);
        band.freq->setBounds (fq.removeFromLeft (fq.getWidth() / 2));
        band.q->setBounds (fq);
        c.removeFromTop (kRowGap);
        band.target->setVisible (ms);
        if (ms) band.target->setBounds (c.removeFromTop (kRowH));
    }
}

//==============================================================================
namespace
{
// Comp display: the scope with the static curve inset on its right (hidden in a narrow column).
class CompHost : public juce::Component
{
public:
    CompHost (NorthlineProcessor& p) : scope (p, LevelScope::Kind::Comp), curve (p, scope)
    {
        addAndMakeVisible (scope);
        addAndMakeVisible (curve);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (r.getWidth() >= 360)
        {
            curve.setVisible (true);
            curve.setBounds (r.removeFromRight (r.getWidth() * 3 / 10).reduced (0, 2));
            r.removeFromRight (6);
        }
        else
        {
            curve.setVisible (false);
        }
        scope.setBounds (r);
    }

private:
    LevelScope scope;
    CompCurve curve;
};
} // namespace

CompPanel::CompPanel (NorthlineProcessor& p) : ModulePanel (p, modComp, P::compOn, P::compMode)
{
    type = segmented (P::compType);
    addAndMakeVisible (gr);
    thresh = knob (P::compThresh, "Thresh");
    ratio = knob (P::compRatio, "Ratio");
    attack = knob (P::compAttack, "Attack");
    release = knob (P::compRelease, "Release");
    knee = knob (P::compKnee, "Knee");
    range = knob (P::compRange, "Range");
    mix = knob (P::compMix, "Mix");
    out = knob (P::compOut, "Output");
    scHp = knob (P::compScHp, "SC HP");
    scLp = knob (P::compScLp, "SC LP");
    scFreq = knob (P::compScBellFreq, "SC Freq");
    scGain = knob (P::compScBellGain, "SC Gain");
    makeup = chip (P::compMakeup, "AUTO GAIN");
    makeup->setTooltip ("Automatic make-up gain. Off by default so the compressor can be used for ducking");
    autoRel = chip (P::compAutoRel, "AUTO REL");
    ext = chip (P::compExt, "EXT SC");
    listenKey = paramButton (P::compListen, 1, "KEY");
    listenDelta = paramButton (P::compListen, 2, "DELTA");
    listenKey->setTooltip ("Hear the key signal (transient, amber while on)");
    listenDelta->setTooltip ("Hear what the compressor removes (transient, amber while on)");
    display = make<CompHost> (p);
    makeDrawer (P::compExt);
}

void CompPanel::layoutBody (juce::Rectangle<int> r)
{
    Plan pl;
    pl.top = { { type, 180.0f }, { makeup, (float) chipWidth ("AUTO GAIN") }, { autoRel, (float) chipWidth ("AUTO REL") } };
    pl.display = display;
    pl.displayMin = 120;
    pl.primary = { thresh, ratio, attack, release };
    pl.secondary = { knee, range, mix, out };
    pl.drawer = true;
    pl.drawerTop = { { ext, (float) chipWidth ("EXT SC") }, { listenKey, (float) chipWidth ("KEY") }, { listenDelta, (float) chipWidth ("DELTA") } };
    pl.drawerKnobs = { scHp, scLp, scFreq, scGain };
    applyPlan (r, pl);
}
} // namespace nl

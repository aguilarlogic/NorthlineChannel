#include "PluginEditor.h"
#include "HubPanel.h"
#include "Onboarding.h"
#include "PresetBrowser.h"
#include "SmartStartPanel.h"
#include "../hub/InstanceRegistry.h"
#include <algorithm>

namespace nl
{
namespace shell
{
Geometry geometry()
{
    Geometry g;
    const int left = kMargin;
    const int masterX = kBaseW - kMargin - kMasterW;
    const int columnW = masterX - kMargin - left; // width of the chain and detail column
    g.topBar = { left, kMargin, kBaseW - 2 * kMargin, kTopBarH };
    g.chain = { left, g.topBar.getBottom() + kMargin, columnW, kChainH };
    g.detail = { left, g.chain.getBottom() + kMargin, columnW, kBaseH - kMargin - (g.chain.getBottom() + kMargin) };
    g.master = { masterX, g.topBar.getBottom() + kMargin, kMasterW, kBaseH - kMargin - (g.topBar.getBottom() + kMargin) };
    return g;
}
}

namespace
{
constexpr int kPanelGap = 8;
constexpr float kScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
}

NorthlineEditor::NorthlineEditor (NorthlineProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    applyTheme (UiSettings::get().theme());
    lnf.refreshColours();
    setLookAndFeel (&lnf);
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (detail);
    detail.addAndMakeVisible (consoleView);
    consoleView.setViewedComponent (&consoleBody, false);
    consoleView.setScrollBarsShown (false, true);
    consoleView.setVisible (false);

    cpuInfo = std::make_unique<CpuInfoPanel>();
    detail.addChildComponent (*cpuInfo);

    overlayLayer.addAndMakeVisible (overlayHolder);
    addChildComponent (overlayLayer);

    frameBuf.resize (256);
    console = UiSettings::get().consoleLayout();
    buildContent();

    // An instance whose size the user never chose opens at the global default. A size the user chose
    // for this instance is kept, 100 % included. The size is set before the limits, so the constraints
    // do not resize (and re-store) a window that has no size yet.
    const float initial = proc.isUiScaleSet() ? proc.getUiScale() : UiSettings::get().defaultScale();
    appliedSerial = UiSettings::get().applyAllSerial();
    setScale (initial, false);
    proc.setUiScale (UiSettings::clampScale (initial), false); // what the top bar shows; not a user choice

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (shell::kBaseW * UiSettings::kMinScale), juce::roundToInt (shell::kBaseH * UiSettings::kMinScale),
                     juce::roundToInt (shell::kBaseW * UiSettings::kMaxScale), juce::roundToInt (shell::kBaseH * UiSettings::kMaxScale));
    getConstrainer()->setFixedAspectRatio (shell::kBaseW / (double) shell::kBaseH);

    UiSettings::get().addChangeListener (this);
    startTimerHz (30);

    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onFrame(); });

    if (! UiSettings::get().onboardingDone())
    {
        juce::Component::SafePointer<NorthlineEditor> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && ! safe->hasOverlay() && ! UiSettings::get().onboardingDone()) safe->openOnboarding();
        });
    }
}

NorthlineEditor::~NorthlineEditor()
{
    UiSettings::get().removeChangeListener (this);
    stopTimer();
    vblank.reset();
    overlay.reset();
    chain.reset();
    topBar.reset();
    master.reset();
    for (auto& p : panels) p.reset();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void NorthlineEditor::buildContent()
{
    // Everything that caches a colour (panels, master, top bar) is rebuilt, so a theme switch
    // takes effect everywhere.
    for (auto& p : panels) p.reset();
    master.reset();
    chain.reset();
    topBar.reset();

    topBar = std::make_unique<TopBar> (proc);
    content.addAndMakeVisible (*topBar);
    topBar->onFocusChanged = [this] { updateFocus(); };
    topBar->onScaleChanged = [this] { setScale (proc.getUiScale(), false); };
    topBar->onOpenSettings = [this] { openSettings(); };
    topBar->onOpenPresetBrowser = [this] { openPresetBrowser(); };
    topBar->onSmartStart = [this] { openSmartStart(); };
    topBar->onOpenHub = [this] { openHub(); };

    chain = std::make_unique<ChainView> (proc);
    content.addAndMakeVisible (*chain);
    chain->onSelect = [this] (int id) { selectModule (id); };
    chain->onOrderChanged = [this] { layoutDetail(); if (topBar) topBar->refresh(); };
    chain->onOpenSettings = [this] { openSettings(); };

    panels[modPre] = std::make_unique<PrePanel> (proc);
    panels[modGate] = std::make_unique<GatePanel> (proc);
    panels[modDs] = std::make_unique<DsPanel> (proc);
    panels[modEq] = std::make_unique<EqPanel> (proc);
    panels[modComp] = std::make_unique<CompPanel> (proc);
    for (auto& panel : panels)
    {
        panel->setShowCpu (UiSettings::get().showCpu());
        panel->onMove = [this] (int id, int delta)
        {
            proc.moveModule (id, delta);
            chain->refreshOrder();
            layoutDetail();
            topBar->refresh();
        };
    }

    master = std::make_unique<MasterPanel> (proc);
    content.addAndMakeVisible (*master);

    chain->setSelected (selected);
    layoutAll();
    updateFocus();
}

void NorthlineEditor::setScale (float s, bool storeInProcessor)
{
    s = UiSettings::clampScale (s);
    scale = s;
    const auto transform = juce::AffineTransform::scale (s);
    content.setTransform (transform);
    overlayHolder.setTransform (transform);
    ui::setDisplayScale (s);
    if (storeInProcessor) proc.setUiScale (s); // a size set here is the instance's own choice
    if (topBar != nullptr) { topBar->setShownScale (s); topBar->refresh(); } // the top bar shows the size at once
    setSize (juce::roundToInt (kBaseW * s), juce::roundToInt (kBaseH * s));
}

void NorthlineEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void NorthlineEditor::resized()
{
    if (getWidth() <= 0) return; // not sized yet
    // Free resize from the corner: the width decides the scale (the aspect ratio is fixed).
    const float s = UiSettings::clampScale (getWidth() / (float) kBaseW);
    if (std::abs (s - scale) > 1.0e-4f)
    {
        scale = s;
        const auto transform = juce::AffineTransform::scale (s);
        content.setTransform (transform);
        overlayHolder.setTransform (transform);
        ui::setDisplayScale (s);
        if (std::abs (proc.getUiScale() - s) > 1.0e-4f) proc.setUiScale (s);
    }

    content.setBounds (0, 0, kBaseW, kBaseH);
    overlayLayer.setBounds (getLocalBounds());
    overlayHolder.setBounds (0, 0, kBaseW, kBaseH);
    layoutAll();
}

void NorthlineEditor::layoutAll()
{
    if (topBar == nullptr) return;
    const auto g = shell::geometry();
    topBar->setBounds (g.topBar);
    chain->setBounds (g.chain);
    master->setBounds (g.master);
    detail.setBounds (g.detail);
    layoutDetail();
    cpuInfo->setBounds (detail.getWidth() - CpuInfoPanel::kWidth - kPanelGap, kPanelGap, CpuInfoPanel::kWidth, CpuInfoPanel::kHeight);
    cpuInfo->setVisible (UiSettings::get().showCpu());
    cpuInfo->toFront (false);
}

void NorthlineEditor::layoutDetail()
{
    if (chain == nullptr) return;
    const auto area = detail.getLocalBounds();
    lastOrder = proc.getOrder();

    if (console)
    {
        consoleView.setVisible (true);
        consoleView.setBounds (area);
        int x = 0;
        for (int slot = 0; slot < numModules; ++slot)
        {
            auto& panel = panels[(size_t) lastOrder[(size_t) slot]];
            if (panel == nullptr) continue;
            if (panel->getParentComponent() != &consoleBody) consoleBody.addAndMakeVisible (*panel);
            panel->setVisible (true);
            panel->setBounds (x, 0, panel->preferredWidth(), area.getHeight());
            x += panel->preferredWidth() + kPanelGap;
        }
        consoleBody.setSize (juce::jmax (area.getWidth(), x - kPanelGap), area.getHeight());
    }
    else
    {
        consoleView.setVisible (false);
        for (int m = 0; m < numModules; ++m)
        {
            auto& panel = panels[(size_t) m];
            if (panel == nullptr) continue;
            if (panel->getParentComponent() != &detail) detail.addAndMakeVisible (*panel);
            const bool shown = m == selected;
            panel->setVisible (shown);
            panel->setBounds (area);
        }
    }
    cpuInfo->toFront (false);
    syncPanelActivity();
}

bool NorthlineEditor::isPanelActive (int moduleId) const noexcept
{
    return moduleId >= 0 && moduleId < numModules && panelLive[(size_t) moduleId];
}

void NorthlineEditor::syncPanelActivity()
{
    // A panel runs its analysers and harmonic taps while it is on screen. A headless editor (offline
    // snapshot, tests) has no peer, so isShowing() is always false there; it counts as on screen.
    const bool screen = isShowing() || getPeer() == nullptr;
    for (int m = 0; m < numModules; ++m)
    {
        auto* panel = panels[(size_t) m].get();
        const bool live = screen && panel != nullptr && (console || m == selected) && panel->isVisible();
        panelLive[(size_t) m] = live;
        if (panel != nullptr) panel->setActive (live); // idempotent: the panels attach and detach only on change
    }
}

void NorthlineEditor::selectModule (int moduleId)
{
    selected = juce::jlimit (0, numModules - 1, moduleId);
    if (chain != nullptr) chain->setSelected (selected);
    if (console)
    {
        if (auto* panel = panels[(size_t) selected].get())
            consoleView.setViewPosition (panel->getX(), 0);
    }
    else
    {
        layoutDetail();
    }
}

void NorthlineEditor::showOverlay (std::unique_ptr<juce::Component> body, const juce::String& title, overlay::Size size,
                                   std::function<void()> onDismiss)
{
    closeOverlay();
    if (body == nullptr) return;

    auto card = std::make_unique<OverlayCard> (std::move (body), title, size);
    card->onClose = [this, dismiss = std::move (onDismiss)]
    {
        if (dismiss) dismiss();
        closeOverlay();
    };
    const auto cardRect = overlay::cardBounds ({ 0, 0, kBaseW, kBaseH }, size);
    overlaySizeValue = size;
    overlay = std::move (card);
    overlayHolder.addAndMakeVisible (*overlay);
    overlay->setBounds (overlay::componentBounds (cardRect));
    overlayLayer.setVisible (true);
    overlayLayer.toFront (false);
    if (auto* c = static_cast<OverlayCard*> (overlay.get())->getContent()) c->grabKeyboardFocus();
    else overlay->grabKeyboardFocus();
}

void NorthlineEditor::closeOverlay()
{
    if (overlay == nullptr) return;
    overlayHolder.removeChildComponent (overlay.get());
    // Deleted after this event: the overlay may be the one calling closeOverlay().
    std::shared_ptr<juce::Component> dead (std::move (overlay));
    overlayLayer.setVisible (false);
    juce::MessageManager::callAsync ([dead] {});
    grabKeyboardFocus();
}

bool NorthlineEditor::keyPressed (const juce::KeyPress& key)
{
    if (overlay != nullptr && key == juce::KeyPress::escapeKey)
    {
        closeOverlay();
        return true;
    }
    // Undo / redo shortcuts; everything else passes through to the host.
    return overlay == nullptr && topBar != nullptr && topBar->handleKeyPress (key);
}

void NorthlineEditor::openPresetBrowser()
{
    auto b = std::make_unique<PresetBrowser> (proc);
    b->onClose = [this] { closeOverlay(); if (topBar) topBar->refresh(); };
    showOverlay (std::move (b), "Presets", overlay::Size::large);
}

void NorthlineEditor::openSmartStart()
{
    auto s = std::make_unique<SmartStartPanel> (proc);
    s->onClose = [this] { closeOverlay(); };
    showOverlay (std::move (s), "Smart Start", overlay::Size::medium);
}

void NorthlineEditor::openHub()
{
    auto h = std::make_unique<HubPanel>();
    h->onClose = [this] { closeOverlay(); };
    // Another instance can only be brought forward when its editor is open; the host owns the
    // windows, so a closed editor cannot be opened from here.
    h->onEditInstance = [] (int id)
    {
        if (auto* other = hub::InstanceRegistry::instance().find (id))
            if (auto* ed = other->getActiveEditor())
                if (auto* top = ed->getTopLevelComponent()) top->toFront (true);
    };
    showOverlay (std::move (h), "Instance Hub", overlay::Size::large);
}

void NorthlineEditor::openSettings()
{
    auto s = std::make_unique<SettingsOverlay> (
        proc,
        [this] (float v) { setScale (v, true); },
        [this] { openOnboarding(); });
    s->onClose = [this] { closeOverlay(); };
    showOverlay (std::move (s), "Settings", overlay::Size::medium);
}

void NorthlineEditor::openOnboarding()
{
    auto c = std::make_unique<OnboardingCoach>();
    c->onClose = [this] { closeOverlay(); };
    // Closing the tour with its close button counts as finishing it, so it does not come back at start-up.
    showOverlay (std::move (c), "First-run tour", overlay::Size::compact, [] { UiSettings::get().setOnboardingDone (true); });
}

void NorthlineEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    settingsChanged();
}

void NorthlineEditor::settingsChanged()
{
    const auto& s = UiSettings::get();

    if (activeTheme() != s.theme())
    {
        applyTheme (s.theme());
        lnf.refreshColours();
        buildContent();
        sendLookAndFeelChange();
        repaint();
    }

    if (console != s.consoleLayout())
    {
        console = s.consoleLayout();
        layoutDetail();
    }

    if (s.applyAllSerial() != appliedSerial)
    {
        appliedSerial = s.applyAllSerial();
        setScale (s.defaultScale(), true);
    }

    cpuInfo->setVisible (s.showCpu());
    for (auto& panel : panels)
        if (panel != nullptr) panel->setShowCpu (s.showCpu());
}

void NorthlineEditor::onFrame()
{
    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    drainMeters();
}

void NorthlineEditor::timerCallback()
{
    // With no display (offline snapshot, hidden window) the vertical-blank callback does not
    // fire, so the meters are drained here as well.
    if (juce::Time::getMillisecondCounterHiRes() - lastFrameMs > 200.0) drainMeters();
    if ((++tick % 3) == 0) housekeeping();
}

void NorthlineEditor::drainMeters()
{
    if (chain == nullptr) return;
    auto& strip = proc.getStrip();

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
    master->updateMeters (sum);

    MeterFrame gr;
    gr.gateGr = sum.gateGr;
    gr.ds1Gr = sum.ds1Gr;
    gr.ds2Gr = sum.ds2Gr;
    gr.compGr = sum.compGr;
    if (any) std::copy (std::begin (last.eqDynGainDb), std::end (last.eqDynGainDb), std::begin (gr.eqDynGainDb));
    chain->updateMeters (gr);
    for (auto& panel : panels)
        if (panel != nullptr) panel->updateMeters (gr);

    // CPU readouts at about 5 Hz.
    if (any && (++cpuTick % 12) == 0)
    {
        for (auto& panel : panels)
            if (panel != nullptr && UiSettings::get().showCpu()) panel->setCpuUs (last.moduleUs[panel->getModuleId()], last.blockUs);
        topBar->setCpu (cpuAvg);
        std::array<float, numModules> us {};
        for (int m = 0; m < numModules; ++m) us[(size_t) m] = last.moduleUs[m];
        cpuInfo->setValues (us, last.blockUs);
    }
}

void NorthlineEditor::housekeeping()
{
    if (topBar == nullptr) return;
    syncPanelActivity();

    // Compare and Delta are transient (not parameters): re-read them a few times a second.
    topBar->refreshTransient();
    for (auto& p : panels) if (p != nullptr) p->refreshTransient();

    // Whole-state changes (presets, undo, A/B, host state restore).
    const int gen = proc.stateGeneration.load();
    if (gen != lastGeneration)
    {
        lastGeneration = gen;
        topBar->refresh();
        chain->refreshOrder();
        if (proc.getOrder() != lastOrder) layoutDetail();
        updateFocus();
    }
    if (topBar->isFocusMode() != lastFocusMode) updateFocus();
    if ((tick % 30) == 0) topBar->refresh();
}

void NorthlineEditor::updateFocus()
{
    const auto& focus = proc.getFocus();
    const bool focusMode = topBar->isFocusMode() && ! focus.empty();
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
    lastFocusMode = topBar->isFocusMode();
}
} // namespace nl

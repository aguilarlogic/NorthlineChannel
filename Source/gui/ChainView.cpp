#include "ChainView.h"
#include <algorithm>
#include <cmath>

namespace nl
{
namespace chain
{
Order moveTo (const Order& order, int moduleId, int targetIndex)
{
    std::vector<int> rest;
    for (int m : order)
        if (m != moduleId) rest.push_back (m);
    targetIndex = juce::jlimit (0, numModules - 1, targetIndex);
    rest.insert (rest.begin() + targetIndex, moduleId);
    Order out {};
    for (int i = 0; i < numModules; ++i) out[(size_t) i] = rest[(size_t) i];
    return out;
}

int slotForCentre (float centreX, float firstLeft, float pitch, int count)
{
    if (pitch <= 0.0f) return 0;
    return juce::jlimit (0, count - 1, (int) std::floor ((centreX - firstLeft) / pitch));
}

float slotLeft (int index, float firstLeft, float pitch)
{
    return firstLeft + (float) index * pitch;
}
} // namespace chain

namespace
{
struct Spec
{
    int on, mode;
    std::array<int, 3> macros;
    std::array<const char*, 3> labels;
};

const Spec kSpecs[numModules] {
    { P::preOn, P::preMode, { P::satDrive, P::hpFreq, P::lpFreq }, { "DRIVE", "HP", "LP" } },
    { P::gateOn, P::gateMode, { P::gateThresh, P::gateAttack, P::gateRelease }, { "THRESH", "ATTACK", "RELEASE" } },
    { P::dsOn, P::dsMode, { P::ds1Freq, P::ds1Thresh, P::dsRelease }, { "FREQ", "THRESH", "RELEASE" } },
    { P::eqOn, P::eqMode, { P::eqLowGain, P::eqMidGain, P::eqHighGain }, { "LOW", "MID", "HIGH" } },
    { P::compOn, P::compMode, { P::compThresh, P::compRatio, P::compRelease }, { "THRESH", "RATIO", "RELEASE" } },
};

float plainOf (NorthlineProcessor& p, int index)
{
    auto* prm = p.param (index);
    return prm->convertFrom0to1 (prm->getValue());
}

// Background of the strip (static: buffered to an image).
class ChainBackdrop : public juce::Component
{
public:
    ChainBackdrop() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::panelHeader);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), tokens::radius::panel);
        g.setColour (colours::edge);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), tokens::radius::panel, 1.0f);
    }
};

// Drag handle (≡): reports press, drag and release to the card.
class ChainHandle : public juce::Component
{
public:
    explicit ChainHandle (std::function<void (int, const juce::MouseEvent&)> f) : fn (std::move (f))
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTitle ("Reorder handle");
        setDescription ("Drag to move this module earlier or later in the chain");
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::dim);
        const float cx = (float) getWidth() * 0.5f;
        for (int i = 0; i < 3; ++i)
            g.fillRect (juce::Rectangle<float> (cx - 4.0f, 4.0f + (float) i * 4.0f, 8.0f, 1.5f));
    }
    void mouseDown (const juce::MouseEvent& e) override { fn (0, e); }
    void mouseDrag (const juce::MouseEvent& e) override { fn (1, e); }
    void mouseUp (const juce::MouseEvent& e) override { fn (2, e); }

private:
    std::function<void (int, const juce::MouseEvent&)> fn;
};

// Gear and "?" buttons drawn with paths.
class IconButton : public juce::Button
{
public:
    enum class Kind { gear, help };
    IconButton (Kind k, const juce::String& name) : juce::Button (name), kind (k) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (5.0f);
        const auto col = getToggleState() ? colours::focus : (over ? colours::text : colours::dim);
        if (down) g.setColour (colours::track);
        else g.setColour (colours::track.withAlpha (over ? 0.8f : 0.4f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), tokens::radius::control);
        g.setColour (col);
        if (kind == Kind::help)
        {
            g.setFont (NorthlineLookAndFeel::font (15.0f, true));
            g.drawText ("?", getLocalBounds(), juce::Justification::centred);
            return;
        }
        // gear: ring with eight teeth
        const auto c = r.getCentre();
        const float outer = r.getWidth() * 0.5f, inner = outer * 0.62f;
        juce::Path gear;
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::pi / 4.0f;
            juce::Path tooth;
            tooth.addRectangle (-outer * 0.22f, -outer, outer * 0.44f, outer * 0.42f);
            gear.addPath (tooth, juce::AffineTransform::rotation (a).translated (c.x, c.y));
        }
        g.fillPath (gear);
        g.setColour (colours::track);
        g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);
        g.setColour (col);
        g.drawEllipse (c.x - inner * 0.55f, c.y - inner * 0.55f, inner * 1.1f, inner * 1.1f, 1.4f);
    }

private:
    Kind kind;
};

class CardValue : public juce::AccessibilityTextValueInterface
{
public:
    explicit CardValue (ChainCard& c) : card (c) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override
    {
        return juce::String (card.isOn() ? "On" : "Off") + ", mode " + card.modeText();
    }
    void setValueAsString (const juce::String&) override {}

private:
    ChainCard& card;
};
} // namespace

//==============================================================================
ChainView::ChainView (NorthlineProcessor& p) : proc (p)
{
    backdrop = std::make_unique<ChainBackdrop>();
    backdrop->setBufferedToImage (true);
    addAndMakeVisible (*backdrop);

    for (int m = 0; m < numModules; ++m)
    {
        cards[(size_t) m] = std::make_unique<ChainCard> (p, m, [this] (int id, int phase, const juce::MouseEvent& e) { handleDragEvent (id, phase, e); });
        auto* card = cards[(size_t) m].get();
        card->onSelect = [this] (int id) { setSelected (id); if (onSelect) onSelect (id); };
        card->onMoveKey = [this] (int id, int delta) { proc.moveModule (id, delta); refreshOrder(); if (onOrderChanged) onOrderChanged(); (void) id; };
        card->onNavigate = [this] (int id, int delta)
        {
            const int idx = (int) (std::find (order.begin(), order.end(), id) - order.begin());
            const int next = juce::jlimit (0, numModules - 1, idx + delta);
            const int target = order[(size_t) next];
            setSelected (target);
            if (onSelect) onSelect (target);
            cards[(size_t) target]->grabKeyboardFocus();
        };
        addAndMakeVisible (*card);
    }

    helpButton = std::make_unique<IconButton> (IconButton::Kind::help, "Help mode");
    helpButton->setClickingTogglesState (true);
    helpButton->setToggleState (ui::helpMode(), juce::dontSendNotification);
    helpButton->setTooltip ("Help mode: hover any control for an explanation");
    helpButton->onClick = [this] { ui::setHelpMode (helpButton->getToggleState()); };
    addAndMakeVisible (*helpButton);

    settingsButton = std::make_unique<IconButton> (IconButton::Kind::gear, "Settings");
    settingsButton->setTooltip ("Settings: theme, size, layout and CPU readout");
    settingsButton->onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    addAndMakeVisible (*settingsButton);

    order = proc.getOrder();
    setSize (100, kHeight);
}

ChainView::~ChainView()
{
    animator.cancelAllAnimations (false);
}

void ChainView::paint (juce::Graphics&) {}

void ChainView::paintOverChildren (juce::Graphics& g)
{
    if (dragId < 0 || dragTarget < 0) return;
    const float x = chain::slotLeft (dragTarget, 0.0f, (float) (cardW + chain::kCardGap)) - chain::kCardGap * 0.5f - 1.5f;
    g.setColour (colours::forModule (dragId));
    g.fillRoundedRectangle (x, 4.0f, 3.0f, (float) getHeight() - 8.0f, 1.5f);
}

void ChainView::resized()
{
    auto r = getLocalBounds();
    backdrop->setBounds (r);
    auto icons = r.removeFromRight (chain::kIconsWidth).withSizeKeepingCentre (chain::kIconsWidth, chain::kIconSize);
    helpButton->setBounds (icons.removeFromLeft (chain::kIconSize));
    icons.removeFromLeft (4);
    settingsButton->setBounds (icons.removeFromLeft (chain::kIconSize));

    const int areaW = r.getWidth() - 8;
    cardW = juce::jmax (40, (areaW - (numModules - 1) * chain::kCardGap) / numModules);
    layoutCards (false);
}

juce::Rectangle<int> ChainView::slotBounds (int slot) const
{
    return { slot * (cardW + chain::kCardGap), 4, cardW, getHeight() - 8 };
}

void ChainView::layoutCards (bool animate)
{
    for (int slot = 0; slot < numModules; ++slot)
    {
        auto* card = cards[(size_t) order[(size_t) slot]].get();
        if (card == nullptr) continue;
        if (animate && card->getBounds() != slotBounds (slot))
            animator.animateComponent (card, slotBounds (slot), 1.0f, tokens::motion::reorderMs, false, 0.0, 0.0);
        else
            card->setBounds (slotBounds (slot));
    }
}

void ChainView::refreshOrder()
{
    const auto current = proc.getOrder();
    if (current != order)
    {
        order = current;
        if (cardW > 0) layoutCards (true);
    }
    refreshValues();
}

void ChainView::setSelected (int moduleId)
{
    selected = moduleId;
    for (auto& c : cards)
        if (c != nullptr) c->setSelected (c->getModuleId() == moduleId);
}

void ChainView::updateMeters (const MeterFrame& f)
{
    for (int m = 0; m < numModules; ++m)
    {
        float gr = 0.0f;
        switch (m)
        {
            case modGate: gr = f.gateGr; break;
            case modDs: gr = std::min (f.ds1Gr, f.ds2Gr); break;
            case modEq:
                for (float g : f.eqDynGainDb) gr = std::min (gr, g);
                break;
            case modComp: gr = f.compGr; break;
            default: break;
        }
        if (auto* c = cards[(size_t) m].get()) c->setGr (gr);
    }
}

void ChainView::refreshValues()
{
    for (auto& c : cards)
        if (c != nullptr) c->refreshValues();
}

void ChainView::moveSelected (int delta)
{
    proc.moveModule (selected, delta);
    refreshOrder();
    if (onOrderChanged) onOrderChanged();
}

void ChainView::handleDragEvent (int moduleId, int phase, const juce::MouseEvent& e)
{
    const float x = e.getEventRelativeTo (this).position.x;
    if (phase == 0)
    {
        if (cardW <= 0) return;
        dragId = moduleId;
        dragOffset = x - (float) cards[(size_t) moduleId]->getX();
        dragTarget = (int) (std::find (order.begin(), order.end(), moduleId) - order.begin());
        cards[(size_t) moduleId]->toFront (false);
        setSelected (moduleId);
        if (onSelect) onSelect (moduleId);
    }
    else if (phase == 1)
    {
        if (dragId == moduleId) dragTo (x);
    }
    else if (dragId == moduleId)
    {
        dragEnd();
    }
}

void ChainView::dragTo (float x)
{
    auto* card = cards[(size_t) dragId].get();
    const float maxLeft = (float) (getWidth() - cardW);
    const float left = juce::jlimit (0.0f, juce::jmax (0.0f, maxLeft), x - dragOffset);
    card->setTopLeftPosition ((int) left, 4);

    const float pitch = (float) (cardW + chain::kCardGap);
    const int target = chain::slotForCentre (left + cardW * 0.5f, 0.0f, pitch, numModules);
    if (target == dragTarget) { repaint(); return; }
    dragTarget = target;

    const auto preview = chain::moveTo (order, dragId, target);
    for (int slot = 0; slot < numModules; ++slot)
    {
        const int m = preview[(size_t) slot];
        if (m == dragId) continue;
        animator.animateComponent (cards[(size_t) m].get(), slotBounds (slot), 1.0f, tokens::motion::reorderMs, false, 0.0, 0.0);
    }
    repaint();
}

void ChainView::dragEnd()
{
    const int id = dragId;
    const int target = dragTarget;
    dragId = -1;
    dragTarget = -1;
    if (target >= 0)
    {
        const auto next = chain::moveTo (order, id, target);
        if (next != order)
        {
            proc.setOrder (next); // records undo
            order = proc.getOrder();
            if (onOrderChanged) onOrderChanged();
        }
    }
    layoutCards (true);
    repaint();
}

//==============================================================================
ChainCard::ChainCard (NorthlineProcessor& p, int id, std::function<void (int, int, const juce::MouseEvent&)> onHandle)
    : proc (p), moduleId (id), handle (std::move (onHandle))
{
    setWantsKeyboardFocus (true);
    const auto& spec = kSpecs[id];
    setTitle (moduleName (id));
    setDescription (juce::String (moduleName (id)) + " module. Click to open it. Drag the handle to reorder.");
    setTooltip (juce::String ("Open ") + moduleName (id) + ". Drag the handle to reorder the chain.");

    power = std::make_unique<Chip> (p, spec.on, moduleName (id), colours::forModule (id));
    addAndMakeVisible (*power);

    dragHandle = std::make_unique<ChainHandle> ([this] (int phase, const juce::MouseEvent& e) { if (handle) handle (moduleId, phase, e); });
    addAndMakeVisible (*dragHandle);

    grBar = std::make_unique<GrBar> (colours::forModule (id), 24.0f);
    grBar->setShowValue (false);
    addAndMakeVisible (*grBar);

    modeParam = spec.mode;
    listeners.push_back (std::make_unique<ParamBinding> (p, spec.mode, [this] { repaint(); }));
    for (int i = 0; i < 3; ++i)
    {
        macros[(size_t) i] = spec.macros[(size_t) i];
        listeners.push_back (std::make_unique<ParamBinding> (p, spec.macros[(size_t) i], [this] { repaint(); }));
    }
}

ChainCard::~ChainCard() = default;

bool ChainCard::isOn() const
{
    return plainOf (proc, kSpecs[moduleId].on) >= 0.5f;
}

juce::String ChainCard::modeText() const
{
    const auto& d = paramDef (modeParam);
    const int i = juce::jlimit (0, (int) d.choices.size() - 1, (int) std::lround (plainOf (proc, modeParam)));
    return d.choices.empty() ? juce::String() : juce::String (d.choices[(size_t) i]);
}

juce::String ChainCard::macroValue (int i) const
{
    const int idx = macros[(size_t) i];
    return formatValue (idx, plainOf (proc, idx));
}

juce::String ChainCard::macroLabel (int i) const
{
    return kSpecs[moduleId].labels[(size_t) i];
}

void ChainCard::setSelected (bool on)
{
    if (selectedState == on) return;
    selectedState = on;
    repaint();
}

void ChainCard::setGr (float db)
{
    grBar->setGr (db);
}

void ChainCard::paint (juce::Graphics& g)
{
    const auto accent = colours::forModule (moduleId);
    const bool on = isOn();
    const auto r = getLocalBounds().toFloat();
    auto body = r.reduced (0.5f);

    g.setColour (selectedState ? colours::panelHeader : colours::panel);
    g.fillRoundedRectangle (body, tokens::radius::panel);
    if (selectedState)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (body, tokens::radius::panel, 1.5f);
    }
    else
    {
        g.setColour (colours::edge);
        g.drawRoundedRectangle (body, tokens::radius::panel, 1.0f);
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (colours::focus);
        g.drawRoundedRectangle (body.reduced (1.5f), tokens::radius::panel, 1.0f);
    }

    g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
    g.setColour (on ? colours::dim : colours::faint);
    g.drawText (modeText(), juce::Rectangle<int> (getWidth() - 62, 4, 56, 18), juce::Justification::centredRight);

    const int colW = (getWidth() - 8) / 3;
    for (int i = 0; i < 3; ++i)
    {
        const int x = 4 + i * colW;
        g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
        g.setColour (on ? colours::dim : colours::faint);
        g.drawText (macroLabel (i), juce::Rectangle<int> (x, 25, colW, 13), juce::Justification::centredLeft);
        g.setFont (NorthlineLookAndFeel::font (tokens::type::value));
        g.setColour (on ? colours::text : colours::faint);
        g.drawText (macroValue (i), juce::Rectangle<int> (x, 38, colW - 2, 15), juce::Justification::centredLeft);
    }
}

void ChainCard::resized()
{
    dragHandle->setBounds (4, 4, 12, 18);
    power->setBounds (18, 4, 58, 18);
    grBar->setBounds (4, getHeight() - 9, getWidth() - 8, 5);
}

void ChainCard::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (onSelect) onSelect (moduleId);
    (void) e;
}

bool ChainCard::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
    {
        if (onSelect) onSelect (moduleId);
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const int delta = code == juce::KeyPress::leftKey ? -1 : 1;
        if (key.getModifiers().isAltDown())
        {
            if (onMoveKey) onMoveKey (moduleId, delta);
        }
        else if (onNavigate)
        {
            onNavigate (moduleId, delta);
        }
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> ChainCard::createAccessibilityHandler()
{
    juce::AccessibilityActions actions;
    actions.addAction (juce::AccessibilityActionType::press, [this] { if (onSelect) onSelect (moduleId); });
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::button, std::move (actions),
                                                         juce::AccessibilityHandler::Interfaces (std::make_unique<CardValue> (*this)));
}

//==============================================================================
CpuInfoPanel::CpuInfoPanel()
{
    setInterceptsMouseClicks (false, false);
    setSize (kWidth, kHeight);
}

void CpuInfoPanel::setValues (const std::array<float, numModules>& values, float block)
{
    if (values == us && block == blockUs) return;
    us = values;
    blockUs = block;
    repaint();
}

void CpuInfoPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panelHeader);
    g.fillRoundedRectangle (r, tokens::radius::panel);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), tokens::radius::panel, 1.0f);

    g.setColour (colours::dim);
    g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
    g.drawText ("CPU PER MODULE", getLocalBounds().reduced (10, 0).removeFromTop (20), juce::Justification::centredLeft);

    for (int m = 0; m < numModules; ++m)
    {
        auto row = getLocalBounds().reduced (10, 0).withTrimmedTop (22 + m * 17).removeFromTop (17);
        const float v = us[(size_t) m];
        const float pct = blockUs > 0.0f ? 100.0f * v / blockUs : 0.0f;
        g.setFont (NorthlineLookAndFeel::font (tokens::type::value, true));
        g.setColour (colours::text);
        g.drawText (moduleName (m), row.removeFromLeft (70), juce::Justification::centredLeft);
        g.setFont (NorthlineLookAndFeel::font (tokens::type::value));
        g.setColour (colours::dim);
        g.drawText (juce::String (v, 1) + " us", row.removeFromLeft (70), juce::Justification::centredLeft);
        g.drawText (juce::String (pct, 2) + " %", row, juce::Justification::centredRight);
    }
}
} // namespace nl

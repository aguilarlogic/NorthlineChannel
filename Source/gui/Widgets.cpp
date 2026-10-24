#include "Widgets.h"

namespace nl
{
//==============================================================================
namespace ui
{
namespace
{
juce::String normaliseEntry (const juce::String& text)
{
    auto s = text.removeCharacters (" \t\r\n").toLowerCase();
    s = s.replaceCharacter ((juce::juce_wchar) 0x2212, (juce::juce_wchar) '-'); // Unicode minus sign
    return s;
}

bool readNumber (const juce::String& s, double& out)
{
    const auto utf = s.toStdString();
    char* end = nullptr;
    const double v = std::strtod (utf.c_str(), &end);
    if (end == utf.c_str() || *end != '\0' || ! std::isfinite (v)) return false;
    out = v;
    return true;
}

bool helpMode_ = false;
float displayScale_ = 1.0f;

// Component that draws the bubble; one instance is shared by the process.
class BubbleComponent : public juce::Component
{
public:
    BubbleComponent() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        const float s = displayScale();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours::panelHeader);
        g.fillRoundedRectangle (r, 4.0f * s);
        g.setColour (colours::controlEdge);
        g.drawRoundedRectangle (r, 4.0f * s, 1.0f);
        g.setColour (colours::text);
        g.setFont (NorthlineLookAndFeel::font (tokens::type::value * s));
        g.drawText (text, getLocalBounds().reduced ((int) (8 * s), 0), juce::Justification::centred);
    }

    juce::String text;
};

std::unique_ptr<BubbleComponent> bubble;
juce::Component::SafePointer<juce::Component> bubbleOwner;
} // namespace

bool parseValueText (const juce::String& text, Unit unit, float minV, float maxV, float& plainOut)
{
    auto t = normaliseEntry (text);
    if (t.endsWith (":1")) t = t.dropLastCharacters (2);
    if (t.isEmpty()) return false;

    if (t == "inf" || t == "infinity" || t == juce::String::fromUTF8 ("\xe2\x88\x9e"))
    {
        plainOut = juce::jlimit (minV, maxV, unit == Unit::Ratio ? kRatioInf : maxV);
        return true;
    }

    double mult = 1.0;
    if (t.endsWith ("hz")) t = t.dropLastCharacters (2);
    else if (t.endsWith ("db")) t = t.dropLastCharacters (2);
    else if (t.endsWith ("ms")) t = t.dropLastCharacters (2);
    else if (t.endsWith ("%")) t = t.dropLastCharacters (1);
    else if (t.endsWith ("s") && unit == Unit::Ms) { mult = 1000.0; t = t.dropLastCharacters (1); }

    if (t.endsWith ("k")) { mult *= 1000.0; t = t.dropLastCharacters (1); }

    double v = 0.0;
    if (! readNumber (t, v)) return false;
    plainOut = juce::jlimit (minV, maxV, (float) (v * mult));
    return true;
}

bool parseParamText (int paramIndex, const juce::String& text, float& plainOut)
{
    const auto& d = paramDef (paramIndex);
    if (d.kind == ParamDef::Bool)
    {
        const auto t = normaliseEntry (text);
        if (t == "on" || t == "yes" || t == "true" || t == "1") { plainOut = 1.0f; return true; }
        if (t == "off" || t == "no" || t == "false" || t == "0") { plainOut = 0.0f; return true; }
        return false;
    }
    if (d.kind == ParamDef::Choice)
    {
        const auto t = normaliseEntry (text);
        for (int i = 0; i < (int) d.choices.size(); ++i)
            if (normaliseEntry (d.choices[(size_t) i]) == t) { plainOut = (float) i; return true; }
        double v = 0.0;
        if (readNumber (t, v) && v >= 0.0 && v < (double) d.choices.size()) { plainOut = (float) std::lround (v); return true; }
        return false;
    }
    return parseValueText (text, d.unit, d.minV, d.maxV, plainOut);
}

float dragDelta (float dxPx, float dyPx, bool fine)
{
    return (dxPx + dyPx) / 200.0f * (fine ? 0.1f : 1.0f);
}

float wheelDelta (float wheelY, bool fine)
{
    return wheelY * 0.03f * (fine ? 0.1f : 1.0f);
}

float keyStep (bool up, bool shift, bool coarse)
{
    const float base = (coarse ? 0.10f : 0.01f) * (shift ? 0.1f : 1.0f);
    return up ? base : -base;
}

bool isResetClick (const juce::ModifierKeys& mods)
{
    return mods.isAltDown() || mods.isCommandDown();
}

float clampNormalised (float n) { return juce::jlimit (0.0f, 1.0f, n); }

namespace
{
struct HelpEntry { const char* id; const char* text; };

const HelpEntry kHelp[] {
    { "in_gain", "Trims the signal before any processing. Sets the level the chain sees." },
    { "out_gain", "Trims the signal after processing. Changes loudness." },
    { "global_mix", "Blends the processed signal with the dry input. 100 % is fully processed." },
    { "quality", "Eco runs the nonlinear stages at the base rate. HQ oversamples them to cut aliasing, at a latency cost." },
    { "lim_ceiling", "Highest output level the limiter allows, in dBTP." },
    { "lim_release", "How quickly the limiter lets go after a peak." },
    { "vu_source", "What the VU meter shows: input level, output level or total gain reduction." },
    { "op_level", "Reference level for the VU meter and auto gain." },
    { "sat_drive", "How hard the signal is pushed into the saturation stage. More drive adds harmonics and loudness." },
    { "sat_mix", "Blend of the saturated and the clean signal." },
    { "sat_tone", "Tilts the saturated signal darker (negative) or brighter (positive)." },
    { "hp_freq", "High-pass cutoff: content below this frequency is removed." },
    { "lp_freq", "Low-pass cutoff: content above this frequency is removed." },
    { "weight_freq", "Frequency of the Weight low-end emphasis." },
    { "gate_thresh", "Level that opens the gate. Below it, the signal is reduced by the Range." },
    { "gate_hyst", "Gap between the opening and closing levels, so the gate does not chatter." },
    { "gate_floor", "How far the gate reduces the signal while closed." },
    { "gate_attack", "How fast the gate opens once the level crosses the threshold." },
    { "gate_hold", "How long the gate stays open after the signal drops below threshold." },
    { "gate_release", "How fast the gate closes after the hold time." },
    { "gate_sc_hp", "High-pass on the detector only, so rumble does not open the gate." },
    { "gate_sc_lp", "Low-pass on the detector only, so hiss does not open the gate." },
    { "ds_release", "Recovery time after the de-esser stops reducing." },
    { "ds_out", "Output trim after de-essing." },
    { "comp_thresh", "Level above which compression starts." },
    { "comp_ratio", "How much the level above threshold is reduced. inf is a limiter." },
    { "comp_knee", "Softness of the threshold. Wider knees start compressing more gradually." },
    { "comp_attack", "How fast compression starts after the level crosses the threshold." },
    { "comp_release", "How fast the gain returns after the level falls." },
    { "comp_mix", "Parallel blend. Below 100 % the dry signal is mixed back in." },
    { "comp_out", "Output trim after compression." },
    { "comp_makeup", "Automatic make-up gain. Off by default so the compressor can duck." },
    { "comp_sc_hp", "High-pass on the detector, so bass does not pump the gain." },
};

struct SuffixHelp { const char* suffix; const char* text; };
const SuffixHelp kSuffixHelp[] {
    { "_freq", "Frequency this control sets, in Hz." },
    { "_gain", "Boost (positive) or cut (negative), in dB." },
    { "_q", "Width of the band. Higher Q is narrower." },
    { "_thresh", "Level at which this control starts to act." },
    { "_range", "Maximum amount of reduction, in dB." },
    { "_attack", "How quickly the effect starts once the signal rises." },
    { "_release", "How quickly the effect ends once the signal falls." },
    { "_mix", "Blend between the processed and the dry signal." },
};
} // namespace

juce::String shortDescription (int paramIndex)
{
    const auto help = helpText (paramIndex);
    return help.isNotEmpty() ? help : juce::String (paramDef (paramIndex).name) + " setting";
}

juce::String helpText (int paramIndex)
{
    const auto& d = paramDef (paramIndex);
    const juce::String id (d.id);
    for (auto& h : kHelp)
        if (id == h.id) return h.text;
    for (auto& h : kSuffixHelp)
        if (id.endsWith (h.suffix)) return h.text;
    return {};
}

juce::String tooltipText (int paramIndex)
{
    const auto& d = paramDef (paramIndex);
    juce::String s;
    const auto help = helpText (paramIndex);
    if (help.isNotEmpty()) s << help << " ";
    s << d.name << ". ";
    if (d.kind == ParamDef::Bool)
    {
        s << "Click or press Space to toggle.";
    }
    else if (d.kind == ParamDef::Choice)
    {
        juce::StringArray names;
        for (auto* c : d.choices) names.add (c);
        s << "Options: " << names.joinIntoString (", ") << ".";
    }
    else
    {
        s << "Range " << formatValue (paramIndex, d.minV) << " to " << formatValue (paramIndex, d.maxV)
          << ". Drag, wheel or arrow keys to adjust; Shift for fine; double-click to type; Alt-click resets; right-click for more.";
    }
    return s;
}

void setHelpMode (bool on) { helpMode_ = on; }
bool helpMode() { return helpMode_; }
void setDisplayScale (float s) { displayScale_ = juce::jlimit (0.5f, 2.5f, s); }
float displayScale() { return displayScale_; }

void showBubble (juce::Component& owner, const juce::String& text)
{
    auto* top = owner.getTopLevelComponent();
    if (top == nullptr || text.isEmpty()) return;
    if (bubble == nullptr) bubble = std::make_unique<BubbleComponent>();
    if (bubble->getParentComponent() != top) top->addAndMakeVisible (*bubble);
    bubble->text = text;
    bubbleOwner = &owner;

    const float s = displayScale();
    const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidthInt (NorthlineLookAndFeel::font (tokens::type::value * s), text) + 16 * s);
    const int h = juce::roundToInt (22 * s);
    const auto anchor = top->getLocalArea (nullptr, owner.getScreenBounds());
    auto r = juce::Rectangle<int> (w, h).withCentre ({ anchor.getCentreX(), anchor.getY() - h / 2 - 4 });
    if (r.getY() < top->getLocalBounds().getY()) r.setY (anchor.getBottom() + 4);
    r = r.constrainedWithin (top->getLocalBounds());
    bubble->setBounds (r);
    bubble->setVisible (true);
    bubble->toFront (false);
    bubble->repaint();
}

void hideBubble (juce::Component& owner)
{
    if (bubble != nullptr && bubbleOwner.getComponent() == &owner)
    {
        bubble->setVisible (false);
        bubbleOwner = nullptr;
    }
}
} // namespace ui

//==============================================================================
ParamBinding::ParamBinding (NorthlineProcessor& p, int paramIndex, std::function<void()> onChanged)
    : proc (p), paramIdx (paramIndex), prm (p.param (paramIndex)), changed (std::move (onChanged))
{
    attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float) { if (changed) changed(); });
}

ParamBinding::~ParamBinding()
{
    stopTimer();
    if (wheelOpen) endGesture();
}

float ParamBinding::plain() const { return prm->convertFrom0to1 (prm->getValue()); }
float ParamBinding::normalised() const { return prm->getValue(); }
float ParamBinding::defaultPlain() const { return prm->convertFrom0to1 (prm->getDefaultValue()); }

void ParamBinding::sendInitialUpdate() { attachment->sendInitialUpdate(); }

void ParamBinding::setNormalised (float n) { attachment->setValueAsPartOfGesture (prm->convertFrom0to1 (ui::clampNormalised (n))); }

void ParamBinding::beginGesture() { attachment->beginGesture(); }
void ParamBinding::endGesture() { attachment->endGesture(); }

void ParamBinding::setPlainAsGesture (float v) { attachment->setValueAsCompleteGesture (v); }

void ParamBinding::setNormalisedAsGesture (float n) { setPlainAsGesture (prm->convertFrom0to1 (ui::clampNormalised (n))); }

void ParamBinding::resetToDefault() { setNormalisedAsGesture (prm->getDefaultValue()); }

void ParamBinding::wheelStep (float d)
{
    if (! wheelOpen)
    {
        beginGesture();
        wheelOpen = true;
    }
    setNormalised (normalised() + d);
    startTimer (300);
}

void ParamBinding::timerCallback()
{
    stopTimer();
    if (wheelOpen)
    {
        endGesture();
        wheelOpen = false;
    }
}

void showParamMenu (juce::Component& owner, ParamBinding& binding, std::function<void()> enterValue)
{
    const int index = binding.index();
    auto& proc = binding.processor();
    const auto& def = paramDef (index);

    juce::PopupMenu m;
    m.addSectionHeader (def.name);
    m.addItem (1, "Reset to default (" + formatValue (index, binding.defaultPlain()) + ")");
    if (enterValue) m.addItem (2, "Enter value...");
    m.addItem (3, "Copy value (" + formatValue (index, binding.plain()) + ")");
    m.addItem (4, "Paste value");
    m.addSeparator();
    m.addItem (5, proc.isInSpotlight (index) ? "Remove from Spotlight" : "Add to Spotlight");

    juce::Component::SafePointer<juce::Component> safe (&owner);
    ParamBinding* b = &binding;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner),
                     [safe, b, enterValue, index, &proc] (int result)
                     {
                         if (safe == nullptr || result <= 0) return;
                         switch (result)
                         {
                             case 1: b->resetToDefault(); break;
                             case 2: if (enterValue) enterValue(); break;
                             case 3: juce::SystemClipboard::copyTextToClipboard (formatValue (index, b->plain())); break;
                             case 4:
                             {
                                 float v = 0.0f;
                                 if (ui::parseParamText (index, juce::SystemClipboard::getTextFromClipboard(), v)) b->setPlainAsGesture (v);
                                 break;
                             }
                             case 5: proc.toggleSpotlight (index); break;
                             default: break;
                         }
                     });
}

//==============================================================================
namespace
{
bool isBipolarDb (const ParamDef& d)
{
    return d.minV < 0.0f && d.maxV > 0.0f && d.unit == Unit::Db && std::abs (d.minV + d.maxV) < 1.0e-3f;
}

class KnobValue : public juce::AccessibilityTextValueInterface
{
public:
    explicit KnobValue (Knob& k) : owner (k) {}
    bool isReadOnly() const override { return false; }
    juce::String getCurrentValueAsString() const override
    {
        return formatValue (owner.getBinding().index(), owner.getBinding().plain());
    }
    void setValueAsString (const juce::String& s) override
    {
        float v = 0.0f;
        if (ui::parseParamText (owner.getBinding().index(), s, v)) owner.getBinding().setPlainAsGesture (v);
    }

private:
    Knob& owner;
};

class SegmentValue : public juce::AccessibilityTextValueInterface
{
public:
    explicit SegmentValue (Segmented& s) : owner (s) {}
    bool isReadOnly() const override { return false; }
    juce::String getCurrentValueAsString() const override
    {
        const auto& d = paramDef (owner.getBinding().index());
        const int i = owner.getSelected();
        return i >= 0 && i < (int) d.choices.size() ? juce::String (d.choices[(size_t) i]) : juce::String();
    }
    void setValueAsString (const juce::String& s) override
    {
        float v = 0.0f;
        if (ui::parseParamText (owner.getBinding().index(), s, v)) owner.getBinding().setPlainAsGesture (v);
    }

private:
    Segmented& owner;
};

class GrValue : public juce::AccessibilityTextValueInterface
{
public:
    explicit GrValue (GrBar& g) : owner (g) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return juce::String (owner.getGr(), 1) + " dB"; }
    void setValueAsString (const juce::String&) override {}

private:
    GrBar& owner;
};
} // namespace

//==============================================================================
Knob::Knob (NorthlineProcessor& p, int paramIndex, const juce::String& text, juce::Colour acc)
    : label (text), accent (acc),
      binding (p, paramIndex, [this] { repaint(); updateBubble(); })
{
    setWantsKeyboardFocus (true);
    setTitle (paramDef (paramIndex).name);
    setDescription (ui::shortDescription (paramIndex));
    setTooltip (ui::tooltipText (paramIndex));
    binding.sendInitialUpdate();
}

Knob::~Knob()
{
    ui::hideBubble (*this);
}

void Knob::resized()
{
    if (editor != nullptr) editor->setBounds (getLocalBounds().removeFromBottom (14).reduced (2, 0));
}

void Knob::paint (juce::Graphics& g)
{
    const bool on = isEnabled();
    auto area = getLocalBounds();
    auto top = area.removeFromTop (14);
    auto bottom = area.removeFromBottom (14);

    g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
    g.setColour (on ? (hovered ? colours::text : colours::dim) : colours::faint);
    g.drawFittedText (label.toUpperCase(), top, juce::Justification::centred, 1, 0.7f);

    const float s = (float) juce::jmin (area.getWidth(), area.getHeight());
    const auto knobArea = area.toFloat().withSizeKeepingCentre (s, s);
    const bool keyFocus = hasKeyboardFocus (false);
    NorthlineLookAndFeel::drawKnob (g, knobArea, binding.normalised(), accent,
                                    isBipolarDb (paramDef (binding.index())), on, keyFocus);

    if (editor == nullptr)
    {
        const auto text = formatValue (binding.index(), binding.plain());
        const float natural = figures::layout (NorthlineLookAndFeel::font (tokens::type::value), text).width;
        const float fit = juce::jlimit (0.7f, 1.0f, (float) bottom.getWidth() / juce::jmax (1.0f, natural));
        const auto font = NorthlineLookAndFeel::font (tokens::type::value * fit);
        g.setColour (on ? colours::text : colours::faint);
        figures::draw (g, text, bottom.toFloat(), juce::Justification::centred, font);
    }
}

void Knob::setFocusState (bool highlighted, bool dimmed)
{
    getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    repaint();
}

void Knob::updateBubble()
{
    const bool show = dragging || hovered || hasKeyboardFocus (false);
    if (show) ui::showBubble (*this, formatValue (binding.index(), binding.plain()));
    else ui::hideBubble (*this);
}

void Knob::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    if (ui::helpMode()) ui::showBubble (*this, ui::tooltipText (binding.index()));
    else updateBubble();
    repaint();
}

void Knob::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    updateBubble();
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    grabKeyboardFocus();
    if (e.mods.isPopupMenu())
    {
        showParamMenu (*this, binding, [this] { beginTextEntry(); });
        return;
    }
    if (ui::isResetClick (e.mods))
    {
        binding.resetToDefault();
        updateBubble();
        return;
    }
    if (e.getNumberOfClicks() >= 2)
    {
        beginTextEntry();
        return;
    }
    dragging = true;
    lastPos = e.position;
    binding.beginGesture();
    updateBubble();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const auto d = e.position - lastPos;
    lastPos = e.position;
    binding.setNormalised (binding.normalised() + ui::dragDelta (d.x, -d.y, e.mods.isShiftDown()));
    updateBubble();
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    binding.endGesture();
    updateBubble();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! isEnabled()) return;
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX;
    if (std::abs (d) < 1.0e-6f) return;
    binding.wheelStep (ui::wheelDelta (d, e.mods.isShiftDown()));
    updateBubble();
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled()) return false;
    const int code = key.getKeyCode();
    const bool shift = key.getModifiers().isShiftDown();
    if (code == juce::KeyPress::returnKey)
    {
        beginTextEntry();
        return true;
    }
    bool up = false, handled = true;
    bool coarse = false;
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) up = true;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) up = false;
    else if (code == juce::KeyPress::pageUpKey) { up = true; coarse = true; }
    else if (code == juce::KeyPress::pageDownKey) { up = false; coarse = true; }
    else handled = false;
    if (! handled) return false;
    binding.setNormalisedAsGesture (binding.normalised() + ui::keyStep (up, shift, coarse));
    updateBubble();
    return true;
}

void Knob::focusGained (FocusChangeType)
{
    repaint();
    updateBubble();
}

void Knob::focusLost (FocusChangeType)
{
    repaint();
    updateBubble();
}

void Knob::beginTextEntry()
{
    if (editor != nullptr) return;
    editor = std::make_unique<juce::TextEditor> ("knobEntry");
    editor->setFont (NorthlineLookAndFeel::font (tokens::type::value));
    editor->setJustification (juce::Justification::centred);
    editor->setText (formatValue (binding.index(), binding.plain()), false);
    editor->selectAll();
    editor->onReturnKey = [this] { if (commitTextEntry()) closeTextEntry(); };
    editor->onEscapeKey = [this] { closeTextEntry(); };
    editor->onFocusLost = [this] { commitTextEntry(); closeTextEntry(); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
}

bool Knob::commitTextEntry()
{
    if (editor == nullptr) return false;
    float v = 0.0f;
    if (ui::parseParamText (binding.index(), editor->getText(), v))
    {
        binding.setPlainAsGesture (v);
        return true;
    }
    ui::showBubble (*this, "Try 3k, -18, 50%, 100ms or inf");
    return false;
}

void Knob::closeTextEntry()
{
    if (editor == nullptr) return;
    removeChildComponent (editor.get());
    // Deleted later: this may run inside the editor's own callback.
    std::shared_ptr<juce::TextEditor> keep (std::move (editor));
    juce::MessageManager::callAsync ([keep] {});
    updateBubble();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces (std::make_unique<KnobValue> (*this)));
}

//==============================================================================
Chip::Chip (NorthlineProcessor& p, int paramIndex, const juce::String& text, juce::Colour accent)
    : juce::TextButton (text),
      binding (p, paramIndex, [this] { setToggleState (binding.plain() >= 0.5f, juce::dontSendNotification); repaint(); })
{
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, accent);
    onClick = [this] { binding.setPlainAsGesture (getToggleState() ? 1.0f : 0.0f); };
    setToggleState (binding.plain() >= 0.5f, juce::dontSendNotification);
    setTitle (paramDef (paramIndex).name);
    setDescription (ui::shortDescription (paramIndex));
    setTooltip (ui::tooltipText (paramIndex));
}

Chip::~Chip() = default;

void Chip::setFocusState (bool highlighted, bool dimmed)
{
    getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    repaint();
}

void Chip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParamMenu (*this, binding, nullptr);
        return;
    }
    if (ui::isResetClick (e.mods))
    {
        binding.resetToDefault();
        return;
    }
    juce::TextButton::mouseDown (e);
}

//==============================================================================
ChoiceBox::ChoiceBox (NorthlineProcessor& p, int paramIndex)
    : binding (p, paramIndex, [this] { setSelectedItemIndex (juce::roundToInt (binding.plain()), juce::dontSendNotification); })
{
    const auto& d = paramDef (paramIndex);
    int id = 1;
    for (auto* c : d.choices) addItem (c, id++);
    onChange = [this] { binding.setPlainAsGesture ((float) getSelectedItemIndex()); };
    setSelectedItemIndex (juce::roundToInt (binding.plain()), juce::dontSendNotification);
    setTitle (d.name);
    setDescription (ui::shortDescription (paramIndex));
    setTooltip (ui::tooltipText (paramIndex));
}

ChoiceBox::~ChoiceBox() = default;

void ChoiceBox::setFocusState (bool highlighted, bool dimmed)
{
    getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    repaint();
}

void ChoiceBox::stepBy (int delta, bool)
{
    const int n = getNumItems();
    if (n <= 0) return;
    const int i = juce::jlimit (0, n - 1, getSelectedItemIndex() + delta);
    if (i != getSelectedItemIndex()) binding.setPlainAsGesture ((float) i);
}

void ChoiceBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParamMenu (*this, binding, [this] { showPopup(); });
        return;
    }
    if (ui::isResetClick (e.mods))
    {
        binding.resetToDefault();
        return;
    }
    juce::ComboBox::mouseDown (e);
}

void ChoiceBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled() || std::abs (w.deltaY) < 1.0e-6f) return;
    stepBy (w.deltaY > 0.0f ? 1 : -1, false);
}

bool ChoiceBox::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::pageUpKey) { stepBy (3, false); return true; }
    if (code == juce::KeyPress::pageDownKey) { stepBy (-3, false); return true; }
    return juce::ComboBox::keyPressed (key);
}

//==============================================================================
class SegButton : public juce::TextButton
{
public:
    SegButton (Segmented& o, const juce::String& text) : juce::TextButton (text), owner (o) {}

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        if (e.mods.isPopupMenu()) { owner.showMenuForButton (*this); return; }
        if (ui::isResetClick (e.mods)) { owner.getBinding().resetToDefault(); return; }
        juce::TextButton::mouseDown (e);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        owner.mouseWheelMove (e, w);
    }

private:
    Segmented& owner;
};

Segmented::Segmented (NorthlineProcessor& p, int paramIndex, juce::Colour accent)
    : index (paramIndex), binding (p, paramIndex, [this] { select (juce::roundToInt (binding.plain())); })
{
    setWantsKeyboardFocus (true);
    const auto& d = paramDef (paramIndex);
    for (int i = 0; i < (int) d.choices.size(); ++i)
    {
        auto* b = buttons.add (new SegButton (*this, d.choices[(size_t) i]));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, i] { choose (i); };
        b->setTooltip (ui::tooltipText (paramIndex));
        addAndMakeVisible (b);
    }
    setTitle (d.name);
    setDescription (ui::shortDescription (paramIndex));
    setTooltip (ui::tooltipText (paramIndex));
    binding.sendInitialUpdate();
}

Segmented::~Segmented() = default;

void Segmented::select (int i)
{
    selected = i;
    for (int k = 0; k < buttons.size(); ++k) buttons[k]->setToggleState (k == i, juce::dontSendNotification);
    if (onChange) onChange();
}

void Segmented::choose (int i)
{
    binding.setPlainAsGesture ((float) i);
    select (i);
}

void Segmented::stepBy (int delta)
{
    const int i = juce::jlimit (0, buttons.size() - 1, selected + delta);
    if (i != selected) choose (i);
}

void Segmented::showMenuForButton (juce::Component&)
{
    showParamMenu (*this, binding, nullptr);
}

void Segmented::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled() || std::abs (w.deltaY) < 1.0e-6f) return;
    stepBy (w.deltaY > 0.0f ? 1 : -1);
}

bool Segmented::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::pageDownKey) { stepBy (-1); return true; }
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::pageUpKey) { stepBy (1); return true; }
    return false;
}

void Segmented::resized()
{
    auto r = getLocalBounds();
    const int n = buttons.size();
    if (n == 0) return;
    const int gap = 2;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
    {
        buttons[i]->setBounds (r.removeFromLeft (i == n - 1 ? r.getWidth() : w));
        r.removeFromLeft (gap);
    }
}

void Segmented::setFocusState (bool highlighted, bool dimmed)
{
    for (auto* b : buttons) { b->getProperties().set ("focus", highlighted); b->repaint(); }
    setAlpha (dimmed ? 0.4f : 1.0f);
}

std::unique_ptr<juce::AccessibilityHandler> Segmented::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group, juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces (std::make_unique<SegmentValue> (*this)));
}

//==============================================================================
GrBar::GrBar (juce::Colour c, float maxDb) : colour (c), range (maxDb)
{
    setTitle ("Gain reduction");
    setDescription ("Gain reduction in dB, ticks at 1, 3, 6, 10 and 20 dB");
}

void GrBar::setGr (float db)
{
    db = juce::jlimit (-range, 0.0f, db);
    if (std::abs (db - value) > 0.05f) { value = db; repaint(); }
}

void GrBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::track);
    g.fillRoundedRectangle (r, 2.0f);
    const float frac = -value / range;
    if (frac > 0.0f)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (r.withLeft (r.getRight() - r.getWidth() * frac), 2.0f);
    }
    g.setColour (colours::text.withAlpha (0.5f));
    for (float tick : { 1.0f, 3.0f, 6.0f, 10.0f, 20.0f })
    {
        if (tick >= range) continue;
        const float x = r.getRight() - r.getWidth() * tick / range;
        g.fillRect (juce::Rectangle<float> (x, r.getY(), 1.0f, r.getHeight()));
    }
    if (showValue)
    {
        g.setColour (colours::text.withAlpha (0.85f));
        figures::draw (g, value < -0.05f ? juce::String (value, 1) : juce::String ("0.0"), getLocalBounds().reduced (3, 0).toFloat(),
                       juce::Justification::centredLeft, NorthlineLookAndFeel::font (tokens::type::label));
    }
}

std::unique_ptr<juce::AccessibilityHandler> GrBar::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::progressBar, juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces (std::make_unique<GrValue> (*this)));
}

//==============================================================================
void StereoMeter::setLevels (float peakL, float peakR, float rmsL, float rmsR)
{
    const float pk[2] { peakL, peakR }, rm[2] { rmsL, rmsR };
    for (int c = 0; c < 2; ++c)
    {
        const float p = juce::Decibels::gainToDecibels (pk[c], -100.0f);
        peakDb[c] = p > peakDb[c] ? p : juce::jmax (p, peakDb[c] - 1.0f); // ~30 dB/s at 30 fps
        rmsDb[c] = juce::Decibels::gainToDecibels (rm[c], -100.0f);
        if (p >= holdDb[c]) { holdDb[c] = p; holdFrames[c] = 45; }
        else if (--holdFrames[c] <= 0) holdDb[c] = juce::jmax (-100.0f, holdDb[c] - 0.7f);
        if (pk[c] >= 1.0f) clip = true;
    }
    repaint();
}

void StereoMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto clipR = r.removeFromTop (6.0f);
    g.setColour (clip ? colours::meterRed : colours::track);
    g.fillRoundedRectangle (clipR, 1.5f);
    r.removeFromTop (2.0f);

    // dB numbers in a gutter on the left, at the height of each tick.
    auto gutter = r.removeFromLeft (kScaleGutter);
    r.removeFromLeft (2.0f);
    auto toY = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
    g.setFont (NorthlineLookAndFeel::font (tokens::type::label));
    g.setColour (colours::dim);
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
    {
        const float y = toY (db);
        g.drawText (db == 0.0f ? juce::String ("0") : juce::String ((int) db), juce::Rectangle<float> (gutter.getX(), y - 6.0f, gutter.getWidth() - 3.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    const float w = (r.getWidth() - 2.0f) * 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) c * (w + 2.0f), r.getY(), w, r.getHeight());
        g.setColour (colours::track);
        g.fillRect (bar);
        const float yPk = toY (peakDb[c]);
        juce::ColourGradient grad (colours::meterGreen, 0, toY (-60.0f), colours::meterRed, 0, toY (6.0f), false);
        grad.addColour ((toY (-12.0f) - toY (-60.0f)) / (toY (6.0f) - toY (-60.0f)), colours::meterGreen);
        grad.addColour ((toY (-3.0f) - toY (-60.0f)) / (toY (6.0f) - toY (-60.0f)), colours::meterYellow);
        g.setGradientFill (grad);
        g.fillRect (bar.withTop (yPk));
        // dB scale ticks, drawn over the bar
        g.setColour (colours::text.withAlpha (0.35f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
            g.fillRect (juce::Rectangle<float> (bar.getX(), toY (db), bar.getWidth(), 1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (bar.getX(), toY (rmsDb[c]) - 1.0f, bar.getWidth(), 2.0f);
        g.setColour (colours::text);
        g.fillRect (bar.getX(), toY (holdDb[c]) - 0.5f, bar.getWidth(), 1.0f);
    }
}

//==============================================================================
void VuMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xffe9e0c8));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

    const auto pivot = juce::Point<float> (r.getCentreX(), r.getBottom() + r.getHeight() * 0.35f);
    const float len = r.getHeight() * 1.12f;
    auto angleFor = [] (float vu) { return juce::jmap (juce::jlimit (-20.0f, 3.0f, vu), -20.0f, 3.0f, -0.78f, 0.78f); };

    g.setFont (NorthlineLookAndFeel::font (tokens::type::label));
    for (float m : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float a = angleFor (m);
        const auto p1 = pivot.getPointOnCircumference (len * 0.86f, a);
        const auto p2 = pivot.getPointOnCircumference (len * 0.94f, a);
        g.setColour (m > 0.0f ? juce::Colour (0xffc0392b) : juce::Colour (0xff2b2b2b));
        g.drawLine ({ p1, p2 }, 1.3f);
        if (m == -20.0f || m == -10.0f || m == -5.0f || m == 0.0f || m == 3.0f)
        {
            const auto t = pivot.getPointOnCircumference (len * 0.78f, a);
            g.drawText (juce::String ((int) m), juce::Rectangle<float> (26, 12).withCentre (t), juce::Justification::centred);
        }
    }
    g.setColour (juce::Colour (0xff2b2b2b));
    g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
    g.drawText (label, r.removeFromBottom (16.0f), juce::Justification::centred);

    const float a = angleFor (value);
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawLine ({ pivot.getPointOnCircumference (len * 0.25f, a), pivot.getPointOnCircumference (len * 0.97f, a) }, 1.6f);
}

void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour accent)
{
    g.setColour (accent.withAlpha (0.9f));
    g.setFont (NorthlineLookAndFeel::font (tokens::type::label, true));
    g.drawText (text, r, juce::Justification::centredLeft);
    const int tw = juce::GlyphArrangement::getStringWidthInt (NorthlineLookAndFeel::font (tokens::type::label, true), text);
    g.setColour (colours::edge);
    g.drawHorizontalLine (r.getCentreY(), (float) (r.getX() + tw + 6), (float) r.getRight());
}
} // namespace nl

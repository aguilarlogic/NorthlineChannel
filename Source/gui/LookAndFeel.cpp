#include "LookAndFeel.h"

namespace nl
{
namespace colours
{
juce::Colour background, panel, panelHeader, edge, track, arcTrack, controlEdge;
juce::Colour text, dim, faint, focus, amber, red;
juce::Colour meterGreen, meterYellow, meterRed;
juce::Colour pre, gate, ds, eq, comp, master;

juce::Colour forModule (int moduleId)
{
    switch (moduleId)
    {
        case 0: return pre;
        case 1: return gate;
        case 2: return ds;
        case 3: return eq;
        case 4: return comp;
        default: return master;
    }
}
} // namespace colours

namespace
{
tokens::Theme currentTheme = tokens::Theme::dark;

// Runs at start-up so the colour names are valid before the first component exists.
const bool themeInitialised = (applyTheme (tokens::Theme::dark), true);
}

void applyTheme (tokens::Theme theme)
{
    using namespace colours;
    const auto& p = tokens::palette (theme);
    currentTheme = theme;
    background = p.background;
    panel = p.panel;
    panelHeader = p.panelHeader;
    edge = p.edge;
    track = p.track;
    arcTrack = p.arcTrack;
    controlEdge = p.controlEdge;
    text = p.text;
    dim = p.dim;
    faint = p.faint;
    focus = p.focus;
    amber = p.amber;
    red = p.red;
    meterGreen = p.meterGreen;
    meterYellow = p.meterYellow;
    meterRed = p.meterRed;
    pre = p.pre;
    gate = p.gate;
    ds = p.ds;
    eq = p.eq;
    comp = p.comp;
    master = p.master;
}

tokens::Theme activeTheme() noexcept { return currentTheme; }

NorthlineLookAndFeel::NorthlineLookAndFeel()
{
    refreshColours();
}

void NorthlineLookAndFeel::refreshColours()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, track);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, controlEdge);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panelHeader);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, eq.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, dim);
    setColour (juce::TextButton::buttonColourId, track);
    setColour (juce::TextButton::buttonOnColourId, eq);
    setColour (juce::TextButton::textColourOffId, dim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, panelHeader);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, edge);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, track);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, controlEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, focus);
    setColour (juce::TextEditor::highlightColourId, eq.withAlpha (0.45f));
}

juce::Font NorthlineLookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions {}.withHeight (height).withStyle (bold ? "Bold" : "Regular"));
}

void NorthlineLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, juce::Colour accent, bool bipolar, bool enabled, bool focused)
{
    const auto bounds = area.reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const float start = juce::degreesToRadians (225.0f), end = juce::degreesToRadians (495.0f);
    const float angle = start + pos * (end - start);

    if (focused)
    {
        g.setColour (colours::focus.withAlpha (0.22f));
        g.fillEllipse (bounds.expanded (2.0f));
    }

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colours::arcTrack);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (enabled ? accent : accent.withSaturation (0.0f).withAlpha (0.5f));
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float knobR = arcR - lineW * 1.1f;
    g.setColour (colours::track);
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
    g.setColour (colours::controlEdge.withAlpha (0.6f));
    g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

    juce::Path pointer;
    const float pw = juce::jmax (1.8f, lineW * 0.55f);
    pointer.addRoundedRectangle (-pw * 0.5f, -knobR + 2.0f, pw, knobR * 0.55f, pw * 0.5f);
    g.setColour (enabled ? colours::text : colours::faint);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    if (focused)
    {
        g.setColour (colours::focus);
        g.drawEllipse (bounds.expanded (1.0f), 1.2f);
    }
}

void NorthlineLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
{
    drawKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, s.findColour (juce::Slider::rotarySliderFillColourId),
              (bool) s.getProperties().getWithDefault ("bipolar", false), s.isEnabled(),
              (bool) s.getProperties().getWithDefault ("focus", false));
}

void NorthlineLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    auto fill = on ? accent.withAlpha (0.85f) : colours::track;
    if (highlighted) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, tokens::radius::control);
    g.setColour (on ? accent.brighter (0.2f) : colours::controlEdge);
    g.drawRoundedRectangle (r, tokens::radius::control, 1.0f);
    if ((bool) b.getProperties().getWithDefault ("focus", false))
    {
        g.setColour (colours::focus);
        g.drawRoundedRectangle (r.expanded (0.5f), tokens::radius::control, 1.2f);
    }
}

void NorthlineLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? juce::Colours::white : colours::dim);
    if (! b.isEnabled()) g.setColour (colours::faint);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.8f);
}

juce::Font NorthlineLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (juce::jmax (tokens::type::label + 1.0f, juce::jmin (13.0f, (float) h * 0.58f)), true);
}

void NorthlineLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::track);
    g.fillRoundedRectangle (r, tokens::radius::control);
    g.setColour ((bool) box.getProperties().getWithDefault ("focus", false) ? colours::focus : colours::controlEdge);
    g.drawRoundedRectangle (r, tokens::radius::control, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (colours::dim);
    g.fillPath (arrow);
}

void NorthlineLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 16, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font NorthlineLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmax (tokens::type::label + 1.0f, juce::jmin (13.0f, (float) box.getHeight() * 0.58f)));
}

juce::Font NorthlineLookAndFeel::getPopupMenuFont() { return font (14.0f); }

void NorthlineLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::panelHeader);
    g.setColour (colours::edge);
    g.drawRect (0, 0, w, h);
}

void NorthlineLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (colours::panelHeader);
    g.setColour (colours::edge);
    g.drawRect (0, 0, w, h);
    g.setColour (colours::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 3), juce::Justification::centredLeft, 6);
}
} // namespace nl

#include "Widgets.h"

namespace nl
{
//==============================================================================
Knob::Knob (NorthlineProcessor& p, int paramIndex, const juce::String& text, juce::Colour accent)
    : label (text), index (paramIndex)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setMouseDragSensitivity (220);
    const auto& d = paramDef (paramIndex);
    slider.getProperties().set ("bipolar", d.minV < 0.0f && d.maxV > 0.0f && d.unit == Unit::Db && std::abs (d.minV + d.maxV) < 1.0e-3f);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), paramID (paramIndex), slider);
    slider.setDoubleClickReturnValue (true, d.defV);
    slider.setTooltip (d.name);
    slider.onValueChange = [this] { repaint(); };
    slider.setPopupDisplayEnabled (false, false, nullptr);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (13);
    r.removeFromBottom (13);
    const int s = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (s, s));
}

void Knob::paint (juce::Graphics& g)
{
    const bool on = isEnabled();
    g.setFont (NorthlineLookAndFeel::font (11.5f, true));
    g.setColour (on ? colours::dim : colours::faint);
    g.drawFittedText (label.toUpperCase(), getLocalBounds().removeFromTop (13), juce::Justification::centred, 1, 0.7f);
    g.setFont (NorthlineLookAndFeel::font (11.5f));
    g.setColour (on ? colours::text : colours::faint);
    g.drawFittedText (slider.getTextFromValue (slider.getValue()), getLocalBounds().removeFromBottom (13), juce::Justification::centred, 1, 0.7f);
}

void Knob::setFocusState (bool highlighted, bool dimmed)
{
    slider.getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    slider.repaint();
}

//==============================================================================
Chip::Chip (NorthlineProcessor& p, int paramIndex, const juce::String& text, juce::Colour accent)
    : juce::TextButton (text), index (paramIndex)
{
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, accent);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), paramID (paramIndex), *this);
    setTooltip (paramDef (paramIndex).name);
}

void Chip::setFocusState (bool highlighted, bool dimmed)
{
    getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    repaint();
}

//==============================================================================
ChoiceBox::ChoiceBox (NorthlineProcessor& p, int paramIndex) : index (paramIndex)
{
    const auto& d = paramDef (paramIndex);
    int id = 1;
    for (auto* c : d.choices) addItem (c, id++);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.getState(), paramID (paramIndex), *this);
    setTooltip (d.name);
}

void ChoiceBox::setFocusState (bool highlighted, bool dimmed)
{
    getProperties().set ("focus", highlighted);
    setAlpha (dimmed ? 0.4f : 1.0f);
    repaint();
}

//==============================================================================
Segmented::Segmented (NorthlineProcessor& p, int paramIndex, juce::Colour accent) : index (paramIndex)
{
    const auto& d = paramDef (paramIndex);
    for (int i = 0; i < (int) d.choices.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (d.choices[(size_t) i]));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
        b->setTooltip (juce::String (d.name) + ": " + d.choices[(size_t) i]);
        addAndMakeVisible (b);
    }
    attachment = std::make_unique<juce::ParameterAttachment> (*p.param (paramIndex), [this] (float v) { select (juce::roundToInt (v)); });
    attachment->sendInitialUpdate();
}

void Segmented::select (int i)
{
    selected = i;
    for (int k = 0; k < buttons.size(); ++k) buttons[k]->setToggleState (k == i, juce::dontSendNotification);
    if (onChange) onChange();
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

//==============================================================================
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
    g.setColour (colours::text.withAlpha (0.85f));
    g.setFont (NorthlineLookAndFeel::font (10.5f));
    g.drawText (value < -0.05f ? juce::String (value, 1) : juce::String ("0.0"), getLocalBounds().reduced (3, 0), juce::Justification::centredLeft);
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

    auto toY = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
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

    g.setFont (NorthlineLookAndFeel::font (9.5f));
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
            g.drawText (juce::String ((int) m), juce::Rectangle<float> (24, 12).withCentre (t), juce::Justification::centred);
        }
    }
    g.setColour (juce::Colour (0xff2b2b2b));
    g.setFont (NorthlineLookAndFeel::font (11.0f, true));
    g.drawText (label, r.removeFromBottom (16.0f), juce::Justification::centred);

    const float a = angleFor (value);
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawLine ({ pivot.getPointOnCircumference (len * 0.25f, a), pivot.getPointOnCircumference (len * 0.97f, a) }, 1.6f);
}

void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour accent)
{
    g.setColour (accent.withAlpha (0.9f));
    g.setFont (NorthlineLookAndFeel::font (11.0f, true));
    g.drawText (text, r, juce::Justification::centredLeft);
    const int tw = juce::GlyphArrangement::getStringWidthInt (NorthlineLookAndFeel::font (11.0f, true), text);
    g.setColour (colours::edge);
    g.drawHorizontalLine (r.getCentreY(), (float) (r.getX() + tw + 6), (float) r.getRight());
}
} // namespace nl

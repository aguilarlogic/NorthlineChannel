#pragma once

#include "../PluginProcessor.h"
#include "LookAndFeel.h"

namespace nl
{
// Controls bound to a parameter implement this so FOCUS mode can highlight them.
struct FocusTarget
{
    virtual ~FocusTarget() = default;
    virtual int focusParam() const = 0;
    virtual void setFocusState (bool highlighted, bool dimmed) = 0;
};

//==============================================================================
class Knob : public juce::Component, public FocusTarget
{
public:
    Knob (NorthlineProcessor& p, int paramIndex, const juce::String& label, juce::Colour accent);

    void resized() override;
    void paint (juce::Graphics&) override;
    int focusParam() const override { return index; }
    void setFocusState (bool highlighted, bool dimmed) override;

    juce::Slider slider;

private:
    juce::String label;
    int index;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class Chip : public juce::TextButton, public FocusTarget
{
public:
    Chip (NorthlineProcessor& p, int paramIndex, const juce::String& text, juce::Colour accent);
    int focusParam() const override { return index; }
    void setFocusState (bool highlighted, bool dimmed) override;

private:
    int index;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
class ChoiceBox : public juce::ComboBox, public FocusTarget
{
public:
    ChoiceBox (NorthlineProcessor& p, int paramIndex);
    int focusParam() const override { return index; }
    void setFocusState (bool highlighted, bool dimmed) override;

private:
    int index;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==============================================================================
class Segmented : public juce::Component, public FocusTarget
{
public:
    Segmented (NorthlineProcessor& p, int paramIndex, juce::Colour accent);
    void resized() override;
    int focusParam() const override { return index; }
    void setFocusState (bool highlighted, bool dimmed) override;
    int getSelected() const { return selected; }
    std::function<void()> onChange;

private:
    void select (int i);
    int index, selected = 0;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
// Gain-reduction bar, 0 to -maxDb, drawn right-to-left.
class GrBar : public juce::Component
{
public:
    explicit GrBar (juce::Colour c, float maxDb = 24.0f) : colour (c), range (maxDb) {}
    void setGr (float db);
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
    float range, value = 0.0f;
};

//==============================================================================
class StereoMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLevels (float peakL, float peakR, float rmsL, float rmsR);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clip = false; holdDb[0] = holdDb[1] = -100.0f; repaint(); }

private:
    float peakDb[2] { -100.0f, -100.0f }, rmsDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f };
    int holdFrames[2] {};
    bool clip = false;
};

//==============================================================================
class VuMeter : public juce::Component
{
public:
    void setValue (float vu) { if (std::abs (vu - value) > 0.01f) { value = vu; repaint(); } }
    void setLabel (const juce::String& s) { if (s != label) { label = s; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    float value = -20.0f;
    juce::String label { "VU" };
};

void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour accent);
} // namespace nl

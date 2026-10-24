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
// Pure helpers for the interaction standard (roadmap 8.3). Tested in TestUiShell.cpp.
namespace ui
{
// Text entry: "3k", "2.5k", "-18", "-18 dB", "inf", "100ms", "1.5s", "50%", "3.0:1", "+2" and
// "−18" (Unicode minus). Returns false for text it cannot read. The result is clamped to range.
bool parseValueText (const juce::String& text, Unit unit, float minV, float maxV, float& plainOut);

// Parses text for any parameter kind: float (parseValueText), on/off, choice name or index.
bool parseParamText (int paramIndex, const juce::String& text, float& plainOut);

// Normalised change for a drag of (dx, dy) pixels (dy positive = up). 200 px span the range;
// fine (Shift) is a tenth of that.
float dragDelta (float dxPx, float dyPx, bool fine);

// Normalised change for one wheel event. Shift is fine.
float wheelDelta (float wheelY, bool fine);

// Normalised change for one key press: arrows 1 %, Shift 0.1 %, Page keys 10 %, Shift+Page 1 %.
float keyStep (bool up, bool shift, bool coarse);

// Alt or Ctrl/Cmd click resets to the default value.
bool isResetClick (const juce::ModifierKeys& mods);

float clampNormalised (float n);

// Descriptive tooltip: what the control does, its range and the gestures.
juce::String tooltipText (int paramIndex);
// Just the description used by help mode and the tooltip.
juce::String helpText (int paramIndex);
// Accessibility description: the help text, or the parameter name when there is none.
juce::String shortDescription (int paramIndex);

// Help mode ("?" in the chain row): hovering a control shows its explanation.
void setHelpMode (bool on);
bool helpMode();

// Scale of the editor (set by the editor) so value bubbles stay legible.
void setDisplayScale (float s);
float displayScale();

// A small value bubble drawn above `owner` in its top-level component. Only `owner` may hide it.
void showBubble (juce::Component& owner, const juce::String& text);
void hideBubble (juce::Component& owner);
} // namespace ui

//==============================================================================
// Binds a control to one parameter. Uses ParameterAttachment, so host automation, undo and
// gesture begin/end all follow the processor's rules. Wheel steps made in quick succession share
// one gesture.
class ParamBinding : private juce::Timer
{
public:
    ParamBinding (NorthlineProcessor& p, int paramIndex, std::function<void()> onChanged);
    ~ParamBinding() override;

    int index() const noexcept { return paramIdx; }
    NorthlineProcessor& processor() const noexcept { return proc; }
    juce::RangedAudioParameter& param() const noexcept { return *prm; }

    float plain() const;
    float normalised() const;
    float defaultPlain() const;
    void sendInitialUpdate();

    // Inside an open gesture.
    void setNormalised (float n);

    void beginGesture();
    void endGesture();

    // Each of these is one complete gesture (one undo step).
    void setPlainAsGesture (float plain);
    void setNormalisedAsGesture (float n);
    void resetToDefault();

    // Wheel steps: consecutive steps share one gesture, which ends 300 ms after the last.
    void wheelStep (float normalisedDelta);

private:
    void timerCallback() override;

    NorthlineProcessor& proc;
    const int paramIdx;
    juce::RangedAudioParameter* prm;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::function<void()> changed;
    bool wheelOpen = false;
};

// Right-click menu shared by Knob, Chip, ChoiceBox and Segmented: reset, enter value, copy and
// paste value, Spotlight. `enterValue` is what "Enter value..." does for that control.
void showParamMenu (juce::Component& owner, ParamBinding& binding, std::function<void()> enterValue);

//==============================================================================
class Knob : public juce::Component, public FocusTarget, public juce::SettableTooltipClient
{
public:
    Knob (NorthlineProcessor& p, int paramIndex, const juce::String& label, juce::Colour accent);
    ~Knob() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    int focusParam() const override { return binding.index(); }
    void setFocusState (bool highlighted, bool dimmed) override;
    // An inert knob takes no clicks, drags, wheel or keys, and draws dimmed; it keeps its tooltip, which says why
    // (the EQ Q knob for band types that ignore Q). Unlike setEnabled, hovering still works.
    void setInert (bool shouldBeInert);
    bool isInert() const noexcept { return inert; }

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    ParamBinding& getBinding() noexcept { return binding; }
    void beginTextEntry();

private:
    void updateBubble();
    void closeTextEntry();
    bool commitTextEntry();

    juce::String label;
    juce::Colour accent;
    ParamBinding binding;
    bool dragging = false, hovered = false, bubbleShown = false;
    bool inert = false;
    juce::Point<float> lastPos;
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
class Chip : public juce::TextButton, public FocusTarget
{
public:
    Chip (NorthlineProcessor& p, int paramIndex, const juce::String& text, juce::Colour accent);
    ~Chip() override;

    int focusParam() const override { return binding.index(); }
    void setFocusState (bool highlighted, bool dimmed) override;
    void mouseDown (const juce::MouseEvent&) override;

    ParamBinding& getBinding() noexcept { return binding; }

private:
    ParamBinding binding;
};

//==============================================================================
class ChoiceBox : public juce::ComboBox, public FocusTarget
{
public:
    ChoiceBox (NorthlineProcessor& p, int paramIndex);
    ~ChoiceBox() override;

    int focusParam() const override { return binding.index(); }
    void setFocusState (bool highlighted, bool dimmed) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    ParamBinding& getBinding() noexcept { return binding; }
    // Amber outline while a transient choice is away from its default (see Params.h, kTransient).
    void updateTransientLook();

private:
    void stepBy (int delta, bool fine);
    ParamBinding binding;
};

//==============================================================================
class SegButton;

class Segmented : public juce::Component, public FocusTarget, public juce::SettableTooltipClient
{
public:
    Segmented (NorthlineProcessor& p, int paramIndex, juce::Colour accent);
    ~Segmented() override;

    void resized() override;
    int focusParam() const override { return binding.index(); }
    void setFocusState (bool highlighted, bool dimmed) override;
    int getSelected() const { return selected; }
    std::function<void()> onChange;

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    ParamBinding& getBinding() noexcept { return binding; }
    void showMenuForButton (juce::Component& button);

private:
    void select (int i);
    void choose (int i);
    void stepBy (int delta);
    int index, selected = 0;
    juce::OwnedArray<SegButton> buttons;
    ParamBinding binding;
};

// Gain-reduction bar, 0 to -maxDb, drawn right-to-left, with ticks at 1/3/6/10/20 dB.
class GrBar : public juce::Component
{
public:
    explicit GrBar (juce::Colour c, float maxDb = 24.0f);
    void setGr (float db);
    void setShowValue (bool on) { showValue = on; repaint(); }
    float getGr() const noexcept { return value; }
    void paint (juce::Graphics&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Colour colour;
    float range, value = 0.0f;
    bool showValue = true;
};

//==============================================================================
// Stereo peak / RMS meter with peak hold and clip latch. Click to clear.
class StereoMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float kScaleGutter = 22.0f; // width of the dB numbers, at the left
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

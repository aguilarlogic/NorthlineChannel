#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nl
{
namespace colours
{
const juce::Colour background { 0xff111317 };
const juce::Colour panel { 0xff1a1d23 };
const juce::Colour panelHeader { 0xff20242b };
const juce::Colour edge { 0xff2b3039 };
const juce::Colour track { 0xff2c313b };
const juce::Colour text { 0xffdde1e7 };
const juce::Colour dim { 0xff8a93a1 };
const juce::Colour faint { 0xff5a6270 };
const juce::Colour focus { 0xfff5d76e };
const juce::Colour pre { 0xffe8a33d };
const juce::Colour gate { 0xff3fc1a5 };
const juce::Colour ds { 0xffb07cf0 };
const juce::Colour eq { 0xff4fa3f7 };
const juce::Colour comp { 0xfff06a4f };
const juce::Colour master { 0xffc9d1dc };
const juce::Colour meterGreen { 0xff52d273 };
const juce::Colour meterYellow { 0xffe9d35b };
const juce::Colour meterRed { 0xffef5350 };

juce::Colour forModule (int moduleId);
} // namespace colours

class NorthlineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NorthlineLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;

    static juce::Font font (float height, bool bold = false);
};
} // namespace nl

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Tokens.h"

namespace nl
{
// Colours follow the active theme (applyTheme). The names are stable and the panels use them
// directly. Values are set from tokens::palette, so a theme switch needs applyTheme plus a rebuild
// of anything that cached a colour (the editor does that).
namespace colours
{
extern juce::Colour background, panel, panelHeader, edge, track, arcTrack, controlEdge;
extern juce::Colour text, dim, faint, focus, amber, red;
extern juce::Colour meterGreen, meterYellow, meterRed;
extern juce::Colour pre, gate, ds, eq, comp, master;

juce::Colour forModule (int moduleId);
} // namespace colours

void applyTheme (tokens::Theme theme);
tokens::Theme activeTheme() noexcept;

// Tabular figures (roadmap 8.4). A live readout lays its digits out in cells of one width, so the
// text does not jitter as the value moves. Works with any font: each digit takes the widest digit
// advance, and the other characters keep their own width.
namespace figures
{
struct Layout
{
    std::vector<float> x; // left edge of each character, relative to the start of the text
    float width = 0.0f;   // whole text width
    float digitCell = 0.0f;
};
Layout layout (const juce::Font& f, const juce::String& text);

// Draws the text inside `area`, aligned by `justification` (left, centred or right).
void draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification, const juce::Font& f);
} // namespace figures

class NorthlineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NorthlineLookAndFeel();

    // Re-reads the colour IDs after applyTheme.
    void refreshColours();

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
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName, int columnId,
                                int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) override;

    // The knob graphic shared by Knob and drawRotarySlider. pos is normalised 0..1.
    static void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float pos, juce::Colour accent, bool bipolar, bool enabled, bool focused);

    // The interface font: Inter when it is embedded in the build (see the font notes in LookAndFeel.cpp),
    // otherwise the platform default sans-serif.
    static juce::Font font (float height, bool bold = false);
    static bool interFontActive() noexcept;
};
} // namespace nl

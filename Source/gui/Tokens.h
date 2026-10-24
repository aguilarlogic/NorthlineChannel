#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Design tokens (roadmap 8.4): colour, type size, spacing, radius and motion. Every colour the
// UI uses comes from a Palette, so a theme is one table. nl::colours (LookAndFeel.h) mirrors the
// active palette under the names the panels already use.
//
// Contrast rules, checked by the ui_tokens_* tests for both themes:
//   text tokens (text, dim, faint) reach 4.5:1 on background, panel, panelHeader and track;
//   accent, focus, amber and red text reaches 4.5:1 on background, panel and panelHeader;
//   UI tokens (controlEdge, arcTrack, focus, amber, red, accents, meters) reach 3:1 on panel.
// Module accents must stay distinct under protanopia and deuteranopia simulation (CIE76 dE >= 8).
// Red is only for clip, overs and errors. Amber is only for transient states (listen, monitoring,
// learning).

namespace nl::tokens
{
enum class Theme : int { dark = 0, highContrast = 1 };

struct Palette
{
    juce::Colour background, panel, panelHeader, edge, track, arcTrack, controlEdge;
    juce::Colour text, dim, faint, focus, amber, red;
    juce::Colour meterGreen, meterYellow, meterRed;
    juce::Colour pre, gate, ds, eq, comp, master;
};

inline Palette darkPalette()
{
    Palette p;
    p.background = juce::Colour (0xff111317);
    p.panel = juce::Colour (0xff1a1d23);
    p.panelHeader = juce::Colour (0xff20242b);
    p.edge = juce::Colour (0xff2b3039);
    p.track = juce::Colour (0xff3a404c);
    p.arcTrack = juce::Colour (0xff687284);
    p.controlEdge = juce::Colour (0xff6e7889);
    p.text = juce::Colour (0xffe4e8ee);
    p.dim = juce::Colour (0xffb8c0cc);
    p.faint = juce::Colour (0xffa6aebb);
    p.focus = juce::Colour (0xfff5d76e);
    p.amber = juce::Colour (0xfff2b13d);
    p.red = juce::Colour (0xffff7f78);
    p.meterGreen = juce::Colour (0xff52d273);
    p.meterYellow = juce::Colour (0xffe9d35b);
    p.meterRed = juce::Colour (0xffef5350);
    p.pre = juce::Colour (0xfff08cc8);
    p.gate = juce::Colour (0xff2fc2a0);
    p.ds = juce::Colour (0xffe083f0);
    p.eq = juce::Colour (0xff70b4ff);
    p.comp = juce::Colour (0xffa6dc66);
    p.master = juce::Colour (0xffc9d1dc);
    return p;
}

inline Palette highContrastPalette()
{
    Palette p;
    p.background = juce::Colour (0xff000000);
    p.panel = juce::Colour (0xff0b0b0b);
    p.panelHeader = juce::Colour (0xff141414);
    p.edge = juce::Colour (0xff6b6b6b);
    p.track = juce::Colour (0xff262626);
    p.arcTrack = juce::Colour (0xff8a8a8a);
    p.controlEdge = juce::Colour (0xffa8a8a8);
    p.text = juce::Colour (0xffffffff);
    p.dim = juce::Colour (0xffe8e8e8);
    p.faint = juce::Colour (0xffd2d2d2);
    p.focus = juce::Colour (0xffffe600);
    p.amber = juce::Colour (0xffffc233);
    p.red = juce::Colour (0xffff5c5c);
    p.meterGreen = juce::Colour (0xff3dff7a);
    p.meterYellow = juce::Colour (0xffffff55);
    p.meterRed = juce::Colour (0xffff5c5c);
    p.pre = juce::Colour (0xffffa8d8);
    p.gate = juce::Colour (0xff40ffd6);
    p.ds = juce::Colour (0xffd88cff);
    p.eq = juce::Colour (0xff7fd0ff);
    p.comp = juce::Colour (0xffc2f57a);
    p.master = juce::Colour (0xffffffff);
    return p;
}

inline const Palette& palette (Theme t)
{
    static const Palette dark = darkPalette();
    static const Palette high = highContrastPalette();
    return t == Theme::highContrast ? high : dark;
}

// Sizes in px at 100 %. Labels are at least 11 px, values at least 12 px.
namespace type
{
constexpr float label = 11.0f;
constexpr float value = 12.0f;
constexpr float title = 13.0f;
constexpr float brand = 18.0f;
}

namespace space
{
constexpr int xs = 2, sm = 4, md = 6, lg = 8, xl = 12;
}

namespace radius
{
constexpr float panel = 6.0f, control = 4.0f;
}

namespace motion
{
constexpr int reorderMs = 150; // chain card reorder
constexpr int bubbleHoldMs = 1200;
}

//==============================================================================
// WCAG 2.x contrast.
inline double relativeLuminance (juce::Colour c)
{
    auto lin = [] (double v) { return v <= 0.03928 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin (c.getFloatRed()) + 0.7152 * lin (c.getFloatGreen()) + 0.0722 * lin (c.getFloatBlue());
}

inline double contrastRatio (juce::Colour a, juce::Colour b)
{
    const double la = relativeLuminance (a), lb = relativeLuminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

struct ContrastPair
{
    const char* name;
    juce::Colour fg, bg;
    bool isText; // true: 4.5:1 required, false: 3:1 (UI element)
};

// Every text and UI token against the backgrounds it is drawn on.
inline std::vector<ContrastPair> contrastPairs (Theme t)
{
    const auto& p = palette (t);
    struct Named { const char* name; juce::Colour colour; };
    const Named backgrounds[] { { "background", p.background }, { "panel", p.panel }, { "panelHeader", p.panelHeader } };
    const Named texts[] { { "text", p.text }, { "dim", p.dim }, { "faint", p.faint } };
    const Named accents[] { { "pre", p.pre }, { "gate", p.gate }, { "ds", p.ds }, { "eq", p.eq }, { "comp", p.comp },
                            { "master", p.master }, { "focus", p.focus }, { "amber", p.amber }, { "red", p.red } };
    const Named uiTokens[] { { "controlEdge", p.controlEdge }, { "arcTrack", p.arcTrack }, { "meterGreen", p.meterGreen },
                             { "meterYellow", p.meterYellow }, { "meterRed", p.meterRed } };

    std::vector<ContrastPair> out;
    for (auto& t_ : texts)
    {
        for (auto& bg : backgrounds) out.push_back ({ t_.name, t_.colour, bg.colour, true });
        out.push_back ({ t_.name, t_.colour, p.track, true }); // text on control fills
    }
    for (auto& a : accents)
        for (auto& bg : backgrounds) out.push_back ({ a.name, a.colour, bg.colour, true });
    for (auto& u : uiTokens) out.push_back ({ u.name, u.colour, p.panel, false });
    for (auto& a : accents) out.push_back ({ a.name, a.colour, p.panel, false });
    return out;
}
} // namespace nl::tokens

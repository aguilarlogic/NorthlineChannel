// UI shell (roadmap 8.2, 8.3, 8.4, 8.7): tokens and contrast, value parsing and gesture mapping,
// gestures and undo of the controls, chain order, settings, layout geometry, overlays, accessibility
// and editor construction time. Run with `NorthlineTool test ui_`.
#include "TestUtil.h"
#include "gui/ChainView.h"
#include "gui/PluginEditor.h"
#include "gui/Scopes.h"
#include "gui/Widgets.h"
#include "gui/OverlayCard.h"
#include "gui/TopBar.h"
#include "gui/HubPanel.h"
#include "gui/PresetBrowser.h"
#include "gui/SmartStartPanel.h"
#include "gui/SettingsOverlay.h"
#include "gui/Onboarding.h"
#include <algorithm>
#include <cmath>

using namespace nl;
using tool::check;

namespace
{
// Settings written by a test go to a throwaway file, never to the user's profile.
void useTempSettings (const char* name)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NorthlineUiTests");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::String (name) + ".settings");
    file.deleteFile();
    UiSettings::get().useFile (file);
}

// Counts begin and end gestures reported by the host-facing parameter.
struct GestureCounter : public juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { starting ? ++begins : ++ends; }
};

// Simple cosine-free colour-blindness simulation (Machado et al. 2009, severity 1.0), applied in
// linear RGB. Used to check that the module accents stay apart.
juce::Colour simulate (juce::Colour c, const double m[3][3])
{
    auto lin = [] (double v) { return v <= 0.04045 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
    auto gam = [] (double v) { v = juce::jlimit (0.0, 1.0, v); return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow (v, 1.0 / 2.4) - 0.055; };
    const double r = lin (c.getFloatRed()), g = lin (c.getFloatGreen()), b = lin (c.getFloatBlue());
    return juce::Colour::fromFloatRGBA ((float) gam (m[0][0] * r + m[0][1] * g + m[0][2] * b),
                                        (float) gam (m[1][0] * r + m[1][1] * g + m[1][2] * b),
                                        (float) gam (m[2][0] * r + m[2][1] * g + m[2][2] * b), 1.0f);
}

// CIE76 colour difference.
double deltaE (juce::Colour a, juce::Colour b)
{
    auto toLab = [] (juce::Colour c)
    {
        auto lin = [] (double v) { return v <= 0.04045 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
        const double r = lin (c.getFloatRed()), g = lin (c.getFloatGreen()), bl = lin (c.getFloatBlue());
        const double x = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.95047;
        const double y = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
        const double z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.08883;
        auto f = [] (double t) { return t > 0.008856 ? std::cbrt (t) : 7.787 * t + 16.0 / 116.0; };
        const double fx = f (x), fy = f (y), fz = f (z);
        return std::array<double, 3> { 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz) };
    };
    const auto A = toLab (a), B = toLab (b);
    return std::sqrt ((A[0] - B[0]) * (A[0] - B[0]) + (A[1] - B[1]) * (A[1] - B[1]) + (A[2] - B[2]) * (A[2] - B[2]));
}

const double kProtan[3][3] { { 0.152286, 1.052583, -0.204868 }, { 0.114503, 0.786281, 0.099216 }, { -0.003882, -0.048116, 1.051998 } };
const double kDeutan[3][3] { { 0.367322, 0.860646, -0.227968 }, { 0.280085, 0.672501, 0.047413 }, { -0.011820, 0.042940, 0.968881 } };

juce::Colour accentOf (int m)
{
    return colours::forModule (m);
}
} // namespace

//==============================================================================
NL_TEST (ui_tokens_contrast)
{
    for (auto theme : { tokens::Theme::dark, tokens::Theme::highContrast })
    {
        const char* name = theme == tokens::Theme::dark ? "dark" : "high contrast";
        int textFails = 0, uiFails = 0, pairs = 0;
        double worstText = 99.0, worstUi = 99.0;
        juce::String worstName;
        for (auto& pair : tokens::contrastPairs (theme))
        {
            ++pairs;
            const double r = tokens::contrastRatio (pair.fg, pair.bg);
            const double need = pair.isText ? 4.5 : 3.0;
            if (pair.isText && r < worstText) { worstText = r; worstName = pair.name; }
            if (! pair.isText && r < worstUi) worstUi = r;
            if (r < need) { if (pair.isText) ++textFails; else ++uiFails; }
        }
        check (textFails == 0, juce::String ("text tokens reach 4.5:1 (") + name + ")",
               "worst " + juce::String (worstText, 2) + " on " + worstName + ", " + juce::String (pairs) + " pairs");
        check (uiFails == 0, juce::String ("UI tokens reach 3:1 (") + name + ")", "worst " + juce::String (worstUi, 2));
    }
}

NL_TEST (ui_tokens_module_distinct)
{
    const int mods[] { modPre, modGate, modDs, modEq, modComp };
    struct Case { const char* name; const double (*matrix)[3]; double need; };
    const Case cases[] { { "normal", nullptr, 15.0 }, { "protanopia", kProtan, 8.0 }, { "deuteranopia", kDeutan, 8.0 } };
    for (auto theme : { tokens::Theme::dark, tokens::Theme::highContrast })
    {
        applyTheme (theme);
        for (auto& c : cases)
        {
            double worst = 1.0e9;
            for (int i = 0; i < 5; ++i)
                for (int j = i + 1; j < 5; ++j)
                {
                    auto a = accentOf (mods[i]), b = accentOf (mods[j]);
                    if (c.matrix != nullptr) { a = simulate (a, c.matrix); b = simulate (b, c.matrix); }
                    worst = std::min (worst, deltaE (a, b));
                }
            check (worst >= c.need, juce::String ("module accents apart under ") + c.name + (theme == tokens::Theme::dark ? " (dark)" : " (high contrast)"),
                   "min dE " + juce::String (worst, 1));
        }
    }
    applyTheme (tokens::Theme::dark);
}

NL_TEST (ui_parse_values)
{
    float v = 0.0f;
    auto ok = [&] (const char* text, Unit u, float lo, float hi, float expected)
    {
        const bool parsed = ui::parseValueText (text, u, lo, hi, v);
        return parsed && std::abs (v - expected) < 1.0e-3f;
    };
    check (ok ("3k", Unit::Hz, 20, 20000, 3000.0f), "parses 3k as 3000 Hz");
    check (ok ("2.5k", Unit::Hz, 20, 20000, 2500.0f), "parses 2.5k");
    check (ok ("-18", Unit::Db, -48, 0, -18.0f), "parses -18 dB");
    check (ok ("-18 dB", Unit::Db, -48, 0, -18.0f), "parses '-18 dB'");
    const bool minusOk = ui::parseValueText (juce::String::fromUTF8 ("\xe2\x88\x92" "18"), Unit::Db, -48, 0, v);
    check (minusOk && std::abs (v - (-18.0f)) < 1.0e-3f, "parses the Unicode minus sign", juce::String (v) + (minusOk ? " ok" : " rejected"));
    check (ok ("inf", Unit::Ratio, 1, kRatioInf, kRatioInf), "inf is the ratio ceiling");
    check (ok ("inf:1", Unit::Ratio, 1, kRatioInf, kRatioInf), "inf : 1 (as displayed) parses");
    check (ok ("100ms", Unit::Ms, 10, 1000, 100.0f), "parses 100ms");
    check (ok ("1.5s", Unit::Ms, 10, 5000, 1500.0f), "parses 1.5s as 1500 ms");
    check (ok ("50%", Unit::Percent, 0, 100, 50.0f), "parses 50%");
    check (ok ("3.0 : 1", Unit::Ratio, 1, kRatioInf, 3.0f), "parses a displayed ratio");
    check (ok ("  +2 ", Unit::Db, -24, 24, 2.0f), "trims spaces and accepts a plus sign");
    check (ok ("1e3", Unit::Hz, 20, 20000, 1000.0f), "accepts exponent notation");
    check (ok ("100000", Unit::Hz, 20, 20000, 20000.0f), "clamps to the range");
    check (! ui::parseValueText ("", Unit::Db, -48, 0, v), "rejects empty text");
    check (! ui::parseValueText ("abc", Unit::Db, -48, 0, v), "rejects text it cannot read");
    check (! ui::parseValueText ("k", Unit::Hz, 20, 20000, v), "rejects a bare suffix");
}

NL_TEST (ui_parse_roundtrip)
{
    // formatValue output must read back to the same value (the copy and paste contract).
    auto p = tool::makeProc();
    const int ids[] { P::hpFreq, P::lpFreq, P::compThresh, P::compRatio, P::compAttack, P::gateHold, P::satDrive, P::eqMidGain, P::eqMidQ, P::limCeiling };
    int bad = 0;
    for (int id : ids)
    {
        const auto& d = paramDef (id);
        for (float plain : { d.defV, d.minV + 0.37f * (d.maxV - d.minV) })
        {
            const auto shown = formatValue (id, plain);
            float back = 0.0f;
            if (! ui::parseParamText (id, shown, back)) { ++bad; continue; }
            const float tol = std::max (0.02f * std::abs (plain), 0.5f); // display precision
            if (std::abs (back - plain) > tol) ++bad;
        }
    }
    check (bad == 0, "formatValue output parses back to its value", juce::String (bad) + " mismatches");

    float v = 0.0f;
    check (ui::parseParamText (P::preOn, "On", v) && v == 1.0f, "boolean parameters accept On");
    check (ui::parseParamText (P::satType, "Tape", v) && v == 4.0f, "choice parameters accept their names");
    check (ui::parseParamText (P::satType, "3", v) && v == 3.0f, "choice parameters accept an index");
    check (! ui::parseParamText (P::satType, "Nope", v), "unknown choice names are rejected");
}

NL_TEST (ui_gesture_mapping)
{
    check (std::abs (ui::dragDelta (0.0f, 100.0f, false) - 0.5f) < 1.0e-5f, "drag up 100 px moves half the range");
    check (std::abs (ui::dragDelta (100.0f, 0.0f, false) - 0.5f) < 1.0e-5f, "drag right 100 px moves half the range");
    check (std::abs (ui::dragDelta (0.0f, 100.0f, true) - 0.05f) < 1.0e-5f, "Shift drag is a tenth as fine");
    check (ui::dragDelta (0.0f, -50.0f, false) < 0.0f, "drag down decreases the value");
    check (ui::wheelDelta (1.0f, false) > 0.0f && ui::wheelDelta (-1.0f, false) < 0.0f, "wheel up increases, down decreases");
    check (std::abs (ui::wheelDelta (1.0f, true) * 10.0f - ui::wheelDelta (1.0f, false)) < 1.0e-6f, "Shift wheel is a tenth");
    check (std::abs (ui::keyStep (true, false, false) - 0.01f) < 1.0e-6f, "arrow step is 1 %");
    check (std::abs (ui::keyStep (true, true, false) - 0.001f) < 1.0e-6f, "Shift arrow is 0.1 %");
    check (std::abs (ui::keyStep (false, false, true) + 0.10f) < 1.0e-6f, "Page Down is a 10 % step down");
    check (std::abs (ui::keyStep (true, true, true) - 0.01f) < 1.0e-6f, "Shift Page is 1 %");
    check (ui::clampNormalised (1.4f) == 1.0f && ui::clampNormalised (-0.2f) == 0.0f, "normalised values are clamped");

    juce::ModifierKeys alt (juce::ModifierKeys::altModifier), ctrl (juce::ModifierKeys::ctrlModifier), none;
    check (ui::isResetClick (alt) && ui::isResetClick (ctrl) && ! ui::isResetClick (none), "Alt and Ctrl clicks reset");
}

NL_TEST (ui_tooltip_text)
{
    const auto tip = ui::tooltipText (P::satDrive);
    check (tip.containsIgnoreCase ("Range") && tip.containsIgnoreCase ("Shift") && tip.containsIgnoreCase ("Alt-click"),
           "tooltips give the range and the gestures", tip);
    check (ui::tooltipText (P::satType).containsIgnoreCase ("Options:"), "choice tooltips list the options");
    check (ui::tooltipText (P::preOn).containsIgnoreCase ("toggle"), "toggle tooltips say how to toggle");
}

NL_TEST (ui_param_gestures)
{
    auto p = tool::makeProc();
    ParamBinding binding (*p, P::hpFreq, {});
    GestureCounter counter;
    p->param (P::hpFreq)->addListener (&counter);

    binding.setPlainAsGesture (1000.0f);
    check (counter.begins == 1 && counter.ends == 1, "a complete set is one begin and one end gesture",
           juce::String (counter.begins) + "/" + juce::String (counter.ends));
    check (std::abs (binding.plain() - 1000.0f) < 1.0f, "the value is written", juce::String (binding.plain()));

    binding.resetToDefault();
    check (std::abs (binding.plain() - paramDef (P::hpFreq).defV) < 0.5f, "reset returns the default value",
           juce::String (binding.plain()));
    check (counter.begins == 2 && counter.ends == 2, "reset is its own gesture");

    binding.wheelStep (0.01f);
    binding.wheelStep (0.01f);
    binding.wheelStep (0.01f);
    check (counter.begins == 3 && counter.ends == 2, "wheel steps share one open gesture until they stop");
    juce::Thread::sleep (350);
    juce::Timer::callPendingTimersSynchronously();
    check (counter.begins == 3 && counter.ends == 3, "the wheel gesture ends after the quiet period");

    p->param (P::hpFreq)->removeListener (&counter);
}

NL_TEST (ui_knob_mouse)
{
    auto p = tool::makeProc();
    Knob knob (*p, P::hpFreq, "HP Freq", colours::eq);
    knob.setBounds (0, 0, 70, 100);
    GestureCounter counter;
    p->param (P::hpFreq)->addListener (&counter);

    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    auto event = [&] (juce::Point<float> at, juce::ModifierKeys mods, int clicks)
    {
        return juce::MouseEvent (source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &knob, &knob, now, at, now, clicks, false);
    };

    const float before = p->param (P::hpFreq)->getValue();
    knob.mouseDown (event ({ 30.0f, 50.0f }, {}, 1));
    knob.mouseDrag (event ({ 30.0f, 40.0f }, {}, 1));
    knob.mouseDrag (event ({ 30.0f, 30.0f }, {}, 1));
    knob.mouseUp (event ({ 30.0f, 30.0f }, {}, 1));
    const float after = p->param (P::hpFreq)->getValue();
    check (after > before + 0.1f, "dragging up raises the value", juce::String (before) + " to " + juce::String (after));
    check (counter.begins == 1 && counter.ends == 1, "a drag is one gesture");

    knob.mouseDown (event ({ 30.0f, 50.0f }, juce::ModifierKeys (juce::ModifierKeys::altModifier), 1));
    knob.mouseUp (event ({ 30.0f, 50.0f }, juce::ModifierKeys (juce::ModifierKeys::altModifier), 1));
    check (std::abs (p->param (P::hpFreq)->getValue() - p->param (P::hpFreq)->getDefaultValue()) < 1.0e-4f, "Alt-click resets to the default");

    p->param (P::hpFreq)->removeListener (&counter);
}

NL_TEST (ui_controls_accessibility)
{
    auto p = tool::makeProc();
    Chip chip (*p, P::preOn, "PRE", colours::pre);
    ChoiceBox box (*p, P::satType);
    Segmented seg (*p, P::preMode, colours::pre);
    Knob knob (*p, P::satDrive, "Drive", colours::pre);
    for (auto* c : { static_cast<juce::Component*> (&chip), static_cast<juce::Component*> (&box), static_cast<juce::Component*> (&seg), static_cast<juce::Component*> (&knob) })
    {
        check (c->getTitle().isNotEmpty(), "control has a title", c->getTitle());
        check (c->getDescription().isNotEmpty(), "control has a description", c->getTitle());
    }
    check (knob.createAccessibilityHandler() != nullptr, "knob has an accessibility handler");
    check (seg.createAccessibilityHandler() != nullptr, "segmented control has an accessibility handler");
    check (knob.getAccessibilityHandler() == nullptr || knob.getAccessibilityHandler()->getTitle().isNotEmpty(), "knob handler reports its title");
}

NL_TEST (ui_chain_reorder)
{
    using Order = chain::Order;
    const Order base { modPre, modGate, modDs, modEq, modComp };
    const Order movedUp = chain::moveTo (base, modComp, 1);
    check (movedUp == Order { modPre, modComp, modGate, modDs, modEq }, "moveTo inserts the module at the target index");
    check (chain::moveTo (base, modPre, 4) == Order { modGate, modDs, modEq, modComp, modPre }, "moveTo can move to the end");
    check (chain::moveTo (base, modEq, 3) == base, "moving to its own slot keeps the order");
    check (chain::moveTo (base, modGate, -3) == Order { modGate, modPre, modDs, modEq, modComp }, "targets are clamped");

    const float first = 0.0f, pitch = 186.0f;
    check (chain::slotForCentre (10.0f, first, pitch, 5) == 0, "a centre in the first slot maps to 0");
    check (chain::slotForCentre (190.0f, first, pitch, 5) == 1, "a centre in the second slot maps to 1");
    check (chain::slotForCentre (9999.0f, first, pitch, 5) == 4, "a centre past the end is clamped");
    check (chain::slotForCentre (-50.0f, first, pitch, 5) == 0, "a centre before the start is clamped");

    // The processor records the move as one undo step.
    auto p = tool::makeProc();
    const bool undoBefore = p->canUndo();
    p->setOrder (movedUp);
    check (p->getOrder() == movedUp, "setOrder applies the new order");
    check (p->canUndo() && ! undoBefore, "a reorder is recorded for undo");
    p->undo();
    check (p->getOrder() == base, "undo restores the previous order");
}

NL_TEST (ui_settings_roundtrip)
{
    useTempSettings ("roundtrip");
    {
        auto& s = UiSettings::get();
        s.setTheme (tokens::Theme::highContrast);
        s.setConsoleLayout (true);
        s.setShowCpu (true);
        s.setDefaultScale (1.5f);
        s.setOnboardingDone (true);
    }
    UiSettings reopened (UiSettings::get().getFile());
    check (reopened.theme() == tokens::Theme::highContrast, "theme is saved");
    check (reopened.consoleLayout() && reopened.showCpu(), "layout and CPU readout are saved");
    check (std::abs (reopened.defaultScale() - 1.5f) < 1.0e-4f, "default size is saved", juce::String (reopened.defaultScale()));
    check (reopened.onboardingDone(), "first-run flag is saved");

    check (std::abs (UiSettings::clampScale (0.5f) - 0.75f) < 1.0e-6f && std::abs (UiSettings::clampScale (3.0f) - 2.0f) < 1.0e-6f,
           "sizes are clamped to 75 % to 200 %");

    // Restore the defaults for the other tests.
    auto& s = UiSettings::get();
    s.setTheme (tokens::Theme::dark);
    s.setConsoleLayout (false);
    s.setShowCpu (false);
    s.setDefaultScale (1.0f);
    s.setOnboardingDone (false);
    applyTheme (tokens::Theme::dark);
}

NL_TEST (ui_geometry)
{
    const auto g = shell::geometry();
    const auto base = juce::Rectangle<int> (shell::kBaseW, shell::kBaseH);
    check (g.topBar.getHeight() == 44, "top bar is 44 px high", juce::String (g.topBar.getHeight()));
    check (g.chain.getHeight() == 72, "chain strip is 72 px high", juce::String (g.chain.getHeight()));
    check (g.master.getWidth() == 300, "master column is 300 px wide", juce::String (g.master.getWidth()));
    check (g.master.getRight() <= base.getRight() && g.master.getBottom() <= base.getBottom(), "master column stays inside the canvas");
    check (! g.detail.intersects (g.master) && ! g.chain.intersects (g.master) && ! g.topBar.intersects (g.chain), "areas do not overlap");
    check (g.detail.getY() >= g.chain.getBottom() && g.chain.getY() >= g.topBar.getBottom(), "areas stack top to bottom");
    check (g.detail.getWidth() >= 900 && g.detail.getHeight() >= 560, "detail area fits the smallest ModulePanel size",
           juce::String (g.detail.getWidth()) + " x " + juce::String (g.detail.getHeight()));
    check (g.topBar.getWidth() == shell::kBaseW - 2 * shell::kMargin, "top bar spans the width");
}

NL_TEST (ui_overlay_host)
{
    useTempSettings ("overlay");
    applyTheme (tokens::Theme::dark);
    auto p = tool::makeProc();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    auto* ne = dynamic_cast<NorthlineEditor*> (editor.get());
    check (ne != nullptr, "createEditor returns the shell editor");
    if (ne == nullptr) return;

    check (! ne->hasOverlay(), "no overlay at start");
    ne->showOverlay (std::make_unique<SettingsOverlay> (*p, [] (float) {}, [] {}), "Settings", overlay::Size::medium);
    check (ne->hasOverlay(), "showOverlay shows an overlay");
    check (ne->overlaySize() == overlay::Size::medium, "the overlay keeps the size it was shown at");
    ne->closeOverlay();
    check (! ne->hasOverlay(), "closeOverlay removes it at once");

    ne->showOverlay (std::make_unique<SettingsOverlay> (*p, [] (float) {}, [] {}), "Settings", overlay::Size::medium);
    check (ne->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)) && ! ne->hasOverlay(), "Esc closes the overlay");
    check (! ne->keyPressed (juce::KeyPress ('x')) && ! ne->hasOverlay(), "other keys pass through");
}

NL_TEST (ui_editor_settings_apply)
{
    useTempSettings ("apply");
    auto p = tool::makeProc();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    auto* ne = dynamic_cast<NorthlineEditor*> (editor.get());
    if (ne == nullptr) { check (false, "editor created"); return; }

    check (ne->getWidth() == shell::kBaseW && ne->getHeight() == shell::kBaseH, "editor opens at 100 % (1320 x 780)",
           juce::String (ne->getWidth()) + " x " + juce::String (ne->getHeight()));

    UiSettings::get().setConsoleLayout (true);
    check (ne->isConsoleLayout(), "console layout applies from Settings");
    UiSettings::get().setConsoleLayout (false);
    check (! ne->isConsoleLayout(), "chain layout applies from Settings");

    ne->setScale (1.5f, true);
    check (ne->getWidth() == 1980 && ne->getHeight() == 1170, "150 % scales the window", juce::String (ne->getWidth()));
    check (std::abs (p->getUiScale() - 1.5f) < 1.0e-4f, "the instance keeps its size");
    ne->setScale (1.0f, true);

    applyTheme (tokens::Theme::highContrast);
    applyTheme (tokens::Theme::dark);
    ne->selectModule (modEq);
    check (ne->selectedModule() == modEq, "selecting a module shows it in the detail area");
}

NL_TEST (ui_editor_construct_time)
{
    useTempSettings ("timing");
    auto p = tool::makeProc();
    std::vector<double> ms;
    for (int i = 0; i < 5; ++i)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        ms.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
    }
    // The fastest of 5: other processes can only make a run slower, so the minimum is the cost
    // of the code itself and does not fail on a busy machine.
    std::sort (ms.begin(), ms.end());
    check (ms[0] <= 150.0, "editor constructs in 150 ms or less (fastest of 5)",
           juce::String (ms[0], 1) + " ms, median " + juce::String (ms[2], 1) + " ms");
}

//==============================================================================
// Overlay host, tabular figures, scale flag, labels and panel activity (wave 3).

NL_TEST (ui_overlay_card_sizes)
{
    const juce::Rectangle<int> canvas (shell::kBaseW, shell::kBaseH);
    const auto large = overlay::cardBounds (canvas, overlay::Size::large);
    const auto medium = overlay::cardBounds (canvas, overlay::Size::medium);
    const auto compact = overlay::cardBounds (canvas, overlay::Size::compact);
    check (large.getWidth() == 1120 && large.getHeight() == 680, "large card is 1120 x 680 on the 1320 x 780 canvas",
           juce::String (large.getWidth()) + " x " + juce::String (large.getHeight()));
    check (medium.getWidth() == 640 && medium.getHeight() == 520, "medium card is 640 x 520");
    check (compact.getWidth() == 560 && compact.getHeight() == 300, "compact card is 560 x 300");
    check (large.getCentre() == canvas.getCentre() && medium.getCentre() == canvas.getCentre(), "cards are centred on the canvas");

    // A smaller window: each card shrinks to the window less the margin, and stays centred.
    const juce::Rectangle<int> small (800, 500);
    const auto smallLarge = overlay::cardBounds (small, overlay::Size::large);
    check (smallLarge.getWidth() == 800 - 2 * overlay::kMargin && smallLarge.getHeight() == 500 - 2 * overlay::kMargin,
           "a large card shrinks to fit a small window", juce::String (smallLarge.getWidth()) + " x " + juce::String (smallLarge.getHeight()));
    check (small.reduced (overlay::kMargin).contains (smallLarge) && smallLarge.getCentre() == small.getCentre(), "a shrunk card stays inside the margin and centred");
    for (auto s : { overlay::Size::compact, overlay::Size::medium, overlay::Size::large })
    {
        const juce::Rectangle<int> tiny (240, 150);
        check (tiny.contains (overlay::cardBounds (tiny, s)), "a card never leaves a tiny window");
    }

    // The component adds the shadow margin, and the content sits below the title row inside the card.
    const auto comp = overlay::componentBounds (large);
    check (comp.getWidth() == large.getWidth() + 2 * overlay::kShadow && comp.getCentre() == large.getCentre(), "the card component carries its shadow margin");
    const auto body = overlay::bodyBounds (comp);
    check (body.getY() >= comp.getY() + overlay::kShadow + overlay::kTitleH, "overlay content starts below the title row");
    check (comp.reduced (overlay::kShadow).contains (body), "overlay content stays inside the card");
    check (body.getWidth() >= 900 && body.getHeight() >= 560, "the large overlay body has room for the preset browser",
           juce::String (body.getWidth()) + " x " + juce::String (body.getHeight()));
}

NL_TEST (ui_overlay_host_card)
{
    useTempSettings ("overlay_card");
    applyTheme (tokens::Theme::dark);
    auto p = tool::makeProc();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    auto* ne = dynamic_cast<NorthlineEditor*> (editor.get());
    if (ne == nullptr) { check (false, "editor created"); return; }

    // Each overlay opens as a card with its size, and Esc closes it.
    ne->openPresetBrowser();
    check (ne->hasOverlay() && ne->overlaySize() == overlay::Size::large, "the preset browser opens as a large card");
    check (ne->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)) && ! ne->hasOverlay(), "Esc closes the preset browser card");
    ne->openHub();
    check (ne->overlaySize() == overlay::Size::large, "the Instance Hub opens large");
    ne->closeOverlay();
    ne->openSmartStart();
    check (ne->overlaySize() == overlay::Size::medium, "Smart Start opens medium");
    ne->closeOverlay();
    ne->openSettings();
    check (ne->overlaySize() == overlay::Size::medium, "Settings opens medium");
    ne->closeOverlay();
    ne->openOnboarding();
    check (ne->overlaySize() == overlay::Size::compact, "the first-run tour opens as a compact card");
    ne->closeOverlay();
    check (! ne->hasOverlay(), "closeOverlay leaves the editor without an overlay");
}

NL_TEST (ui_figures_tabular)
{
    const auto f = NorthlineLookAndFeel::font (12.0f);
    const auto a = figures::layout (f, "-88.8");
    const auto b = figures::layout (f, "-11.1");
    check (std::abs (a.width - b.width) < 1.0e-3f, "tabular: same-length readouts have the same width",
           juce::String (a.width, 2) + " vs " + juce::String (b.width, 2));
    bool same = a.x.size() == b.x.size();
    for (size_t i = 0; same && i < a.x.size(); ++i) same = std::abs (a.x[i] - b.x[i]) < 1.0e-3f;
    check (same, "tabular: every character starts at the same x in both readouts");

    const auto one = figures::layout (f, "1");
    const auto eight = figures::layout (f, "8");
    check (std::abs (one.width - eight.width) < 1.0e-3f, "tabular: a narrow 1 takes the width of an 8");
    check (figures::layout (f, "11").width == figures::layout (f, "88").width, "tabular: digit pairs match");
    check (figures::layout (f, "1.0").width < figures::layout (f, "10.0").width, "tabular: a shorter readout is narrower");
    check (one.digitCell > 0.0f && one.x.size() == 1 && one.x[0] == 0.0f, "tabular: the layout starts at zero and reports the digit cell");
    check (figures::layout (f, "").width == 0.0f && figures::layout (f, "").x.empty(), "tabular: empty text has no width");
    check (figures::layout (f, "-18.0 dB").x.size() == 8, "tabular: every character gets a cell, spaces and units included");
}

NL_TEST (ui_scale_percent_text)
{
    check (scalePercentText (1.0f) == "100%", "scale text: 100 %");
    check (scalePercentText (0.75f) == "75%", "scale text: 75 %");
    check (scalePercentText (0.9f) == "90%", "scale text: a free size shows its percentage", scalePercentText (0.9f));
    check (scalePercentText (1.333f) == "133%", "scale text: rounds to the whole percent");
}

NL_TEST (ui_scale_flag_roundtrip)
{
    auto p = tool::makeProc();
    check (! p->isUiScaleSet(), "a new instance has no chosen size");

    p->setUiScale (1.5f, false); // the editor applying the default: not the instance's own choice
    check (! p->isUiScaleSet(), "applying a size without a user choice leaves the flag off");

    p->setUiScale (1.0f); // the user picks 100 %
    check (p->isUiScaleSet() && std::abs (p->getUiScale() - 1.0f) < 1.0e-6f, "a chosen 100 % sets the flag");

    juce::MemoryBlock block;
    p->getStateInformation (block);
    auto q = tool::makeProc();
    q->setStateInformation (block.getData(), (int) block.getSize());
    check (q->isUiScaleSet(), "the flag survives a save and reload");
    check (std::abs (q->getUiScale() - 1.0f) < 1.0e-6f, "a stored 100 % is kept on reload");

    // A session saved before the flag existed has no flag, so the global default applies.
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    check (xml != nullptr, "the saved state is XML");
    if (xml == nullptr) return;
    xml->removeAttribute ("uiScaleSet");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    auto r = tool::makeProc();
    r->setStateInformation (old.getData(), (int) old.getSize());
    check (! r->isUiScaleSet(), "a session without the flag does not count as a chosen size");
}

NL_TEST (ui_scale_flag_editor)
{
    useTempSettings ("scale_flag");
    UiSettings::get().setDefaultScale (1.25f);
    auto p = tool::makeProc();
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        check (editor->getWidth() == juce::roundToInt (shell::kBaseW * 1.25f), "a size the user never chose opens at the global default (125 %)",
               juce::String (editor->getWidth()));
    }
    p->setUiScale (1.0f); // the user chose 100 % for this instance
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        check (editor->getWidth() == shell::kBaseW, "a 100 % the user chose is kept, not replaced by the default", juce::String (editor->getWidth()));
    }
    UiSettings::get().setDefaultScale (1.0f);
}

NL_TEST (ui_label_stacking)
{
    std::vector<float> tops { 100.0f, 102.0f, 104.0f, 200.0f };
    ui::stackLabelTops (tops, 12.0f);
    check (tops[0] == 100.0f, "stacked labels: the first label stays on its line");
    check (tops[1] - tops[0] >= 12.0f && tops[2] - tops[1] >= 12.0f, "stacked labels: close lines push labels apart", juce::String (tops[2], 1));
    check (tops[3] == 200.0f, "stacked labels: a label far from the others does not move");
}

NL_TEST (ui_combo_placeholders)
{
    useTempSettings ("combos");
    auto p = tool::makeProc();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    auto* ne = dynamic_cast<NorthlineEditor*> (editor.get());
    if (ne == nullptr) { check (false, "editor created"); return; }

    // Open each overlay in turn and check that every combo box on the editor shows a value or a placeholder.
    std::function<void (juce::Component&, std::vector<juce::ComboBox*>&)> collect = [&] (juce::Component& c, std::vector<juce::ComboBox*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* box = dynamic_cast<juce::ComboBox*> (child)) out.push_back (box);
            collect (*child, out);
        }
    };
    auto blankCombos = [&] (const char* when)
    {
        std::vector<juce::ComboBox*> boxes;
        collect (*ne, boxes);
        int blank = 0;
        for (auto* box : boxes)
            if (box->getSelectedId() == 0 && box->getText().isEmpty() && box->getTextWhenNothingSelected().isEmpty()) ++blank;
        check (blank == 0, juce::String ("every combo shows a value or a placeholder (") + when + ")", juce::String (blank) + " blank of " + juce::String ((int) boxes.size()));
    };
    blankCombos ("editor");
    ne->openPresetBrowser();
    blankCombos ("preset browser");
    ne->closeOverlay();
    ne->openHub();
    blankCombos ("instance hub");
    ne->closeOverlay();
}

NL_TEST (ui_cpu_footer_flag)
{
    auto p = tool::makeProc();
    PrePanel pre (*p);
    check (! pre.cpuFooterShown(), "the CPU footer is off by default");
    pre.setShowCpu (true);
    check (pre.cpuFooterShown(), "Show CPU turns the module CPU footer on");
    pre.setShowCpu (false);
    check (! pre.cpuFooterShown(), "and off again");
}

NL_TEST (ui_panel_activity_headless)
{
    useTempSettings ("activity");
    auto p = tool::makeProc();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    auto* ne = dynamic_cast<NorthlineEditor*> (editor.get());
    if (ne == nullptr) { check (false, "editor created"); return; }
    auto pump = [] { for (int i = 0; i < 4; ++i) juce::Timer::callPendingTimersSynchronously(); };

    ne->selectModule (modEq);
    pump();
    check (ne->isPanelActive (modEq) && ! ne->isPanelActive (modPre), "the selected EQ panel is live (its analyser runs), the others are not",
           "offline editors count as on screen");
    ne->selectModule (modPre);
    pump();
    check (ne->isPanelActive (modPre) && ! ne->isPanelActive (modEq), "selecting PRE makes PRE live (harmonic taps) and EQ idle");

    UiSettings::get().setConsoleLayout (true);
    pump();
    bool all = true;
    for (int m = 0; m < numModules; ++m) all = all && ne->isPanelActive (m);
    check (all, "console layout: every panel is on screen, so every panel is live");
    UiSettings::get().setConsoleLayout (false);
    pump();
}

NL_TEST (ui_render_editor_review)
{
    // Review aid, not a check: NL_RENDER_UI=<folder> (or 1 for build/ui_renders) writes PNGs of the editor
    // at 75, 100 and 150 %, for every module selected, the four overlays, and the high-contrast theme.
    const auto setting = juce::SystemStats::getEnvironmentVariable ("NL_RENDER_UI", {});
    if (setting.isEmpty()) return;
    const juce::File dir = setting == "1" ? juce::File::getCurrentWorkingDirectory().getChildFile ("build/ui_renders") : juce::File (setting);
    dir.createDirectory();
    useTempSettings ("render");

    auto p = tool::makeProc();
    p->loadFactoryPreset (1);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    auto* ne = dynamic_cast<NorthlineEditor*> (editor.get());
    if (ne == nullptr) { check (false, "editor created"); return; }

    // Two seconds of a voice-like tone with hiss bursts, as the snapshot tool feeds.
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    juce::Random rng (11);
    double phase = 0.0;
    const double fs = 48000.0;
    int n = 0;
    auto feed = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b, n += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                const double t = (n + i) / fs;
                const double env = 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 3.0 * t);
                double v = 0.0;
                for (int h = 1; h <= 8; ++h) v += std::sin (phase * h) / h;
                phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / fs;
                const double hiss = (std::fmod (t, 0.6) > 0.5) ? (rng.nextFloat() * 2.0f - 1.0f) * 0.25 : 0.0;
                const float x = (float) (0.22 * env * v + hiss);
                buf.setSample (0, i, x);
                buf.setSample (1, i, x * 0.92f);
            }
            p->processBlock (buf, midi);
        }
    };
    auto pump = [&] (int ms) { for (int t = 0; t < ms; t += 34) { juce::Timer::callPendingTimersSynchronously(); juce::Thread::sleep (34); } };
    auto save = [&] (const juce::String& name, juce::Component& c)
    {
        // Painted the way the window paints it (paintEntireComponent into an ARGB image).
        juce::Image img (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
        {
            juce::Graphics g (img);
            c.paintEntireComponent (g, false);
        }
        const auto& out = img;
        juce::FileOutputStream os (dir.getChildFile (name + ".png"));
        if (os.openedOk()) juce::PNGImageFormat().writeImageToStream (out, os);
    };

    feed (6 * 70);
    pump (300);
    for (int m = 0; m < numModules; ++m)
    {
        ne->selectModule (m);
        feed (6 * 25);
        pump (200);
        save ("module" + juce::String (m) + "_100", *ne);
    }
    ne->selectModule (modPre);

    ne->setScale (0.75f, false);
    pump (100);
    save ("main_75", *ne);
    ne->setScale (1.5f, false);
    pump (100);
    save ("main_150", *ne);
    ne->setScale (1.0f, false);
    pump (100);
    save ("main_100", *ne);

    ne->openPresetBrowser();
    pump (100);
    save ("overlay_presets", *ne);
    ne->closeOverlay();
    ne->openSmartStart();
    pump (100);
    save ("overlay_smart", *ne);
    ne->closeOverlay();
    ne->openHub();
    pump (100);
    save ("overlay_hub", *ne);
    ne->closeOverlay();
    ne->openSettings();
    pump (100);
    save ("overlay_settings", *ne);
    ne->closeOverlay();
    ne->openOnboarding();
    pump (100);
    save ("overlay_tour", *ne);
    ne->closeOverlay();

    UiSettings::get().setTheme (tokens::Theme::highContrast);
    pump (100);
    save ("main_highcontrast", *ne);
    UiSettings::get().setTheme (tokens::Theme::dark);
    pump (100);
    check (true, "render: editor views written to " + dir.getFullPathName());
}

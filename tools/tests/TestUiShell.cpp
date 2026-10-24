// UI shell (roadmap 8.2, 8.3, 8.4, 8.7): tokens and contrast, value parsing and gesture mapping,
// gestures and undo of the controls, chain order, settings, layout geometry, overlays, accessibility
// and editor construction time. Run with `NorthlineTool test ui_`.
#include "TestUtil.h"
#include "gui/ChainView.h"
#include "gui/PluginEditor.h"
#include "gui/Widgets.h"
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
    ne->showOverlay (std::make_unique<SettingsOverlay> (*p, [] (float) {}, [] {}));
    check (ne->hasOverlay(), "showOverlay shows an overlay");
    ne->closeOverlay();
    check (! ne->hasOverlay(), "closeOverlay removes it at once");

    ne->showOverlay (std::make_unique<SettingsOverlay> (*p, [] (float) {}, [] {}));
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
    std::sort (ms.begin(), ms.end());
    const double median = ms[2];
    check (median <= 150.0, "editor constructs in 150 ms or less (median of 5)", juce::String (median, 1) + " ms");
}

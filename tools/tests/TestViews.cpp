#include "../TestUtil.h"
#include "gui/Analyser.h"
#include "gui/EqView.h"
#include "gui/ModulePanels.h"
#include "gui/ViewUtil.h"
#include "gui/Transient.h"
#include "gui/Widgets.h"
#include "dsp/EqModule.h"

// Module views (ROADMAP 8.4 and 8.5): scope taps, analyser maths and threads, EQ handle mapping, the
// sidechain and transient buttons, and the panel layout at the detail and console sizes.
// Run with `NorthlineTool test views_`.
using tool::check;
using namespace nl;

namespace
{
constexpr double kFs = 48000.0;

// Drains a scope tap completely.
std::vector<ScopePoint> drain (ScopeTap& tap)
{
    std::vector<ScopePoint> out;
    ScopePoint buf[64];
    for (;;)
    {
        const size_t got = tap.pop (buf, 64);
        out.insert (out.end(), buf, buf + got);
        if (got < 64) break;
    }
    return out;
}
} // namespace

//==============================================================================
// Scope taps

NL_TEST (views_scope_tap_block_independent)
{
    constexpr int total = 240 * 40 + 100; // 40 whole 5 ms cells and a remainder
    std::vector<float> L (total), R (total);
    for (int i = 0; i < total; ++i)
    {
        L[(size_t) i] = 0.6f * (float) std::sin (2.0 * kPiD * 997.0 * i / kFs) * (1.0f - 0.5f * (float) i / total);
        R[(size_t) i] = 0.3f * (float) std::sin (2.0 * kPiD * 311.0 * i / kFs + 0.4);
    }

    auto run = [&] (int block)
    {
        ScopeTap tap;
        tap.prepare (kFs);
        drain (tap); // the first pop after prepare() discards what was written before it
        std::vector<ScopePoint> points;
        std::vector<float> oL ((size_t) block), oR ((size_t) block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            const float* in[2] { L.data() + pos, R.data() + pos };
            const float* out[2] { oL.data(), oR.data() };
            for (int i = 0; i < n; ++i)
            {
                oL[(size_t) i] = 0.5f * L[(size_t) (pos + i)];
                oR[(size_t) i] = 0.5f * R[(size_t) (pos + i)];
            }
            tap.process (in, out, 2, n, -3.0f, -20.0f);
            auto got = drain (tap);
            points.insert (points.end(), got.begin(), got.end());
        }
        return points;
    };

    const auto ref = run (1);
    check ((int) ref.size() == total / 240, "scope tap: one point per 5 ms cell", juce::String ((int) ref.size()));
    check (ref.front().inDb > -40.0f && ref.front().outDb < ref.front().inDb, "scope tap: output below input at -6 dB", juce::String (ref.front().inDb, 2));
    check (std::abs (ref.front().inDb - ref.front().outDb - 6.0206f) < 0.01f, "scope tap: levels exact to the sample", juce::String (ref.front().inDb - ref.front().outDb, 4));

    for (int block : { 7, 32, 64, 256, 511, 512, 2048 })
    {
        const auto other = run (block);
        bool same = other.size() == ref.size();
        for (size_t k = 0; same && k < ref.size(); ++k)
            same = other[k].inDb == ref[k].inDb && other[k].outDb == ref[k].outDb && other[k].grDb == ref[k].grDb && other[k].keyDb == ref[k].keyDb;
        check (same, "scope tap: identical points at block " + juce::String (block));
    }
}

NL_TEST (views_scope_tap_wrap_keeps_order)
{
    ScopeTap tap;
    tap.prepare (kFs);
    drain (tap);
    constexpr int cells = 5000; // several times the ring's 2048 slots, drained every 100 cells
    std::vector<float> s (240);
    int seen = 0, bad = 0;
    for (int c = 0; c < cells; ++c)
    {
        const float a = (float) ((c % 50) + 1) / 100.0f;
        std::fill (s.begin(), s.end(), a);
        const float* in[1] { s.data() };
        tap.process (in, in, 1, 240, 0.0f, -120.0f);
        if (c % 100 == 99)
            for (const auto& p : drain (tap))
            {
                const float expect = 20.0f * std::log10 ((float) ((seen % 50) + 1) / 100.0f);
                if (std::abs (p.inDb - expect) > 1.0e-3f) ++bad;
                ++seen;
            }
    }
    for (const auto& p : drain (tap))
    {
        const float expect = 20.0f * std::log10 ((float) ((seen % 50) + 1) / 100.0f);
        if (std::abs (p.inDb - expect) > 1.0e-3f) ++bad;
        ++seen;
    }
    check (seen == cells && bad == 0, "scope tap: order kept across ring wrap", juce::String (seen) + " points, " + juce::String (bad) + " out of order");
}

NL_TEST (views_scope_tap_prepare_discards)
{
    ScopeTap tap;
    tap.prepare (kFs);
    drain (tap);
    std::vector<float> s (480, 0.5f); // the last process() call below reads 480 samples
    const float* in[1] { s.data() };
    for (int c = 0; c < 3; ++c) tap.process (in, in, 1, 240, 0.0f, -120.0f);
    tap.prepare (96000.0);
    check (tap.samplesPerCell() == 480, "scope tap: 5 ms at 96 kHz is 480 samples", juce::String (tap.samplesPerCell()));
    check (drain (tap).empty(), "scope tap: the first pop after prepare() discards the ring");
    tap.process (in, in, 1, 480, 0.0f, -120.0f);
    const auto after = drain (tap);
    check (after.size() == 1, "scope tap: new points arrive after the discard", juce::String ((int) after.size()));
}

//==============================================================================
// Analyser maths

NL_TEST (views_analyser_sine_bin_and_level)
{
    spectrum::Analysis an (spectrum::kFftOrder);
    const int bin = 85;
    const double f = bin * kFs / spectrum::kFftSize;
    std::vector<float> x (spectrum::kFftSize), db (spectrum::kBins);
    for (int i = 0; i < spectrum::kFftSize; ++i) x[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPiD * f * i / kFs);
    an.analyse (x.data(), db.data());

    int peak = 0;
    for (int k = 1; k < spectrum::kBins; ++k)
        if (db[(size_t) k] > db[(size_t) peak]) peak = k;
    check (peak == bin, "analyser: a sine lands in its bin", juce::String (peak));
    check (std::abs (db[(size_t) bin] - (-6.0206f)) < 0.05f, "analyser: a 0.5 sine reads -6.02 dBFS", juce::String (db[(size_t) bin], 3));
    check (db[(size_t) (bin + 10)] < db[(size_t) bin] - 60.0f, "analyser: the Hann window keeps leakage 60 dB down", juce::String (db[(size_t) (bin + 10)], 1));
}

NL_TEST (views_analyser_tilt_slope)
{
    check (std::abs (spectrum::tiltDb (1000.0)) < 1.0e-9, "tilt: 0 dB at 1 kHz");
    check (std::abs (spectrum::tiltDb (2000.0) - 4.5) < 1.0e-9, "tilt: +4.5 dB at 2 kHz");
    check (std::abs (spectrum::tiltDb (500.0) + 4.5) < 1.0e-9, "tilt: -4.5 dB at 500 Hz");
    check (std::abs ((spectrum::tiltDb (8000.0) - spectrum::tiltDb (4000.0)) - 4.5) < 1.0e-9, "tilt: 4.5 dB per octave above 1 kHz");
    check (std::abs ((spectrum::tiltDb (40.0) - spectrum::tiltDb (80.0)) + 4.5) < 1.0e-9, "tilt: 4.5 dB per octave below 1 kHz");
}

NL_TEST (views_analyser_display_bins)
{
    check (spectrum::displayBinForFrequency (20.0) == 0, "display bins: 20 Hz is the first bin");
    check (spectrum::displayBinForFrequency (30000.0) == spectrum::kDisplayBins - 1, "display bins: clamped at the top");
    check (std::abs (spectrum::displayEdge (spectrum::kDisplayBins) - 20000.0) < 1.0e-6, "display bins: 20 kHz at the top edge");

    spectrum::Analysis an (spectrum::kFftOrder);
    const double f = 85 * kFs / spectrum::kFftSize;
    std::vector<float> x (spectrum::kFftSize), db (spectrum::kBins), disp (spectrum::kDisplayBins);
    for (int i = 0; i < spectrum::kFftSize; ++i) x[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPiD * f * i / kFs);
    an.analyse (x.data(), db.data());
    spectrum::toDisplay (db.data(), spectrum::kBins, spectrum::kFftSize, kFs, disp.data(), spectrum::kDisplayBins);
    int peak = 0;
    for (int i = 1; i < spectrum::kDisplayBins; ++i)
        if (disp[(size_t) i] > disp[(size_t) peak]) peak = i;
    check (std::abs (peak - spectrum::displayBinForFrequency (f)) <= 1, "display bins: the tone lands in its display bin", juce::String (peak));
}

NL_TEST (views_harmonic_levels_read_dbc)
{
    spectrum::Analysis an (spectrum::kHarmonicOrder);
    const double fs = kFs, f0 = 34 * fs / spectrum::kHarmonicFftSize, f2 = 2 * f0; // bin-centred: bins 34 and 68
    std::vector<float> x (spectrum::kHarmonicFftSize), db (spectrum::kHarmonicFftSize / 2 + 1);
    for (int i = 0; i < spectrum::kHarmonicFftSize; ++i)
        x[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPiD * f0 * i / fs) + 0.05f * (float) std::sin (2.0 * kPiD * f2 * i / fs);
    an.analyse (x.data(), db.data());
    float levels[1 + spectrum::kHarmonics] {};
    spectrum::harmonicLevels (db.data(), spectrum::kHarmonicFftSize, fs, f0, levels, spectrum::kHarmonics);
    check (std::abs (levels[0] + 6.0206f) < 0.05f, "harmonics: fundamental in dBFS", juce::String (levels[0], 3));
    check (std::abs (levels[1] + 20.0f) < 0.1f, "harmonics: H2 reads -20 dBc", juce::String (levels[1], 3));
    check (levels[2] < -60.0f, "harmonics: H3 absent", juce::String (levels[2], 1));
}

NL_TEST (views_seqlock_roundtrip)
{
    spectrum::Seqlock<4> s;
    float out[4] {};
    check (! s.read (out), "seqlock: nothing to read before the first publish");
    const float in[4] { 1.0f, 2.0f, 3.0f, 4.0f };
    s.publish (in);
    check (s.read (out) && out[0] == 1.0f && out[3] == 4.0f, "seqlock: the published frame is read back");
}

NL_TEST (views_eq_analyser_peak_at_tone)
{
    auto proc = tool::makeProc (kFs, 256);
    EqAnalyser an (proc->getStrip());
    an.attach();

    juce::AudioBuffer<float> work (juce::jmax (2, proc->getTotalNumInputChannels()), 256);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < 300; ++blk)
    {
        work.clear();
        for (int i = 0; i < 256; ++i)
        {
            const float v = 0.4f * (float) std::sin (2.0 * kPiD * 1000.0 * (blk * 256 + i) / kFs);
            for (int c = 0; c < work.getNumChannels(); ++c) work.setSample (c, i, v);
        }
        proc->processBlock (work, midi);
        juce::Thread::sleep (1);
    }
    std::array<float, 2 * spectrum::kDisplayBins> frame {};
    const bool have = an.readPre (frame.data());
    an.detach();

    check (have, "eq analyser: a frame is published after 1.6 s of tone");
    if (! have) return;
    int peak = 0;
    for (int i = 1; i < spectrum::kDisplayBins; ++i)
        if (frame[(size_t) i] > frame[(size_t) peak]) peak = i;
    check (std::abs (peak - spectrum::displayBinForFrequency (1000.0)) <= 2, "eq analyser: the 1 kHz tone is the peak", juce::String (peak));
}

//==============================================================================
// EQ display geometry and handle mapping

NL_TEST (views_eq_geometry_round_trip)
{
    EqGeometry g { 400.0f, 200.0f };
    check (std::abs (g.xForFreq (20.0)) < 1.0e-4f && std::abs (g.xForFreq (20000.0) - 400.0f) < 1.0e-3f, "eq geometry: 20 Hz and 20 kHz at the edges");
    check (std::abs (g.freqForX (0.0f) - 20.0) < 1.0e-6 && std::abs (g.freqForX (400.0f) - 20000.0) < 1.0e-3, "eq geometry: edges back to the frequency range");
    check (std::abs (g.yForDb (0.0f) - 100.0f) < 1.0e-4f && std::abs (g.yForDb (18.0f)) < 1.0e-4f && std::abs (g.yForDb (-18.0f) - 200.0f) < 1.0e-4f,
           "eq geometry: +18 dB at the top, 0 dB at the middle, -18 dB at the bottom");

    bool ok = true;
    for (double f : { 35.0, 120.0, 1000.0, 8000.0, 16000.0 })
        for (float db : { -15.0f, -6.5f, 0.0f, 3.25f, 17.0f })
        {
            const auto v = valuesAt (g, handleFor (g, f, db));
            ok = ok && std::abs (v.freqHz - f) / f < 1.0e-3 && std::abs (v.gainDb - db) < 1.0e-3f;
        }
    check (ok, "eq handles: a handle position maps back to the band's frequency and gain");

    const auto far = valuesAt (g, { -50.0f, 500.0f });
    check (std::abs (far.freqHz - 20.0) < 1.0e-6 && std::abs (far.gainDb + 18.0f) < 1.0e-5f, "eq handles: positions outside the display are clamped to the parameter ranges");
}

NL_TEST (views_eq_wheel_q)
{
    check (qAfterWheel (1.0f, 1.0f, false) > 1.0f, "eq wheel: up raises Q");
    check (qAfterWheel (1.0f, -1.0f, false) < 1.0f, "eq wheel: down lowers Q");
    check (std::abs (qAfterWheel (1.0f, 1.0f, true) - 1.0f) < std::abs (qAfterWheel (1.0f, 1.0f, false) - 1.0f), "eq wheel: Shift makes the step finer");
    check (qAfterWheel (9.9f, 2.0f, false) <= 10.0f && qAfterWheel (0.21f, -2.0f, false) >= 0.2f, "eq wheel: Q stays in 0.2 to 10");
}

NL_TEST (views_eq_handle_maps_to_parameter)
{
    auto proc = tool::makeProc();
    // A band's frequency and gain parameters, set through the parameter, read back through the handle.
    const int o = P::eqMidOn; // band 1 block
    auto setPlainValue = [&] (int index, float v) { view::setPlain (*proc, index, v); };
    setPlainValue (o + 2, 1234.0f);
    setPlainValue (o + 3, -4.5f);
    EqGeometry g { 600.0f, 240.0f };
    const auto at = handleFor (g, view::plain (*proc, o + 2), view::plain (*proc, o + 3));
    const auto back = valuesAt (g, at);
    check (std::abs (back.freqHz - 1234.0) / 1234.0 < 0.01 && std::abs (back.gainDb + 4.5f) < 0.05f,
           "eq handles: the parameter values survive the round trip through the display", juce::String (back.freqHz, 1) + " Hz " + juce::String (back.gainDb, 2) + " dB");
}

//==============================================================================
// Transient buttons and sidechain status

NL_TEST (views_param_button_toggles_transient)
{
    auto proc = tool::makeProc();
    ParamButton band2 (*proc, P::dsListen, 2, "BAND 2");
    check (band2.findColour (juce::TextButton::buttonOnColourId) == colours::amber, "listen button: lit in amber (transient)");
    band2.onClick();
    check (view::plain (*proc, P::dsListen) > 1.5f && view::plain (*proc, P::dsListen) < 2.5f, "listen button: a click selects band 2");
    band2.onClick();
    check (view::plain (*proc, P::dsListen) < 0.5f, "listen button: a second click releases it");
    check (isTransient (P::dsListen), "listen button: the listen parameter is transient");
}

//==============================================================================
// Panel layout at the detail and console sizes

namespace
{
// Children that stick out of the panel, or that run into the footer or the header band.
void checkPanel (const char* name, juce::Component& panel, int w, int h, int& outside, int& footer, int& header)
{
    for (auto* c : panel.getChildren())
    {
        if (! c->isVisible()) continue;
        const auto b = c->getBounds();
        if (b.getX() < -1 || b.getY() < -1 || b.getRight() > w + 1 || b.getBottom() > h + 1)
        {
            ++outside;
            std::printf ("    %s: child outside (%d,%d %dx%d) in %dx%d\n", name, b.getX(), b.getY(), b.getWidth(), b.getHeight(), w, h);
        }
        if (b.getY() < h - 18 && b.getBottom() > h - 18 + 1) ++footer;
        if (b.getY() < 32 && b.getBottom() > 33) ++header;
    }
}
} // namespace

NL_TEST (views_panel_layout_fits)
{
    auto proc = tool::makeProc();
    std::vector<std::unique_ptr<ModulePanel>> panels;
    panels.push_back (std::make_unique<PrePanel> (*proc));
    panels.push_back (std::make_unique<GatePanel> (*proc));
    panels.push_back (std::make_unique<DsPanel> (*proc));
    panels.push_back (std::make_unique<EqPanel> (*proc));
    panels.push_back (std::make_unique<CompPanel> (*proc));

    struct Size { int w, h; const char* what; };
    int outside = 0, footer = 0, header = 0, cases = 0;
    for (auto& panel : panels)
    {
        // 996 x 632 is the detail area the editor really gives a panel (roadmap 8.2 says "about 1020 x 664").
        const std::vector<Size> sizes { { 996, 632, "chain detail" }, { 1020, 664, "detail" }, { 900, 560, "small detail" },
                                        { 1400, 900, "large detail" }, { panel->preferredWidth(), 632, "console" } };
        for (bool drawer : { false, true })
            for (const auto& s : sizes)
            {
                panel->setDrawerOpen (drawer);
                panel->setBounds (0, 0, s.w, s.h);
                const juce::String name = juce::String (panel->getModuleId()) + " " + s.what + (drawer ? " drawer" : "");
                const int before = outside + footer + header;
                checkPanel (name.toRawUTF8(), *panel, s.w, s.h, outside, footer, header);
                ++cases;
                if (outside + footer + header != before) std::printf ("    (module %d, %s)\n", panel->getModuleId(), name.toRawUTF8());
            }
    }
    check (outside == 0, "panel layout: every control sits inside its panel", juce::String (cases) + " layouts");
    check (footer == 0, "panel layout: nothing runs into the footer");
    check (header == 0, "panel layout: nothing runs into the header");
}

NL_TEST (views_sidechain_no_key_without_input)
{
    auto proc = tool::makeProc();
    CompPanel comp (*proc);
    comp.setBounds (0, 0, comp.preferredWidth(), 664);
    check (comp.getModuleId() == modComp, "panels: the COMP panel reports its module");
    check (proc->getStrip().getSidechainDb() < -100.0f, "sidechain: no key level without a sidechain input");
}

//==============================================================================
// PRE: the static transfer curve (own engine instance, message thread)

NL_TEST (views_transfer_curve_monotonic_and_drive)
{
    PreParams low, high;
    low.satOn = high.satOn = true;
    low.satType = high.satType = 0;
    low.drive = 0.0f;
    high.drive = 100.0f;
    low.satAutoGain = high.satAutoGain = false; // without make-up, drive alone moves the curve
    TransferCurve a, b;
    computeTransferCurve (low, kFs, a);
    computeTransferCurve (high, kFs, b);

    bool monotonic = true, drive = false;
    float maxDiff = 0.0f;
    for (int k = 1; k < TransferCurve::kPoints; ++k)
    {
        monotonic = monotonic && b.outDb[k] + 1.0e-3f >= b.outDb[k - 1];
        maxDiff = std::max (maxDiff, std::abs (b.outDb[k] - a.outDb[k]));
    }
    drive = maxDiff > 3.0f;
    check (monotonic, "transfer curve: output never falls as the input rises");
    check (drive, "transfer curve: drive moves the curve", juce::String (maxDiff, 2) + " dB");
    check (std::abs (a.outDb[TransferCurve::kPoints - 1] - 0.0f) < 3.0f || a.outDb[TransferCurve::kPoints - 1] > -10.0f,
           "transfer curve: low drive is near unity at full scale", juce::String (a.outDb[TransferCurve::kPoints - 1], 2));
}

//==============================================================================
// Review aid, not a check: NL_RENDER_VIEWS=1 (or a folder) writes PNGs of every panel at the detail and
// console sizes, with a tone and noise running through the strip so the displays have data.

NL_TEST (views_render_panels)
{
    const auto setting = juce::SystemStats::getEnvironmentVariable ("NL_RENDER_VIEWS", {});
    if (setting.isEmpty()) return;
    const juce::File dir = setting == "1" ? juce::File::getCurrentWorkingDirectory().getChildFile ("build/views_renders") : juce::File (setting);
    dir.createDirectory();

    auto proc = tool::makeProc (kFs, 256);
    using view::setPlain;
    setPlain (*proc, P::satOn, 1.0f);
    setPlain (*proc, P::satDrive, 60.0f);
    setPlain (*proc, P::hpOn, 1.0f);
    setPlain (*proc, P::hpFreq, 120.0f);
    setPlain (*proc, P::lpOn, 1.0f);
    setPlain (*proc, P::lpFreq, 9000.0f);
    setPlain (*proc, P::eqMidGain, 6.0f);
    setPlain (*proc, P::eqMidFreq, 1000.0f);
    setPlain (*proc, P::eqMidDyn, 1.0f);
    setPlain (*proc, P::eqMidDynThresh, -40.0f);
    setPlain (*proc, P::eqMidDynRange, 6.0f);
    setPlain (*proc, P::compOn, 1.0f);
    setPlain (*proc, P::compThresh, -30.0f);
    setPlain (*proc, P::compRatio, 4.0f);
    setPlain (*proc, P::gateOn, 1.0f);
    setPlain (*proc, P::gateThresh, -35.0f);
    setPlain (*proc, P::dsOn, 1.0f);
    setPlain (*proc, P::ds2On, 1.0f);

    std::vector<std::unique_ptr<ModulePanel>> panels;
    panels.push_back (std::make_unique<PrePanel> (*proc));
    panels.push_back (std::make_unique<GatePanel> (*proc));
    panels.push_back (std::make_unique<DsPanel> (*proc));
    panels.push_back (std::make_unique<EqPanel> (*proc));
    panels.push_back (std::make_unique<CompPanel> (*proc));
    // Headless panels never report isShowing(), so a visibility change would deactivate them. Show first, then activate.
    for (auto& p : panels) { p->setVisible (true); p->setActive (true); }

    juce::AudioBuffer<float> work (juce::jmax (2, proc->getTotalNumInputChannels()), 256);
    juce::MidiBuffer midi;
    juce::Random rnd (3);
    for (int blk = 0; blk < 400; ++blk)
    {
        work.clear();
        const float burst = (blk % 40) < 20 ? 1.0f : 0.2f;
        for (int i = 0; i < 256; ++i)
        {
            const float tone = 0.25f * burst * (float) std::sin (2.0 * kPiD * 1000.0 * (blk * 256 + i) / kFs);
            const float v = tone + 0.08f * (rnd.nextFloat() * 2.0f - 1.0f);
            for (int c = 0; c < work.getNumChannels(); ++c) work.setSample (c, i, v);
        }
        proc->processBlock (work, midi);
        juce::Thread::sleep (1);
    }

    auto pumpTimers = [] (int ms) { for (int t = 0; t < ms; t += 34) { juce::Timer::callPendingTimersSynchronously(); juce::Thread::sleep (34); } };
    pumpTimers (600); // lets the displays' timers run
    for (auto& p : panels)
    {
        const std::vector<std::pair<int, int>> sizes { { 1020, 664 }, { p->preferredWidth(), 664 } };
        for (bool drawer : { false, true })
            for (auto [w, h] : sizes)
            {
                p->setDrawerOpen (drawer);
                p->setBounds (0, 0, w, h);
                pumpTimers (120);
                juce::Image img (juce::Image::ARGB, w, h, true);
                {
                    juce::Graphics g (img);
                    p->paintEntireComponent (g, false);
                }
                juce::Image out (juce::Image::ARGB, w, h, false); // ARGB: the RGB PNG path dropped content in review renders
                {
                    juce::Graphics g (out);
                    g.fillAll (colours::background);
                    g.drawImageAt (img, 0, 0);
                }
                const auto name = "module" + juce::String (p->getModuleId()) + "_" + juce::String (w) + "x" + juce::String (h) + (drawer ? "_drawer" : "") + ".png";
                juce::FileOutputStream os (dir.getChildFile (name));
                if (os.openedOk()) juce::PNGImageFormat().writeImageToStream (out, os);
            }
    }
    for (auto& p : panels) p->setActive (false);
    check (true, "render: panels written to " + dir.getFullPathName());
}

//==============================================================================
// Transient controls (wave 3): Compare, Delta and the EQ Q rule. The rules are pure and the processor calls check
// the button-to-API mapping end to end.

NL_TEST (views_compare_target_mapping)
{
    using namespace nl::transient;
    check (kCompareOff == -1 && kCompareStrip == 0, "compare: off is -1, the whole strip is 0");
    check (moduleCompareTarget (modPre) == 1 && moduleCompareTarget (modComp) == 1 + modComp, "compare: a module's target is 1 + its id");
    check (toggleModuleCompare (kCompareOff, modEq) == moduleCompareTarget (modEq), "module compare: from off, asks for that module");
    check (toggleModuleCompare (moduleCompareTarget (modEq), modEq) == kCompareOff, "module compare: a second press turns it off");
    check (toggleModuleCompare (kCompareStrip, modEq) == moduleCompareTarget (modEq), "module compare: replaces the whole-strip compare");
    check (toggleModuleCompare (moduleCompareTarget (modGate), modEq) == moduleCompareTarget (modEq), "module compare: replaces another module's");
}

NL_TEST (views_compare_hold_latch)
{
    using namespace nl::transient;
    CompareHold h;
    check (h.press (false) == kCompareStrip, "compare hold: pressing compares the whole strip");
    check (h.release() == kCompareOff, "compare hold: releasing goes back");
    check (h.press (true) == kCompareStrip && h.isLatched(), "compare latch: Shift-press latches it on");
    check (h.release() == kCompareStrip, "compare latch: releasing keeps it on");
    check (h.press (false) == kCompareOff && ! h.isLatched(), "compare latch: a plain click lets it go");
    check (h.release() == kCompareOff, "compare latch: off after the click");
    check (h.press (true) == kCompareStrip, "compare latch: Shift-press latches again");
    check (h.press (true) == kCompareOff, "compare latch: a second Shift-press unlatches");
    check (h.release() == kCompareOff, "compare latch: off after unlatching");
}

NL_TEST (views_compare_delta_api_mapping)
{
    auto proc = tool::makeProc();
    proc->setCompare (nl::transient::moduleCompareTarget (modEq));
    check (proc->getCompare() == 1 + modEq, "compare API: a module target round-trips");
    proc->setCompare (nl::transient::kCompareStrip);
    check (proc->getCompare() == nl::transient::kCompareStrip, "compare API: the whole strip round-trips");
    proc->setCompare (nl::transient::kCompareOff);
    check (proc->getCompare() == nl::transient::kCompareOff, "compare API: off round-trips");

    proc->setDelta (modDs, true);
    check (proc->getDelta (modDs) && juce::roundToInt (view::plain (*proc, P::dsListen)) == 3, "delta API: DE-ESS Delta is its listen mode 3");
    proc->setDelta (modDs, false);
    check (! proc->getDelta (modDs), "delta API: DE-ESS Delta turns off");
    proc->setDelta (modComp, true);
    check (proc->getDelta (modComp) && juce::roundToInt (view::plain (*proc, P::compListen)) == 2, "delta API: COMP Delta is its listen mode 2");
    proc->setDelta (modComp, false);
    check (! proc->getDelta (modComp), "delta API: COMP Delta turns off");
    proc->setDelta (modEq, true);
    check (proc->getDelta (modEq), "delta API: other modules run Delta in the strip");
    proc->setDelta (modEq, false);
    check (! proc->getDelta (modEq), "delta API: the strip Delta turns off");
}

NL_TEST (views_eq_q_only_for_bell_types)
{
    // The Q knob is live only for the band types whose kind takes the band's Q (EqModule::kindOf). Shelves and Wide /
    // Narrow set their own width, so their base Q does not move with the knob.
    int mismatches = 0, cases = 0;
    for (int band = 0; band < 4; ++band)
    {
        const auto& choices = paramDef (P::eqLowOn + band * P::kEqBandStride + 1).choices;
        mismatches += (int) choices.size() != 3;
        for (int type = 0; type < 3; ++type)
        {
            const bool kindUsesQ = EqModule::kindOf (band, type, 0.5f).baseQ != EqModule::kindOf (band, type, 3.0f).baseQ;
            mismatches += kindUsesQ != nl::transient::eqBandUsesQ (type);
            ++cases;
        }
    }
    check (mismatches == 0, "eq Q: the Q knob is live exactly for the band types that use Q", juce::String (cases) + " band types");
    check (! nl::transient::eqBandUsesQ (0) && ! nl::transient::eqBandUsesQ (1) && nl::transient::eqBandUsesQ (2),
           "eq Q: Shelf, Wide and Narrow ignore Q; Bell and Param use it");
}

NL_TEST (views_knob_inert_and_monitor_look)
{
    auto proc = tool::makeProc();
    Knob q (*proc, P::eqLowOn + 4, "Q", colours::eq);
    check (! q.isInert(), "knob: a Q knob starts live");
    q.setInert (true);
    check (q.isInert(), "knob: setInert dims it and blocks edits");
    q.setInert (false);
    check (! q.isInert(), "knob: setInert(false) makes it live again");

    // The monitor mode is transient: its outline is amber away from Stereo (choice 0) and plain at Stereo.
    ChoiceBox monitor (*proc, P::monitor);
    const auto& choices = paramDef (P::monitor).choices;
    const float step = 1.0f / (float) (choices.size() - 1);
    proc->param (P::monitor)->setValueNotifyingHost (step);
    monitor.updateTransientLook();
    check (monitor.getProperties().getWithDefault ("transient", false), "monitor: away from Stereo the outline is amber");
    proc->param (P::monitor)->setValueNotifyingHost (0.0f);
    monitor.updateTransientLook();
    check (! monitor.getProperties().getWithDefault ("transient", false), "monitor: back at Stereo the outline is plain");
}

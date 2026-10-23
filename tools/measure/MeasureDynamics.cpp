#include "Measure.h"
#include "../tests/DynamicsTestKit.h"

using namespace tool;
using namespace tool::dk;

namespace
{
const char* const kCompNames[] { "VCA", "FET", "OPT", "SOFT" };

// Percent of one core per second of stereo audio at 48 kHz, 512-sample blocks, best of several runs.
template <class Make>
double cpuPercent (Make make, bool quick)
{
    const int n = 48000 * (quick ? 2 : 10);
    const auto in = pinkNoise (n, -18.0f, 9);
    double best = 1.0e9;
    juce::ScopedNoDenormals nd;
    for (int rep = 0; rep < (quick ? 1 : 3); ++rep)
    {
        auto m = make();
        auto work = in;
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int pos = 0; pos < n; pos += 512)
        {
            float* io[2] { work.getWritePointer (0) + pos, work.getWritePointer (1) + pos };
            m->process (io, 2, juce::jmin (512, n - pos), nullptr);
        }
        best = std::min (best, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
    }
    return 100.0 * best / (n / 48000.0);
}

// Open/close transitions of the gate on a 40 Hz sine that decays through the threshold (hold 0).
int gateTransitions (float scHp)
{
    GateParams gp;
    gp.thresh = -30; gp.hyst = 4; gp.holdMs = 0; gp.releaseMs = 20; gp.attackMs = 0.5f; gp.floorDb = -40; gp.scHp = scHp;
    auto g = makeModule<GateModule> (gp);
    const int n = 3 * 48000;
    juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kFs;
        const float v = lin (-20.0f - 20.0f * (float) t) * (float) std::sin (2.0 * kPiD * 40.0 * t);
        in.setSample (0, i, v);
        in.setSample (1, i, v);
    }
    int toggles = 0;
    bool closed = false;
    auto out = in;
    juce::ScopedNoDenormals nd;
    for (int pos = 0; pos < n; pos += 32)
    {
        float* io[2] { out.getWritePointer (0) + pos, out.getWritePointer (1) + pos };
        g->process (io, 2, 32, nullptr);
        const bool nowClosed = g->getGainReductionDb() < -20.0f;
        if (nowClosed != closed) { ++toggles; closed = nowClosed; }
    }
    return toggles;
}
} // namespace

NL_MEASURE (dynamics, "COMP, GATE and DE-ESS: static curves, honest timing, auto make-up, chatter, lookahead, band isolation, CPU")
{
    // ---------------------------------------------------------------- COMP
    r.h2 ("COMP");
    r.h3 ("Static curve: worst error against the knee formula (dB)");
    r.para ("1 kHz sine, -60..0 dBFS in 3 dB steps, threshold -24 dB. Spec: within 0.2 dB.");
    r.table ({ "Type", "Knee 0, 2:1", "Knee 6, 4:1", "Knee 12, 10:1", "Knee 24, 10:1" });
    for (int t = 0; t < 4; ++t)
        r.row ({ kCompNames[t], tool::fmt (staticCurveError (t, 0.0f, 2.0f, -24.0f), 3), tool::fmt (staticCurveError (t, 6.0f, 4.0f, -24.0f), 3),
                 tool::fmt (staticCurveError (t, 12.0f, 10.0f, -24.0f), 3), tool::fmt (staticCurveError (t, 24.0f, 10.0f, -24.0f), 3) });
    r.endTable();

    r.h3 ("Gain-reduction ripple on a steady sine (peak-to-peak dB, last second)");
    r.para ("Sine at -18 dBFS, threshold -24 dB, 4:1, hard knee, default timing. A peak detector has no ripple; an RMS window passes a ripple at twice the signal frequency.");
    r.table ({ "Type", "100 Hz", "1 kHz" });
    for (int t = 0; t < 4; ++t)
    {
        juce::String cols[2];
        for (int k = 0; k < 2; ++k)
        {
            const double f = k == 0 ? 100.0 : 1000.0;
            CompParams cp;
            cp.type = t; cp.thresh = -24; cp.ratio = 4; cp.knee = 0;
            auto m = makeModule<CompModule> (cp);
            const int n = 2 * 48000;
            const auto in = sine (kFs, f, lin (-18.0f), n);
            float lch = 0.0f, rch = 0.0f;
            float* io[2] { &lch, &rch };
            float lo = 0.0f, hi = 0.0f;
            juce::ScopedNoDenormals nd;
            for (int i = 0; i < n; ++i)
            {
                lch = in.getSample (0, i);
                rch = in.getSample (1, i);
                m->process (io, 2, 1, nullptr);
                if (i >= n / 2)
                {
                    const float g = m->getGainReductionDb();
                    if (i == n / 2) lo = hi = g;
                    lo = std::min (lo, g);
                    hi = std::max (hi, g);
                }
            }
            cols[k] = tool::fmt (hi - lo, 3);
        }
        r.row ({ kCompNames[t], cols[0], cols[1] });
    }
    r.endTable();

    r.h3 ("Measured attack: 10-90% of a 10 dB step, error against the knob");
    r.para ("Threshold -40 dB, 4:1, hard knee. Spec: within 10 % for every type and sample rate. n/a: below the type's minimum attack.");
    r.table ({ "Type", "Attack (ms)", "44.1 kHz", "48 kHz", "96 kHz", "192 kHz" });
    const double rates[] { 44100.0, 48000.0, 96000.0, 192000.0 };
    for (int t = 0; t < 4; ++t)
        for (float a : { 1.0f, 3.0f, 10.0f, 40.0f })
        {
            juce::StringArray cols;
            cols.add (kCompNames[t]);
            cols.add (tool::fmt (a, 0));
            for (double fs : rates)
            {
                const bool skip = quick && fs != 48000.0;
                if (a < CompModule::minAttackMs (t)) cols.add ("n/a");
                else if (skip) cols.add ("-");
                else cols.add (tool::fmt (100.0 * (measureCompAttackMs (t, fs, a) / a - 1.0), 1) + " %");
            }
            r.row ({ cols[0], cols[1], cols[2], cols[3], cols[4], cols[5] });
        }
    r.endTable();

    r.h3 ("Measured release: 10-90% recovery after a 30 dB step, 48 kHz");
    r.para ("Spec for VCA, FET and SOFT: within 12 % of the knob. OPT's release is scaled by its gain-reduction memory, so the figure is for the state after a 30 dB step.");
    r.table ({ "Type", "Release knob (ms)", "Measured (ms)", "Error" });
    for (int t = 0; t < 4; ++t)
        for (float rel : { 40.0f, 150.0f, 600.0f })
        {
            if (quick && rel == 40.0f) continue;
            const double ms = measureCompReleaseMs (t, 48000.0, rel);
            r.row ({ kCompNames[t], tool::fmt (rel, 0), tool::fmt (ms, 1), tool::fmt (100.0 * (ms / rel - 1.0), 1) + " %" });
        }
    r.endTable();

    r.h3 ("Program-dependent release (VCA, knob 100 ms)");
    r.table ({ "Auto release", "After 50 ms of compression (ms)", "After 4 s of compression (ms)" });
    for (bool autoRel : { false, true })
        r.row ({ autoRel ? "on" : "off", tool::fmt (measureCompAutoRelease (50.0, autoRel), 0), tool::fmt (measureCompAutoRelease (4000.0, autoRel), 0) });
    r.endTable();

    r.h3 ("Auto make-up: integrated loudness error at the Operating Level (LU, pink noise at -18 dBFS RMS)");
    r.para ("Spec grid: ratio 1.5-10, threshold -40..-10 dB (spec: within 1 LU). The held-out grid (ratio 3-8, threshold -35..-15 dB) and two other timings check that the fit generalises.");
    r.table ({ "Type", "Spec grid, worst", "Held-out, worst", "Fast timing 3/80 ms, worst", "Slow timing 30/400 ms, worst" });
    for (int t = 0; t < 4; ++t)
    {
        double specWorst = 0.0, heldWorst = 0.0, fastWorst = 0.0, slowWorst = 0.0;
        for (float ratio : { 1.5f, 2.0f, 4.0f, 10.0f })
            for (float thr : { -40.0f, -30.0f, -20.0f, -10.0f })
                specWorst = std::max (specWorst, std::abs (makeupErrorLu (t, ratio, thr, 48000.0)));
        if (! quick)
            for (float ratio : { 3.0f, 6.0f, 8.0f })
                for (float thr : { -35.0f, -25.0f, -15.0f })
                {
                    heldWorst = std::max (heldWorst, std::abs (makeupErrorLu (t, ratio, thr, 48000.0)));
                    fastWorst = std::max (fastWorst, std::abs (makeupErrorLu (t, ratio, thr, 48000.0, 3.0f, 80.0f)));
                    slowWorst = std::max (slowWorst, std::abs (makeupErrorLu (t, ratio, thr, 48000.0, 30.0f, 400.0f)));
                }
        const auto dash = juce::String ("-");
        r.row ({ kCompNames[t], tool::fmt (specWorst, 2), quick ? dash : tool::fmt (heldWorst, 2), quick ? dash : tool::fmt (fastWorst, 2), quick ? dash : tool::fmt (slowWorst, 2) });
    }
    r.endTable();

    r.h3 ("CPU per module");
    r.table ({ "Configuration", "% of one core" });
    for (int t = 0; t < 4; ++t)
    {
        CompParams cp;
        cp.type = t;
        r.row ({ kCompNames[t], tool::fmt (cpuPercent ([&] { return makeModule<CompModule> (cp); }, quick), 2) });
    }
    r.endTable();

    // ---------------------------------------------------------------- GATE
    r.h2 ("GATE");
    r.h3 ("Detector chatter: 40 Hz sine decaying 20 dB/s through the threshold, hold 0");
    r.para ("Spec: at most one open/close cycle (two transitions).");
    r.table ({ "Sidechain high-pass (Hz)", "Open/close transitions" });
    for (float hp : { 20.0f, 30.0f })
        r.row ({ tool::fmt (hp, 0), juce::String (gateTransitions (hp)) });
    r.endTable();

    r.h3 ("Kick transient loss in the first 5 ms (0.5 ms opening)");
    r.para ("A click and 60 Hz body opening a closed gate. Spec: at most 0.5 dB with 2 ms lookahead.");
    r.table ({ "Lookahead (ms)", "Loss (dB)" });
    const int laMs[] { 0, 1, 2, 5 }; // kLookaheadMs
    for (int i = 0; i < 4; ++i)
        r.row ({ juce::String (laMs[i]), tool::fmt (kickLossDb (i), 2) });
    r.endTable();

    r.h3 ("Opening click at a -60 dB floor: splatter above 8 kHz");
    r.para ("1 kHz tone through an externally keyed gate. Spec: -60 dB re signal for the 0.5 ms default.");
    r.table ({ "Attack (ms)", "Splatter (dB re signal)", "Largest derivative step (x)" });
    for (float a : { 0.02f, 0.1f, 0.3f, 0.5f, 1.0f })
    {
        const auto c = gateOpeningClick (a);
        r.row ({ tool::fmt (a, 2), tool::fmt (c.splatterDb, 1), tool::fmt (c.stepRatio, 2) });
    }
    r.endTable();

    r.h3 ("CPU per module");
    r.table ({ "Type", "% of one core" });
    const char* const gateNames[] { "Gate", "Expander", "Duck" };
    for (int t = 0; t < 3; ++t)
    {
        GateParams gp;
        gp.type = t;
        r.row ({ gateNames[t], tool::fmt (cpuPercent ([&] { return makeModule<GateModule> (gp); }, quick), 2) });
    }
    r.endTable();

    // ---------------------------------------------------------------- DE-ESS
    r.h2 ("DE-ESS");
    r.h3 ("Band isolation at matched gain reduction (synthetic vocal, 100 Hz - 1.5 kHz)");
    r.para ("Spec (Split mode): change outside the band within 0.3 dB at matched gain reduction.");
    r.table ({ "Mode", "Gain reduction (dB)", "Change outside the band (dB)" });
    const char* const dsModes[] { "Split", "Wide" };
    for (int m = 0; m < 2; ++m)
    {
        const auto s = deEssSpectrum (m);
        r.row ({ dsModes[m], tool::fmt (s.grDb, 2), tool::fmt (s.outsideDb, 2) });
    }
    r.endTable();

    r.h3 ("Measured attack: 10-90% of a step, error against the knob (60 ms release)");
    r.table ({ "Mode", "Attack knob (ms)", "Measured (ms)", "Error" });
    for (int m = 0; m < 2; ++m)
        for (float a : { 0.5f, 2.0f, 5.0f })
        {
            const double ms = measureDsAttackMs (m, a, 60.0f);
            r.row ({ dsModes[m], tool::fmt (a, 1), tool::fmt (ms, 2), tool::fmt (100.0 * (ms / a - 1.0), 1) + " %" });
        }
    r.endTable();

    r.h3 ("Measured release: 10-90% recovery, error against the knob");
    r.table ({ "Release knob (ms)", "Measured (ms)", "Error" });
    for (float rel : { 40.0f, 150.0f })
    {
        const double ms = measureDsReleaseMs (rel);
        r.row ({ tool::fmt (rel, 0), tool::fmt (ms, 1), tool::fmt (100.0 * (ms / rel - 1.0), 1) + " %" });
    }
    r.endTable();

    r.h3 ("CPU per module");
    r.table ({ "Configuration", "% of one core" });
    DsParams one;
    DsParams two;
    two.lookahead = 2;
    two.band[0].thresh = 0;
    two.band[1].on = true;
    r.row ({ "One band", tool::fmt (cpuPercent ([&] { return makeModule<DeEsserModule> (one); }, quick), 2) });
    r.row ({ "Two bands, 2 ms lookahead", tool::fmt (cpuPercent ([&] { return makeModule<DeEsserModule> (two); }, quick), 2) });
    r.endTable();

    r.h2 ("Notes");
    r.para ("Timing figures are 10-90% rises of the gain-reduction step response. The RMS COMP types (OPT, SOFT) have a two-pole window of about 0.075 x the "
            "attack time per pole; its ripple at twice the signal frequency shows in the gain-reduction ripple table, and is largest for bass. "
            "The auto make-up is fitted on pink noise at the default timing and is not a bass-specific correction.");
}

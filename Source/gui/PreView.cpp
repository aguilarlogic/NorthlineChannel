#include "PreView.h"
#include "ViewUtil.h"
#include "../dsp/Filters.h"

namespace nl
{
namespace
{
constexpr double kMeasureHz = 1000.0;
constexpr float kBarFloorDbc = -60.0f;
constexpr float kFilterTopDb = 6.0f, kFilterBottomDb = -36.0f;
} // namespace

void computeTransferCurve (const PreParams& pp, double fs, TransferCurve& out)
{
    const int type = std::clamp (pp.satType, 0, shaper::numTypes - 1);
    const auto d = sat::driveSetting (sat::calibration (false), type, shaper::driveDb (type, pp.drive), (double) pp.opLevel,
                                      pp.satAutoGain ? 1.0 : 0.0);
    sat::Coefs cf;
    cf.configure (type, fs, sat::kEcoOrder, sat::makeDroopFir (sat::kEcoOrder, fs, fs));
    cf.setTone (pp.satTone * 0.01f);
    cf.setTrim (d.lsTrim, d.hsTrim);
    cf.biasScale = d.biasScale;

    sat::Ramp ramp;
    ramp.g = std::pow (10.0, d.gInDb / 20.0);
    ramp.dg = 0.0;
    ramp.go = std::pow (10.0, d.gOutDb / 20.0);
    ramp.dgo = 0.0;

    // Measure over 20 whole cycles of 1 kHz after two cycles' worth of settling, so the amplitude is exact.
    const int m = juce::jmax (1, (int) std::lround (20.0 * fs / kMeasureHz));
    const int total = 3 * m;
    std::vector<float> x ((size_t) total), y ((size_t) total);
    for (int k = 0; k < TransferCurve::kPoints; ++k)
    {
        const double inDb = -60.0 + (double) k;
        const double amp = std::pow (10.0, inDb / 20.0);
        for (int i = 0; i < total; ++i)
            x[(size_t) i] = (float) (amp * std::sin (2.0 * kPiD * kMeasureHz * i / fs));

        sat::State st; // a fresh engine state per point
        st.reset (cf);
        sat::updateBias (cf, st);
        sat::process (st, cf, x.data(), y.data(), total, ramp);

        double re = 0.0, im = 0.0;
        for (int i = total - m; i < total; ++i)
        {
            const double w = 2.0 * kPiD * kMeasureHz * i / fs;
            re += (double) y[(size_t) i] * std::cos (w);
            im += (double) y[(size_t) i] * std::sin (w);
        }
        const double outAmp = 2.0 * std::hypot (re, im) / (double) m;
        out.inDb[k] = (float) inDb;
        out.outDb[k] = (float) std::max (-120.0, 20.0 * std::log10 (outAmp + 1.0e-12));
    }
}

void computeFilterCurveDb (const PreParams& pp, double fs, const double* freqHz, float* outDb, int n)
{
    SlopeFilter hp, lp;
    if (pp.hpOn)
    {
        hp.prepare (fs);
        hp.setParams (true, pp.hpFreq, pp.hpSlope, pp.hpRes);
        hp.reset();
        hp.updateControl();
    }
    if (pp.lpOn)
    {
        lp.prepare (fs);
        lp.setParams (false, pp.lpFreq, pp.lpSlope, pp.lpRes);
        lp.reset();
        lp.updateControl();
    }
    for (int i = 0; i < n; ++i)
    {
        float m = 1.0f;
        if (pp.hpOn) m *= hp.magnitude (freqHz[i]);
        if (pp.lpOn) m *= lp.magnitude (freqHz[i]);
        outDb[i] = juce::Decibels::gainToDecibels (m, -120.0f);
    }
}

//==============================================================================
PreDisplay::PreDisplay (NorthlineProcessor& p, HarmonicAnalyser& h) : proc (p), harmonics (h)
{
    filterDb.assign (256, 0.0f);
    startTimerHz (30);
}

PreDisplay::~PreDisplay() { stopTimer(); }

void PreDisplay::resized()
{
    auto r = getLocalBounds().toFloat();
    if (r.getWidth() >= 460.0f)
    {
        transferArea = r.removeFromLeft (r.getWidth() * 0.56f).reduced (0.0f, 0.0f);
        auto right = r.reduced (6.0f, 0.0f);
        harmArea = right.removeFromTop (right.getHeight() * 0.5f);
        filterArea = right;
    }
    else
    {
        transferArea = r.removeFromTop (r.getHeight() * 0.56f);
        harmArea = r.removeFromLeft (r.getWidth() * 0.5f);
        filterArea = r;
    }
    harmArea = harmArea.reduced (2.0f, 2.0f);
    filterArea = filterArea.reduced (2.0f, 2.0f);
    transferArea = transferArea.reduced (2.0f, 2.0f);
}

void PreDisplay::recomputeCurves (const PreParams& pp)
{
    const double fs = proc.getStrip().getSampleRate() > 0.0 ? proc.getStrip().getSampleRate() : 48000.0;
    computeTransferCurve (pp, fs, transfer);
    std::array<double, 256> freqs {};
    for (int i = 0; i < 256; ++i) freqs[(size_t) i] = 20.0 * std::pow (1000.0, (double) i / 255.0);
    computeFilterCurveDb (pp, fs, freqs.data(), filterDb.data(), 256);
    lastPre = pp;
    haveCurves = true;
    lastRecompute = juce::Time::getMillisecondCounter();
}

void PreDisplay::timerCallback()
{
    const auto pp = readPreParams (view::snapshot (proc));
    const bool changed = ! haveCurves || ! (pp == lastPre);
    const auto now = juce::Time::getMillisecondCounter();
    // The transfer curve costs a few milliseconds, so while a knob is being dragged it is recomputed at about 12 Hz.
    if (changed && (now - lastRecompute >= 80u || ! haveCurves))
    {
        recomputeCurves (pp);
        repaint();
    }
    else if (changed)
    {
        pendingRecompute = true;
    }
    if (pendingRecompute && now - lastRecompute >= 80u)
    {
        pendingRecompute = false;
        recomputeCurves (pp);
        repaint();
    }

    std::array<float, 1 + spectrum::kHarmonics> lv {};
    if (harmonics.read (lv.data()))
    {
        bool moved = ! haveLevels;
        for (size_t i = 0; i < lv.size(); ++i) moved = moved || std::abs (lv[i] - levels[i]) > 0.05f;
        if (moved)
        {
            levels = lv;
            haveLevels = true;
            repaint (harmArea.toNearestInt());
        }
    }
}

void PreDisplay::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    paintTransfer (g, transferArea);
    paintHarmonics (g, harmArea);
    paintFilters (g, filterArea);
}

void PreDisplay::paintTransfer (juce::Graphics& g, juce::Rectangle<float> a) const
{
    g.setFont (NorthlineLookAndFeel::font (10.0f));
    auto plot = a.reduced (18.0f, 14.0f).withTrimmedBottom (4.0f);
    auto xFor = [&] (float db) { return plot.getX() + plot.getWidth() * (db + 60.0f) / 60.0f; };
    auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * (db + 60.0f) / 60.0f; };

    g.setColour (colours::edge);
    for (float db : { -60.0f, -40.0f, -20.0f, 0.0f })
    {
        g.drawVerticalLine ((int) xFor (db), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
    }
    g.setColour (colours::faint);
    g.drawText ("SATURATION TRANSFER", a.removeFromTop (13.0f).reduced (4.0f, 0.0f), juce::Justification::left);
    g.drawText ("in dBFS", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 1.0f, 60.0f, 11.0f), juce::Justification::left);
    g.drawText ("out", juce::Rectangle<float> (plot.getX() + 3.0f, plot.getY() + 2.0f, 30.0f, 11.0f), juce::Justification::left);

    g.setColour (colours::dim.withAlpha (0.45f));
    g.drawLine (xFor (-60.0f), yFor (-60.0f), xFor (0.0f), yFor (0.0f), 1.0f);

    if (! haveCurves) return;
    juce::Path curve;
    for (int i = 0; i < TransferCurve::kPoints; ++i)
    {
        const float x = xFor (transfer.inDb[i]), y = juce::jlimit (plot.getY() - 4.0f, plot.getBottom() + 4.0f, yFor (juce::jmax (-60.0f, transfer.outDb[i])));
        if (i == 0) curve.startNewSubPath (x, y);
        else curve.lineTo (x, y);
    }
    const bool on = lastPre.satOn;
    g.setColour (on ? colours::pre : colours::faint);
    g.strokePath (curve, juce::PathStrokeType (1.8f));
    if (! on)
    {
        g.setColour (colours::faint);
        g.drawText ("SATURATION OFF (curve shows the engine at these settings)", plot, juce::Justification::centred);
    }
}

void PreDisplay::paintHarmonics (juce::Graphics& g, juce::Rectangle<float> a) const
{
    g.setColour (colours::faint);
    g.setFont (NorthlineLookAndFeel::font (10.0f));
    const auto title = a.removeFromTop (13.0f).reduced (4.0f, 0.0f);
    g.drawText ("HARMONICS (dBc)", title, juce::Justification::left);
    const float fund = levels[0];
    if (! haveLevels || fund < -80.0f)
    {
        g.setColour (colours::faint);
        g.drawText ("no signal", a, juce::Justification::centred);
        return;
    }
    g.setColour (colours::dim);
    g.drawText ("f0 " + juce::String (fund, 1) + " dBFS", title, juce::Justification::right);
    a.removeFromTop (12.0f); // the value row above the bars

    const float barW = (a.getWidth() - 12.0f) / (float) spectrum::kHarmonics;
    for (int h = 0; h < spectrum::kHarmonics; ++h)
    {
        const float lvl = juce::jlimit (kBarFloorDbc, 0.0f, levels[(size_t) h + 1]);
        auto cell = juce::Rectangle<float> (a.getX() + 6.0f + barW * (float) h, a.getY() + 4.0f, barW, a.getHeight() - 22.0f);
        const float fill = cell.getHeight() * (lvl - kBarFloorDbc) / -kBarFloorDbc;
        g.setColour (colours::track);
        g.fillRoundedRectangle (cell.withWidth (barW - 6.0f), 2.0f);
        g.setColour (colours::pre.withAlpha (0.9f));
        g.fillRoundedRectangle (cell.withWidth (barW - 6.0f).withTop (cell.getBottom() - fill), 2.0f);
        g.setColour (colours::text);
        g.setFont (NorthlineLookAndFeel::font (10.0f));
        g.drawText (juce::String (juce::roundToInt (levels[(size_t) h + 1])), juce::Rectangle<float> (cell.getX() - 2.0f, cell.getY() - 13.0f, barW, 12.0f), juce::Justification::centred);
        g.setColour (colours::faint);
        g.drawText ("H" + juce::String (h + 2), juce::Rectangle<float> (cell.getX() - 2.0f, cell.getBottom() + 2.0f, barW, 12.0f), juce::Justification::centred);
    }
}

void PreDisplay::paintFilters (juce::Graphics& g, juce::Rectangle<float> a) const
{
    g.setColour (colours::faint);
    g.setFont (NorthlineLookAndFeel::font (10.0f));
    g.drawText ("HP / LP", a.removeFromTop (13.0f).reduced (4.0f, 0.0f), juce::Justification::left);
    auto plot = a.reduced (4.0f, 4.0f);
    auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yFor = [&] (float db) { return plot.getY() + plot.getHeight() * (kFilterTopDb - db) / (kFilterTopDb - kFilterBottomDb); };

    g.setColour (colours::edge.withAlpha (0.6f));
    for (float hz : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) xFor (hz), plot.getY(), plot.getBottom());
    g.drawHorizontalLine ((int) yFor (0.0f), plot.getX(), plot.getRight());

    if (! lastPre.hpOn && ! lastPre.lpOn)
    {
        g.setColour (colours::faint);
        g.drawText ("filters off", plot, juce::Justification::centred);
        return;
    }
    juce::Path p;
    for (int i = 0; i < 256; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / 255.0);
        const float x = xFor (hz), y = juce::jlimit (plot.getY(), plot.getBottom(), yFor (juce::jmax (kFilterBottomDb, filterDb[(size_t) i])));
        if (i == 0) p.startNewSubPath (x, y);
        else p.lineTo (x, y);
    }
    g.setColour (colours::pre);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}
} // namespace nl

#include "Scopes.h"
#include "ViewUtil.h"
#include "../dsp/Dynamics.h"

namespace nl
{
namespace
{
constexpr int kCellsPerSecond = 200;                         // 5 ms grid
constexpr int kWindowCells = (int) (LevelScope::kWindowSeconds * kCellsPerSecond);
constexpr float kGrRangeDb = 20.0f;

juce::Font smallFont() { return NorthlineLookAndFeel::font (10.5f); }
} // namespace

//==============================================================================
LevelScope::LevelScope (NorthlineProcessor& p, Kind k) : proc (p), kind (k)
{
    auto& strip = p.getStrip();
    const int id = kind == Kind::Gate ? (int) modGate : (kind == Kind::Comp ? (int) modComp : (int) modDs);
    tapA = &strip.scope (id);
    if (kind == Kind::DeEss) tapB = &strip.deEsserBand2Scope();
    floorDb = kind == Kind::Gate ? -80.0f : -60.0f;
    setInterceptsMouseClicks (kind == Kind::Comp, false);
    startTimerHz (30);
}

LevelScope::~LevelScope() { stopTimer(); }

void LevelScope::timerCallback()
{
    const int before = histA.count + histB.count;
    drainTap (*tapA, histA);
    if (tapB != nullptr) drainTap (*tapB, histB);
    if (histA.count > 0) last = histA.ageOf (0);
    if (histA.count + histB.count != before) repaint();
}

void LevelScope::drainTap (ScopeTap& tap, History& h)
{
    std::array<ScopePoint, 64> buf {};
    for (;;)
    {
        const size_t got = tap.pop (buf.data(), buf.size());
        for (size_t i = 0; i < got; ++i) h.push (buf[i]);
        if (got < buf.size()) break;
    }
}

float LevelScope::yForDb (float db) const noexcept
{
    const float d = juce::jlimit (floorDb, 0.0f, db);
    return plot.getY() + plot.getHeight() * (0.0f - d) / (0.0f - floorDb);
}

float LevelScope::dbForY (float y) const noexcept
{
    return juce::jlimit (floorDb, 0.0f, -floorDb * (y - plot.getY()) / plot.getHeight());
}

void LevelScope::resized()
{
    auto r = getLocalBounds().toFloat();
    gr = kind == Kind::Gate ? juce::Rectangle<float>() : r.removeFromBottom (24.0f).reduced (2.0f, 4.0f);
    plot = r.reduced (4.0f, 2.0f);
    paintBackground();
}

void LevelScope::paintBackground()
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    background = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (background);
    g.fillAll (colours::background);
    g.setFont (smallFont());
    const std::initializer_list<float> ticks = kind == Kind::Gate ? std::initializer_list<float> { -20.0f, -40.0f, -60.0f, -80.0f }
                                                                  : std::initializer_list<float> { -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
    for (float db : ticks)
    {
        const float y = yForDb (db);
        g.setColour (colours::edge.withAlpha (0.55f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (colours::faint);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 2.0f, y - 11.0f, 30.0f, 11.0f), juce::Justification::left);
    }
    g.setColour (colours::edge);
    g.drawHorizontalLine ((int) yForDb (0.0f), plot.getX(), plot.getRight());
    g.setColour (colours::faint);
    g.drawText ("0", juce::Rectangle<float> (plot.getX() + 2.0f, plot.getY(), 20.0f, 11.0f), juce::Justification::left);
}

namespace ui
{
void stackLabelTops (std::vector<float>& tops, float height)
{
    for (size_t i = 1; i < tops.size(); ++i)
        tops[i] = juce::jmax (tops[i], tops[i - 1] + height);
}
} // namespace ui

void LevelScope::paintLines (juce::Graphics& g, float db, juce::Colour c, const juce::String& label, bool dashed)
{
    const float y = yForDb (db);
    if (y < plot.getY() - 0.5f || y > plot.getBottom() + 0.5f) return;
    g.setColour (c);
    if (dashed)
    {
        juce::Path line, dashes;
        line.startNewSubPath (plot.getX(), y);
        line.lineTo (plot.getRight(), y);
        const float pattern[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashes, line, pattern, 2);
        g.fillPath (dashes);
    }
    else
    {
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
    }
    pending.push_back ({ y, label, c });
}

void LevelScope::flushLabels (juce::Graphics& g)
{
    std::sort (pending.begin(), pending.end(), [] (const PendingLabel& a, const PendingLabel& b) { return a.y < b.y; });
    std::vector<float> tops;
    tops.reserve (pending.size());
    for (const auto& p : pending) tops.push_back (p.y - 12.0f);
    ui::stackLabelTops (tops, 12.0f);

    g.setFont (smallFont());
    for (size_t i = 0; i < pending.size(); ++i)
    {
        g.setColour (pending[i].colour);
        g.drawText (pending[i].text, juce::Rectangle<float> (plot.getRight() - 150.0f, tops[i], 148.0f, 11.0f), juce::Justification::right);
    }
    pending.clear();
}

void LevelScope::paintGrStrip (juce::Graphics& g, const History& h, juce::Colour c, juce::Rectangle<float> strip)
{
    juce::Path bars;
    const float cellW = plot.getWidth() / (float) (kWindowCells - 1);
    for (int j = 0; j < kWindowCells; ++j)
    {
        const int age = kWindowCells - 1 - j;
        if (age >= h.count) continue;
        const float depth = juce::jlimit (0.0f, 1.0f, -h.ageOf (age).grDb / kGrRangeDb) * strip.getHeight();
        if (depth > 0.2f) bars.addRectangle (plot.getX() + cellW * (float) j, strip.getY(), cellW + 0.6f, depth);
    }
    g.setColour (c.withAlpha (0.75f));
    g.fillPath (bars);
}

void LevelScope::paint (juce::Graphics& g)
{
    g.drawImageAt (background, 0, 0);
    pending.clear();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const float cellW = plot.getWidth() / (float) (kWindowCells - 1);
    auto xFor = [&] (int j) { return plot.getX() + cellW * (float) j; };

    auto buildPath = [&] (const History& h, float ScopePoint::*field)
    {
        juce::Path p;
        bool started = false;
        for (int j = 0; j < kWindowCells; ++j)
        {
            const int age = kWindowCells - 1 - j;
            if (age >= h.count) continue;
            const juce::Point<float> pt (xFor (j), yForDb (h.ageOf (age).*field));
            if (! started) { p.startNewSubPath (pt); started = true; }
            else p.lineTo (pt);
        }
        return p;
    };

    if (kind == Kind::Gate)
    {
        const float thresh = view::plain (proc, P::gateThresh), hyst = view::plain (proc, P::gateHyst), rangeDb = view::plain (proc, P::gateFloor);

        // Open spans: where the gate is not reducing the signal.
        juce::Path open;
        for (int j = 0; j < kWindowCells; ++j)
        {
            const int age = kWindowCells - 1 - j;
            if (age < histA.count && histA.ageOf (age).grDb > -1.0f) open.addRectangle (xFor (j), plot.getY(), cellW + 0.6f, plot.getHeight());
        }
        g.setColour (colours::gate.withAlpha (0.14f * alpha));
        g.fillPath (open);

        paintLines (g, rangeDb, colours::faint.withAlpha (0.8f), "RANGE " + juce::String (rangeDb, 0), false);
        paintLines (g, thresh - hyst, colours::gate.withAlpha (0.7f * alpha), "HYST " + juce::String (thresh - hyst, 1), true);
        paintLines (g, thresh, colours::gate.withAlpha (alpha), "THRESH " + juce::String (thresh, 1) + " dB", false);

        g.setColour (colours::gate.withAlpha (alpha));
        g.strokePath (buildPath (histA, &ScopePoint::keyDb), juce::PathStrokeType (1.6f));
    }
    else if (kind == Kind::Comp)
    {
        const float thresh = view::plain (proc, P::compThresh);
        g.setColour (colours::text.withAlpha (0.35f * alpha));
        g.strokePath (buildPath (histA, &ScopePoint::inDb), juce::PathStrokeType (1.0f));
        g.setColour (colours::comp.withAlpha (alpha));
        g.strokePath (buildPath (histA, &ScopePoint::outDb), juce::PathStrokeType (1.6f));
        paintLines (g, thresh, colours::comp.withAlpha (0.9f * alpha), "THRESH " + juce::String (thresh, 1) + " dB", false);

        paintGrStrip (g, histA, colours::comp, gr);
        g.setColour (colours::faint);
        g.setFont (smallFont());
        for (float v : { 6.0f, 20.0f })
        {
            const float y = gr.getY() + gr.getHeight() * v / kGrRangeDb;
            g.drawHorizontalLine ((int) y, gr.getRight() - 4.0f, gr.getRight());
            g.drawText (juce::String ((int) v), juce::Rectangle<float> (gr.getRight() - 24.0f, y - 5.0f, 20.0f, 11.0f), juce::Justification::right);
        }
    }
    else
    {
        const float t1 = view::plain (proc, P::ds1Thresh), t2 = view::plain (proc, P::ds2Thresh);
        const auto c1 = colours::ds, c2 = colours::ds.withMultipliedLightness (1.45f);
        g.setColour (c1.withAlpha (alpha));
        g.strokePath (buildPath (histA, &ScopePoint::keyDb), juce::PathStrokeType (1.5f));
        g.setColour (c2.withAlpha (alpha));
        g.strokePath (buildPath (histB, &ScopePoint::keyDb), juce::PathStrokeType (1.5f));
        paintLines (g, t1, c1.withAlpha (0.9f * alpha), "BAND 1 " + juce::String (t1, 1), false);
        paintLines (g, t2, c2.withAlpha (0.9f * alpha), "BAND 2 " + juce::String (t2, 1), true);
        paintGrStrip (g, histA, c1, gr.withHeight (gr.getHeight() * 0.5f));
        paintGrStrip (g, histB, c2, gr.withTop (gr.getY() + gr.getHeight() * 0.5f).withHeight (gr.getHeight() * 0.5f));
    }

    flushLabels (g);

    // Newest readout.
    if (histA.count > 0)
    {
        g.setFont (smallFont());
        g.setColour (colours::dim);
        juce::String text;
        if (kind == Kind::Comp) text = "IN " + juce::String (last.inDb, 1) + "  OUT " + juce::String (last.outDb, 1) + "  GR " + juce::String (last.grDb, 1);
        else if (kind == Kind::Gate) text = "KEY " + juce::String (last.keyDb, 1) + " dB";
        else text = "KEY " + juce::String (last.keyDb, 1) + " / " + juce::String (histB.count > 0 ? histB.ageOf (0).keyDb : -120.0f, 1) + " dB";
        figures::draw (g, text, plot.reduced (6.0f, 0.0f).removeFromTop (14.0f), juce::Justification::topRight, smallFont());
    }
}

void LevelScope::setCompThreshold (float db)
{
    auto* r = proc.param (P::compThresh);
    r->setValueNotifyingHost (r->convertTo0to1 (juce::jlimit (-50.0f, 0.0f, db)));
}

void LevelScope::mouseDown (const juce::MouseEvent& e)
{
    if (kind != Kind::Comp) return;
    dragging = true;
    dragStartDb = view::plain (proc, P::compThresh);
    proc.param (P::compThresh)->beginChangeGesture();
    if (! e.mods.isShiftDown()) setCompThreshold (dbForY (e.position.y));
}

void LevelScope::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    if (e.mods.isShiftDown())
        setCompThreshold (view::plain (proc, P::compThresh) + (dbForY (e.position.y) - view::plain (proc, P::compThresh)) * 0.1f);
    else
        setCompThreshold (dbForY (e.position.y));
}

void LevelScope::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    proc.param (P::compThresh)->endChangeGesture();
}

void LevelScope::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (kind != Kind::Comp) return;
    const float step = (e.mods.isShiftDown() ? 0.1f : 0.5f) * (w.deltaY > 0.0f ? 1.0f : -1.0f);
    proc.param (P::compThresh)->beginChangeGesture();
    setCompThreshold (view::plain (proc, P::compThresh) + step);
    proc.param (P::compThresh)->endChangeGesture();
}

//==============================================================================
CompCurve::CompCurve (NorthlineProcessor& p, const LevelScope& s) : proc (p), scope (s)
{
    startTimerHz (30);
}

CompCurve::~CompCurve() { stopTimer(); }

void CompCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::faint);
    g.setFont (NorthlineLookAndFeel::font (10.5f));
    g.drawText ("STATIC CURVE", r.removeFromTop (14.0f).reduced (6.0f, 0.0f), juce::Justification::left);

    auto plot = r.reduced (16.0f, 8.0f).withTrimmedBottom (10.0f);
    const float lo = -60.0f;
    auto xFor = [&] (float db) { return plot.getX() + plot.getWidth() * (db - lo) / -lo; };
    auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * (db - lo) / -lo; };

    g.setColour (colours::edge);
    for (float db : { -60.0f, -40.0f, -20.0f, 0.0f })
    {
        g.drawVerticalLine ((int) xFor (db), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
    }
    g.setColour (colours::dim.withAlpha (0.5f));
    g.drawLine (xFor (lo), yFor (lo), xFor (0.0f), yFor (0.0f), 1.0f);

    const float thresh = view::plain (proc, P::compThresh), ratio = view::plain (proc, P::compRatio), knee = view::plain (proc, P::compKnee);
    const float invRatio = ratio >= kRatioInf - 0.01f ? 0.0f : 1.0f / ratio;
    juce::Path curve;
    for (int i = 0; i <= 60; ++i)
    {
        const float x = lo + (float) i;
        const float y = juce::jlimit (lo, 0.0f, x + dyn::compressorGainDb (x, thresh, invRatio, knee));
        if (i == 0) curve.startNewSubPath (xFor (x), yFor (y));
        else curve.lineTo (xFor (x), yFor (y));
    }
    g.setColour (colours::comp);
    g.strokePath (curve, juce::PathStrokeType (1.8f));

    g.setColour (colours::comp.withAlpha (0.4f));
    g.drawVerticalLine ((int) xFor (thresh), plot.getY(), plot.getBottom());

    const auto& s = scope.latest();
    if (s.keyDb > -100.0f)
    {
        const float ox = xFor (juce::jlimit (lo, 0.0f, s.keyDb)), oy = yFor (juce::jlimit (lo, 0.0f, s.keyDb + s.grDb));
        g.setColour (colours::text);
        g.fillEllipse (ox - 4.0f, oy - 4.0f, 8.0f, 8.0f);
        g.setColour (colours::comp);
        g.fillEllipse (ox - 2.5f, oy - 2.5f, 5.0f, 5.0f);
    }
    g.setColour (colours::faint);
    g.setFont (NorthlineLookAndFeel::font (10.0f));
    g.drawText ("in dB", plot.withTop (plot.getBottom() + 1.0f).withHeight (10.0f), juce::Justification::centredRight);
}
} // namespace nl

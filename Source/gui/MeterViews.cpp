#include "MeterViews.h"

namespace nl
{
namespace
{
constexpr float kHoldFramesAt30Hz = 45.0f; // peak hold: 1.5 s
constexpr float kHoldFallDbPerFrame = 0.7f;
constexpr float kPeakFallDbPerFrame = 1.0f;

juce::Font label11 (bool bold = false) { return NorthlineLookAndFeel::font (11.0f, bold); }
} // namespace

juce::Colour levelMatchColour (LevelMatchStatus::State state)
{
    switch (state)
    {
        case LevelMatchStatus::Learning: return colours::pre; // amber: a transient state
        case LevelMatchStatus::Learned: return colours::meterGreen;
        case LevelMatchStatus::Live: return colours::text;
        case LevelMatchStatus::Idle:
        default: return colours::dim;
    }
}

juce::Colour loudnessStatusColour (loudness::Status status)
{
    switch (status)
    {
        case loudness::Status::OnTarget: return colours::meterGreen;
        case loudness::Status::OffTarget: return colours::pre; // amber
        case loudness::Status::NoReading:
        default: return colours::dim;
    }
}

//==============================================================================
void LevelMeter::setLevels (float peakL, float peakR, float rmsL, float rmsR)
{
    const float pk[2] { peakL, peakR }, rm[2] { rmsL, rmsR };
    for (int c = 0; c < 2; ++c)
    {
        const float p = juce::Decibels::gainToDecibels (pk[c], -100.0f);
        peakDb[c] = p > peakDb[c] ? p : juce::jmax (p, peakDb[c] - kPeakFallDbPerFrame);
        rmsDb[c] = juce::Decibels::gainToDecibels (rm[c], -100.0f);
        if (p >= holdDb[c])
        {
            holdDb[c] = p;
            holdFrames[c] = (int) kHoldFramesAt30Hz;
        }
        else if (--holdFrames[c] <= 0)
        {
            holdDb[c] = juce::jmax (-100.0f, holdDb[c] - kHoldFallDbPerFrame);
        }
        if (pk[c] >= 1.0f) clip = true;
    }
    repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clip = false;
    holdDb[0] = holdDb[1] = -100.0f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (title.isNotEmpty())
    {
        g.setFont (label11 (true));
        g.setColour (colours::dim);
        g.drawText (title, r.removeFromBottom (12.0f), juce::Justification::centred);
    }
    auto led = r.removeFromTop (6.0f);
    g.setColour (clip ? colours::meterRed : colours::track);
    g.fillRoundedRectangle (led, 1.5f);
    r.removeFromTop (3.0f);

    const float gap = 3.0f, w = (r.getWidth() - gap) * 0.5f;
    auto toY = [&r] (float db) { return r.getBottom() - r.getHeight() * meter::levelFraction (db); };
    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> (r.getX() + (float) c * (w + gap), r.getY(), w, r.getHeight());
        g.setColour (colours::track);
        g.fillRect (bar);
        // Green to -9 dBFS, yellow to 0 dBFS, red above 0 dBFS (clip).
        auto segment = [&] (float lo, float hi, juce::Colour colour)
        {
            const float top = std::min (peakDb[c], hi);
            if (top <= lo) return;
            g.setColour (colour);
            g.fillRect (bar.withTop (toY (top)).withBottom (toY (lo)));
        };
        segment (meter::kLevelFloorDb, -9.0f, colours::meterGreen);
        segment (-9.0f, 0.0f, colours::meterYellow);
        segment (0.0f, meter::kLevelTopDb, colours::meterRed);
        g.setColour (colours::text.withAlpha (0.5f));
        g.fillRect (bar.getX(), toY (rmsDb[c]) - 1.0f, bar.getWidth(), 2.0f);
        g.setColour (colours::text);
        g.fillRect (bar.getX(), toY (holdDb[c]) - 0.5f, bar.getWidth(), 1.0f);
    }
}

void MeterScale::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    r.removeFromBottom (12.0f); // title row of the meter
    g.setFont (label11());
    g.setColour (colours::dim);
    for (float db : meter::kLevelTicksDb)
    {
        const float y = r.getBottom() - r.getHeight() * meter::levelFraction (db);
        g.drawText (db == 0.0f ? juce::String ("0") : juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, r.getWidth() - 4.0f, 12.0f), juce::Justification::centredRight);
        g.drawHorizontalLine ((int) y, r.getWidth() - 3.0f, r.getWidth());
    }
}

//==============================================================================
void GrMeter::setGr (float db)
{
    db = juce::jlimit (-meter::kGrRangeDb, 0.0f, db);
    if (std::abs (db - value) > 0.05f)
    {
        value = db;
        repaint();
    }
}

void GrMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto readout = r.removeFromLeft (44.0f);
    g.setFont (NorthlineLookAndFeel::font (12.0f, true));
    g.setColour (colours::text);
    g.drawText (value < -0.05f ? juce::String (value, 1) : juce::String ("0.0"), readout, juce::Justification::centredLeft);

    r.removeFromLeft (4.0f);
    auto bar = r.removeFromTop (10.0f);
    g.setColour (colours::track);
    g.fillRoundedRectangle (bar, 2.0f);
    const float frac = meter::grFraction (value);
    if (frac > 0.0f)
    {
        g.setColour (colours::master);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 2.0f);
    }
    g.setFont (label11());
    for (float t : meter::kGrTicksDb)
    {
        const float x = bar.getX() + bar.getWidth() * (t / meter::kGrRangeDb);
        g.setColour (colours::text.withAlpha (0.45f));
        g.drawVerticalLine ((int) x, bar.getBottom(), bar.getBottom() + 3.0f);
        g.setColour (colours::dim);
        const float lx = juce::jlimit (0.0f, (float) getWidth() - 20.0f, x - 10.0f); // keep the last tick label on the panel
        g.drawText (juce::String ((int) t), juce::Rectangle<float> (lx, bar.getBottom() + 2.0f, 20.0f, 12.0f), juce::Justification::centred);
    }
}

//==============================================================================
void VuFace::setValue (float vu)
{
    if (std::abs (vu - value) > 0.01f)
    {
        value = vu;
        repaint();
    }
}

void VuFace::setLabel (const juce::String& s)
{
    if (s != label)
    {
        label = s;
        repaint();
    }
}

void VuFace::setReference (float opLevelDbfs)
{
    if (std::abs (opLevelDbfs - reference) > 0.01f)
    {
        reference = opLevelDbfs;
        repaint();
    }
}

void VuFace::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto caption = r.removeFromBottom (14.0f);
    g.setColour (colours::track);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

    const auto pivot = juce::Point<float> (r.getCentreX(), r.getBottom() + r.getHeight() * 0.35f);
    const float len = r.getHeight() * 1.12f;
    auto angleFor = [] (float vu) { return juce::jmap (juce::jlimit (-20.0f, 3.0f, vu), -20.0f, 3.0f, -0.78f, 0.78f); };
    g.setFont (label11());
    for (float m : { -20.0f, -10.0f, -5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float a = angleFor (m);
        g.setColour (colours::text.withAlpha (m > 0.0f ? 0.5f : 0.8f));
        g.drawLine ({ pivot.getPointOnCircumference (len * 0.86f, a), pivot.getPointOnCircumference (len * 0.94f, a) }, 1.3f);
        if (m == -20.0f || m == -10.0f || m == -5.0f || m == 0.0f || m == 3.0f)
            g.drawText (juce::String ((int) m), juce::Rectangle<float> (24.0f, 12.0f).withCentre (pivot.getPointOnCircumference (len * 0.74f, a)), juce::Justification::centred);
    }
    g.setColour (colours::text);
    g.drawLine ({ pivot.getPointOnCircumference (len * 0.25f, angleFor (value)), pivot.getPointOnCircumference (len * 0.97f, angleFor (value)) }, 1.6f);

    g.setFont (label11 (true));
    g.setColour (colours::dim);
    g.drawText (label, caption.removeFromLeft (caption.getWidth() * 0.5f), juce::Justification::centredLeft);
    g.setFont (label11());
    if (label.startsWith ("VU"))
        g.drawText ("0 VU = " + juce::String ((int) std::round (reference)) + " dBFS", caption, juce::Justification::centredRight);
}

//==============================================================================
void GrHistoryView::setHistory (const float* pts, int n)
{
    count = juce::jlimit (0, kMaxPoints, n);
    for (int i = 0; i < count; ++i) points[(size_t) i] = pts[i];
    repaint();
}

void GrHistoryView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::track.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 3.0f);
    const int cols = juce::jmin ((int) r.getWidth(), (int) columns.size());
    meter::columnMinima (points.data(), count, columns.data(), cols);
    g.setColour (colours::master.withAlpha (0.85f));
    for (int c = 0; c < cols; ++c)
    {
        const float frac = meter::grFraction (columns[(size_t) c]);
        if (frac > 0.0f) g.fillRect (r.getX() + (float) c, r.getY(), 1.0f, r.getHeight() * frac);
    }
    g.setColour (colours::edge);
    for (float db : { -6.0f, -12.0f })
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * meter::grFraction (db)), r.getX(), r.getRight());
    g.setFont (label11());
    g.setColour (colours::dim);
    g.drawText ("GR, 10 s", r.reduced (4.0f, 0.0f).withTrimmedBottom (r.getHeight() - 14.0f), juce::Justification::topLeft);
}

//==============================================================================
void Vectorscope::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    if (trail.isNull() || trail.getWidth() != getWidth() || trail.getHeight() != getHeight())
    {
        trail = juce::Image (juce::Image::RGB, getWidth(), getHeight(), true); // opaque, so the fade can blend
        juce::Graphics g (trail);
        g.fillAll (colours::track);
    }
}

void Vectorscope::pushPairs (const ScopePair* pairs, size_t n)
{
    if (trail.isNull()) return;
    juce::Graphics g (trail);
    g.setColour (colours::track.withAlpha (0.12f)); // persistence: each update fades the trail by 12 %
    g.fillAll();
    g.setColour (colours::text.withAlpha (0.85f));
    const float cx = 0.5f * (float) trail.getWidth(), cy = 0.5f * (float) trail.getHeight();
    const float R = 0.5f * (float) juce::jmin (trail.getWidth(), trail.getHeight()) - 2.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const auto p = meter::scopePoint (pairs[i].l, pairs[i].r);
        g.fillRect (juce::Rectangle<float> (cx + p.x * R, cy - p.y * R, 1.0f, 1.0f));
    }
    repaint();
}

void Vectorscope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::track.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 4.0f);
    if (! trail.isNull()) g.drawImageAt (trail, 0, 0);

    // Graticule: circle, mid and side axes, and the L and R axes on the diagonals.
    const float cx = r.getCentreX(), cy = r.getCentreY(), R = 0.5f * juce::jmin (r.getWidth(), r.getHeight()) - 2.0f;
    const float d = R * 0.70710678f;
    g.setColour (colours::edge);
    g.drawEllipse (cx - R, cy - R, 2.0f * R, 2.0f * R, 1.0f);
    g.drawLine (cx - R, cy, cx + R, cy, 1.0f);
    g.drawLine (cx, cy - R, cx, cy + R, 1.0f);
    g.setColour (colours::faint);
    g.drawLine (cx - d, cy - d, cx + d, cy + d, 1.0f); // L
    g.drawLine (cx + d, cy - d, cx - d, cy + d, 1.0f); // R
}

//==============================================================================
void CorrelationBar::setValue (float v)
{
    v = juce::jlimit (-1.0f, 1.0f, v);
    if (std::abs (v - value) > 0.005f)
    {
        value = v;
        repaint();
    }
}

void CorrelationBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto labels = r.removeFromTop (12.0f);
    g.setFont (label11());
    g.setColour (colours::dim);
    g.drawText ("-1", labels, juce::Justification::centredLeft);
    g.drawText ("+1", labels, juce::Justification::centredRight);
    g.drawText ("CORR", labels, juce::Justification::centred);

    g.setColour (colours::track);
    g.fillRoundedRectangle (r, 2.0f);
    const float cx = r.getCentreX(), px = cx + value * r.getWidth() * 0.5f;
    g.setColour (value < 0.0f ? colours::meterYellow : colours::meterGreen);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, px), r.getY(), juce::jmax (cx, px), r.getBottom()));
    g.setColour (colours::text.withAlpha (0.5f));
    g.drawVerticalLine ((int) cx, r.getY(), r.getBottom());
}

//==============================================================================
void LevelMatchView::setStatus (const LevelMatchStatus& s)
{
    if (s.state != status.state || std::abs (s.progress - status.progress) > 0.005f
        || std::abs (s.gainDb - status.gainDb) > 0.05f || std::abs (s.learnedTrimDb - status.learnedTrimDb) > 0.05f)
    {
        status = s;
        repaint();
    }
}

void LevelMatchView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromTop (16.0f);
    g.setFont (NorthlineLookAndFeel::font (12.0f, true));
    g.setColour (levelMatchColour (status.state));
    g.drawText (levelmatch::text (status), text, juce::Justification::centredLeft);
    r.removeFromTop (4.0f);
    auto bar = r.removeFromTop (6.0f);
    g.setColour (colours::track);
    g.fillRoundedRectangle (bar, 2.0f);
    if (status.state == LevelMatchStatus::Learning)
    {
        g.setColour (colours::pre);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, status.progress)), 2.0f);
    }
    else if (status.state == LevelMatchStatus::Live || status.state == LevelMatchStatus::Learned)
    {
        // The applied gain, as a fraction of the +-12 dB trim range.
        g.setColour (levelMatchColour (status.state).withAlpha (0.8f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, std::abs (status.gainDb) / 12.0f)), 2.0f);
    }
}

//==============================================================================
void LoudnessReadouts::setValues (const Values& v, const loudness::Target& target)
{
    const bool same = std::abs (v.momentary - values.momentary) < 0.05 && std::abs (v.shortTerm - values.shortTerm) < 0.05
                      && std::abs (v.integrated - values.integrated) < 0.05 && std::abs (v.lra - values.lra) < 0.05
                      && std::abs (v.truePeakDb - values.truePeakDb) < 0.05 && std::abs (v.plr - values.plr) < 0.05
                      && v.overs == values.overs && target.integratedLufs == targetLufs && target.truePeakDb == ceilingDb && juce::String (target.name) == targetName;
    if (same) return;
    values = v;
    targetLufs = target.integratedLufs;
    ceilingDb = target.truePeakDb;
    targetName = target.name;
    repaint();
}

void LoudnessReadouts::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float colW = r.getWidth() * 0.5f, rowH = r.getHeight() * 0.25f;

    auto cell = [&] (int col, int row, const juce::String& name, const juce::String& value, const juce::String& unit, juce::Colour colour)
    {
        auto c = juce::Rectangle<float> (r.getX() + (float) col * colW, r.getY() + (float) row * rowH, colW, rowH).reduced (4.0f, 1.0f);
        g.setFont (label11 (true));
        g.setColour (colours::dim);
        g.drawText (name, c.removeFromTop (12.0f), juce::Justification::centredLeft);
        g.setFont (NorthlineLookAndFeel::font (15.0f, true));
        g.setColour (colour);
        const float vw = juce::GlyphArrangement::getStringWidthInt (NorthlineLookAndFeel::font (15.0f, true), value) + 2.0f;
        g.drawText (value, c.removeFromLeft (vw), juce::Justification::centredLeft);
        g.setFont (label11());
        g.setColour (colours::faint);
        g.drawText (unit, c, juce::Justification::centredLeft);
    };
    auto fmt = [] (double v, int dec) { return loudness::hasReading (v) ? juce::String (v, dec) : juce::String ("-inf"); };

    const auto iStatus = loudness::integratedStatus (values.integrated, targetLufs);
    const double diff = loudness::differenceLu (values.integrated, targetLufs);
    cell (0, 0, "MOMENTARY", fmt (values.momentary, 1), "LUFS", colours::text);
    cell (1, 0, "SHORT-TERM", fmt (values.shortTerm, 1), "LUFS", colours::text);
    cell (0, 1, "INTEGRATED", fmt (values.integrated, 1), "LUFS", loudnessStatusColour (iStatus));
    cell (1, 1, "LRA", juce::String (values.lra, 1), "LU", colours::text);
    cell (0, 2, "TRUE PEAK", fmt (values.truePeakDb, 1), "dBTP", loudness::truePeakOver (values.truePeakDb, ceilingDb) ? colours::meterRed : colours::text);
    cell (1, 2, "PLR", juce::String (values.plr, 1), "LU", colours::text);
    cell (0, 3, "OVERS", juce::String ((int) values.overs), "", values.overs > 0 ? colours::meterRed : colours::dim);
    cell (1, 3, "VS TARGET", loudness::hasReading (values.integrated) ? juce::String (diff, 1) : juce::String ("-"), "LU", loudnessStatusColour (iStatus));
}
} // namespace nl

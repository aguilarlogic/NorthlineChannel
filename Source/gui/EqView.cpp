#include "EqView.h"
#include "ViewUtil.h"
#include "../dsp/EqModule.h"

namespace nl
{
namespace
{
constexpr float kAnalyserFloorDb = -90.0f;
constexpr float kHandleRadius = 6.0f;

int bandOffset (int b) { return P::eqLowOn + b * P::kEqBandStride; }
const char* kBandNames[] { "L", "M", "T", "H" };
juce::Colour bandColour() { return colours::eq; }
} // namespace

//==============================================================================
float EqGeometry::xForFreq (double hz) const noexcept
{
    return width * (float) (std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz));
}

double EqGeometry::freqForX (float x) const noexcept
{
    const double t = juce::jlimit (0.0, 1.0, (double) x / (double) width);
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float EqGeometry::yForDb (float db) const noexcept
{
    return height * 0.5f * (1.0f - db / kRangeDb);
}

float EqGeometry::dbForY (float y) const noexcept
{
    return juce::jlimit (-kRangeDb, kRangeDb, kRangeDb * (1.0f - 2.0f * y / height));
}

juce::Point<float> handleFor (const EqGeometry& g, double freqHz, float gainDb) noexcept
{
    return { g.xForFreq (freqHz), g.yForDb (gainDb) };
}

BandValues valuesAt (const EqGeometry& g, juce::Point<float> p) noexcept
{
    return { g.freqForX (p.x), g.dbForY (p.y) };
}

float qAfterWheel (float q, float wheelDelta, bool fine) noexcept
{
    const float notches = juce::jlimit (-2.0f, 2.0f, wheelDelta * 4.0f);
    const float next = q * std::pow (2.0f, notches * (fine ? 0.05f : 0.25f));
    return juce::jlimit (0.2f, 10.0f, next);
}

//==============================================================================
EqView::EqView (NorthlineProcessor& p, EqAnalyser& a) : proc (p), analyser (a)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

EqView::~EqView() { stopTimer(); }

void EqView::setAnalyserShown (bool shown)
{
    analyserShown = shown;
    if (! shown) { preCurve.clear(); postCurve.clear(); holdCurve.clear(); }
    repaint();
}

EqGeometry EqView::geometry() const noexcept
{
    return { (float) juce::jmax (1, getWidth()), (float) juce::jmax (1, getHeight()) };
}

void EqView::resized()
{
    lastValues.clear(); // rebuild at the new size
    analyserFrames = ~uint32_t (0); // the analyser paths are built in pixels too, so rebuild them on the next tick
}

void EqView::timerCallback()
{
    std::vector<float> v;
    v.reserve (80);
    for (int i = P::eqOn; i <= P::eqHighDynExt; ++i) v.push_back (view::plain (proc, i));
    for (int b = 0; b < 4; ++b) v.push_back (std::round (proc.getStrip().getEqDynGainDb (b) * 20.0f));
    v.push_back ((float) getWidth());
    v.push_back ((float) getHeight());
    if (v != lastValues)
    {
        lastValues = std::move (v);
        rebuild();
        repaint();
    }

    if (analyserShown && analyser.isAttached())
    {
        const auto frames = analyser.frameCount();
        if (frames != analyserFrames)
        {
            analyserFrames = frames;
            rebuildAnalyser();
            repaint();
        }
    }
}

void EqView::rebuild()
{
    const auto snap = view::snapshot (proc);
    EqParams ep = readEqParams (snap);
    const bool eqOn = snap.on (P::eqOn);
    for (auto& bp : ep.band) bp.on = bp.on && eqOn;
    msMode = snap.choice (P::eqMode) == 1;

    const double fs = proc.getStrip().getSampleRate() > 0.0 ? proc.getStrip().getSampleRate() : 48000.0;
    const EqGeometry g = geometry();
    const int n = juce::jmax (16, (int) g.width);
    std::vector<double> freqs ((size_t) n), mid ((size_t) n), side ((size_t) n), liveMidDb ((size_t) n), liveSideDb ((size_t) n);
    for (int i = 0; i < n; ++i) freqs[(size_t) i] = g.freqForX (g.width * (float) i / (float) (n - 1));

    float dyn[4] {};
    for (int b = 0; b < 4; ++b) dyn[b] = eqOn ? proc.getStrip().getEqDynGainDb (b) : 0.0f;

    EqModule::magnitudeCurveDb (ep, fs, freqs.data(), mid.data(), n, 0, nullptr);
    EqModule::magnitudeCurveDb (ep, fs, freqs.data(), liveMidDb.data(), n, 0, dyn);
    if (msMode)
    {
        EqModule::magnitudeCurveDb (ep, fs, freqs.data(), side.data(), n, 1, nullptr);
        EqModule::magnitudeCurveDb (ep, fs, freqs.data(), liveSideDb.data(), n, 1, dyn);
    }

    auto toPath = [&] (const std::vector<double>& db)
    {
        juce::Path path;
        for (int i = 0; i < n; ++i)
        {
            const float x = g.width * (float) i / (float) (n - 1);
            const float y = juce::jlimit (-10.0f, g.height + 10.0f, g.yForDb ((float) db[(size_t) i]));
            if (i == 0) path.startNewSubPath (x, y);
            else path.lineTo (x, y);
        }
        return path;
    };
    staticMid = toPath (mid);
    liveMid = toPath (liveMidDb);
    staticSide = msMode ? toPath (side) : juce::Path();
    liveSide = msMode ? toPath (liveSideDb) : juce::Path();
    fillMid = liveMid;
    fillMid.lineTo (g.width, g.yForDb (0.0f));
    fillMid.lineTo (0.0f, g.yForDb (0.0f));
    fillMid.closeSubPath();

    for (int b = 0; b < 4; ++b)
    {
        const auto& bp = ep.band[b];
        handleOn[(size_t) b] = bp.on;
        handleQ[(size_t) b] = bp.q;
        handles[(size_t) b] = handleFor (g, bp.freq, bp.on ? bp.gain : 0.0f);
    }
}

void EqView::rebuildAnalyser()
{
    const EqGeometry g = geometry();
    auto toPath = [&] (const float* db, bool closed)
    {
        juce::Path path;
        for (int i = 0; i < spectrum::kDisplayBins; ++i)
        {
            const double fc = std::sqrt (spectrum::displayEdge (i) * spectrum::displayEdge (i + 1));
            const float x = g.xForFreq (fc);
            const float y = juce::jlimit (0.0f, g.height, g.height * (1.0f - (db[i] - kAnalyserFloorDb) / (0.0f - kAnalyserFloorDb)));
            if (i == 0) path.startNewSubPath (x, y);
            else path.lineTo (x, y);
        }
        if (closed) { path.lineTo (g.width, g.height); path.lineTo (0.0f, g.height); path.closeSubPath(); }
        return path;
    };

    std::array<float, 2 * spectrum::kDisplayBins> pre {}, post {};
    if (! analyser.readPre (pre.data()) || ! analyser.readPost (post.data()))
    {
        preCurve.clear(); postCurve.clear(); holdCurve.clear();
        return;
    }
    preCurve = toPath (pre.data(), false);
    postCurve = toPath (post.data(), false);
    holdCurve = toPath (post.data() + spectrum::kDisplayBins, false);
}

void EqView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const EqGeometry geo = geometry();
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 4.0f);

    g.setFont (NorthlineLookAndFeel::font (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = geo.xForFreq (f);
        g.setColour (colours::edge.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (colours::faint);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x + 2.0f, r.getBottom() - 13.0f, 30.0f, 12.0f), juce::Justification::left);
    }
    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        g.setColour (colours::edge.withAlpha (0.4f));
        g.drawHorizontalLine ((int) geo.yForDb (db), r.getX(), r.getRight());
    }
    g.setColour (colours::edge);
    g.drawHorizontalLine ((int) geo.yForDb (0.0f), r.getX(), r.getRight());

    if (analyserShown && analyser.isAttached())
    {
        g.setColour (colours::dim.withAlpha (0.12f));
        g.fillPath (preCurve);
        g.setColour (colours::dim.withAlpha (0.5f));
        g.strokePath (preCurve, juce::PathStrokeType (1.0f));
        g.setColour (colours::master.withAlpha (0.7f));
        g.strokePath (postCurve, juce::PathStrokeType (1.0f));
        g.setColour (colours::text.withAlpha (0.35f));
        g.strokePath (holdCurve, juce::PathStrokeType (1.0f));
        g.setColour (colours::faint);
        g.setFont (NorthlineLookAndFeel::font (10.0f));
        g.drawText ("PRE / POST  4.5 dB/oct  dBFS", r.reduced (6.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topRight);
    }

    g.setColour (colours::eq.withAlpha (0.35f));
    g.strokePath (staticMid, juce::PathStrokeType (1.0f));
    if (msMode)
    {
        g.setColour (colours::ds.withAlpha (0.35f));
        g.strokePath (staticSide, juce::PathStrokeType (1.0f));
    }
    g.setColour (colours::eq.withAlpha (0.14f));
    g.fillPath (fillMid);
    g.setColour (colours::eq);
    g.strokePath (liveMid, juce::PathStrokeType (1.8f));
    if (msMode)
    {
        g.setColour (colours::ds);
        g.strokePath (liveSide, juce::PathStrokeType (1.8f));
    }

    // Q rings (the half-bandwidth of a band is about 0.5 / Q octaves) and the handles.
    static constexpr float kRingRy = 8.0f;
    for (int b = 0; b < 4; ++b)
    {
        const auto h = handles[(size_t) b];
        const bool on = handleOn[(size_t) b];
        if (on)
        {
            const double f = geo.freqForX (h.x);
            const float rx = juce::jlimit (3.0f, geo.width * 0.5f, std::abs (geo.xForFreq (f * std::pow (2.0, 0.5 / handleQ[(size_t) b])) - h.x));
            g.setColour (bandColour().withAlpha (0.45f));
            g.drawEllipse (h.x - rx, h.y - kRingRy, 2.0f * rx, 2.0f * kRingRy, 1.0f);
        }
        g.setColour (on ? bandColour() : colours::faint);
        g.fillEllipse (h.x - kHandleRadius, h.y - kHandleRadius, 2.0f * kHandleRadius, 2.0f * kHandleRadius);
        g.setColour (juce::Colours::black);
        g.setFont (NorthlineLookAndFeel::font (9.0f, true));
        g.drawText (kBandNames[b], juce::Rectangle<float> (12.0f, 12.0f).withCentre (h), juce::Justification::centred);
    }
}

//==============================================================================
int EqView::nearestHandle (juce::Point<float> p, float maxDistance) const noexcept
{
    int best = -1;
    float bestD = maxDistance;
    for (int b = 0; b < 4; ++b)
    {
        const float d = handles[(size_t) b].getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqView::beginBand (int b)
{
    const int o = bandOffset (b);
    if (view::plain (proc, o) < 0.5f)
    {
        proc.param (o)->beginChangeGesture();
        view::setPlain (proc, o, 1.0f);
        proc.param (o)->endChangeGesture();
    }
    proc.param (o + 2)->beginChangeGesture();
    proc.param (o + 3)->beginChangeGesture();
}

void EqView::endBand (int b)
{
    const int o = bandOffset (b);
    proc.param (o + 2)->endChangeGesture();
    proc.param (o + 3)->endChangeGesture();
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    dragBand = nearestHandle (e.position, 14.0f);
    if (dragBand >= 0) beginBand (dragBand);
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    const int o = bandOffset (dragBand);
    const auto v = valuesAt (geometry(), e.position);
    double freq = v.freqHz;
    float gain = v.gainDb;
    if (e.mods.isShiftDown())
    {
        // Fine: move a tenth of the way to the pointer on each event.
        const double f0 = view::plain (proc, o + 2);
        const float g0 = view::plain (proc, o + 3);
        freq = f0 + (freq - f0) * 0.1;
        gain = g0 + (gain - g0) * 0.1f;
    }
    view::setPlain (proc, o + 2, juce::jlimit (20.0f, 20000.0f, (float) freq));
    view::setPlain (proc, o + 3, juce::jlimit (-18.0f, 18.0f, gain));
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    if (dragBand < 0) return;
    endBand (dragBand);
    dragBand = -1;
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nearestHandle (e.position, 14.0f);
    if (b < 0) return;
    const int o = bandOffset (b) + 3;
    proc.param (o)->beginChangeGesture();
    view::setPlain (proc, o, 0.0f);
    proc.param (o)->endChangeGesture();
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = nearestHandle (e.position, 24.0f);
    if (b < 0) return;
    const int o = bandOffset (b) + 4;
    const float q = qAfterWheel (view::plain (proc, o), w.deltaY, e.mods.isShiftDown());
    proc.param (o)->beginChangeGesture();
    view::setPlain (proc, o, q);
    proc.param (o)->endChangeGesture();
}
} // namespace nl

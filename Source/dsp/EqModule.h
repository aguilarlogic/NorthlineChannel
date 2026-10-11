#pragma once

#include "ParamSnapshot.h"
#include "Filters.h"

namespace nl
{
struct EqBandParams
{
    bool on = true;
    int type = 0, target = 0;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
};

struct EqParams
{
    EqBandParams band[4];
};

// Four-band EQ: LOW (shelf / resonant shelf / bell), MID and TONE (wide /
// narrow / parametric bells) and HIGH (shelf / resonant shelf / bell). Each
// band can target both channels, or only mid (L) or side (R). Bands at 0 dB
// cost nothing.
class EqModule
{
public:
    enum Band { low = 0, mid, tone, high };

    static SvfCoefs bandCoefs (int band, int type, double fs, float freq, float gainDb, float q)
    {
        switch (band)
        {
            case low:
                if (type == 0) return makeSvf (SvfType::LowShelf, fs, freq, 0.7071f, gainDb);
                if (type == 1) return makeSvf (SvfType::LowShelf, fs, freq, 1.5f, gainDb);
                return makeSvf (SvfType::Bell, fs, freq, q, gainDb);
            case high:
                if (type == 0) return makeSvf (SvfType::HighShelf, fs, freq, 0.7071f, gainDb);
                if (type == 1) return makeSvf (SvfType::HighShelf, fs, freq, 1.5f, gainDb);
                return makeSvf (SvfType::Bell, fs, freq, q, gainDb);
            case mid:
                if (type == 0) return makeSvf (SvfType::Bell, fs, freq, 0.55f, gainDb);
                if (type == 1) return makeSvf (SvfType::Bell, fs, freq, 2.5f, gainDb);
                return makeSvf (SvfType::Bell, fs, freq, q, gainDb);
            case tone:
            default:
                // Wide TONE uses proportional Q: it narrows as gain increases.
                if (type == 0) return makeSvf (SvfType::Bell, fs, freq, 0.8f + 0.08f * std::abs (gainDb), gainDb);
                if (type == 1) return makeSvf (SvfType::Bell, fs, freq, 2.5f, gainDb);
                return makeSvf (SvfType::Bell, fs, freq, q, gainDb);
        }
    }

    // Latency in samples. Must depend only on non-automatable settings (quality,
    // lookahead), never on whether the module is on or on automatable parameters.
    int latency() const noexcept { return 0; }
    // Current dynamic gain of one band in dB (0 when the band is static), for the display.
    float getDynGainDb (int /*band*/) const noexcept { return 0.0f; } // TODO(eq)

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : bands)
        {
            b.freq.prepare (fs, 40.0f);
            b.gain.prepare (fs, 40.0f);
            b.q.prepare (fs, 40.0f);
        }
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            b.st[0].reset();
            b.st[1].reset();
            b.freq.snap(); b.gain.snap(); b.q.snap();
            b.dirty = true;
        }
        counter = 0;
    }

    void setParams (const EqParams& p)
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto& bp = p.band[i];
            auto& b = bands[i];
            b.freq.setTarget (std::log (bp.freq));
            b.gain.setTarget (bp.on ? bp.gain : 0.0f);
            b.q.setTarget (std::log (bp.q));
            if (bp.type != b.type) { b.type = bp.type; b.dirty = true; }
            b.target = bp.target;
        }
    }

    void process (float* const* ch, int numCh, int n)
    {
        int i = 0;
        while (i < n)
        {
            const int phase = counter % kControlBlock;
            if (phase == 0) updateControl();
            const int len = std::min (n - i, kControlBlock - phase);

            for (auto& b : bands)
            {
                if (! b.active) continue;
                for (int c = 0; c < numCh; ++c)
                {
                    if (numCh > 1 && b.target != 0 && b.target - 1 != c) continue;
                    float* x = ch[c] + i;
                    auto& st = b.st[c];
                    const auto& co = b.c;
                    for (int k = 0; k < len; ++k) x[k] = st.process (x[k], co);
                }
            }
            i += len;
            counter += len;
        }
    }

private:
    struct BandState
    {
        Smoothed freq, gain, q;
        SvfCoefs c;
        SvfState st[2];
        int type = 0, target = 0;
        bool dirty = true, active = false;
    };

    void updateControl()
    {
        for (int i = 0; i < 4; ++i)
        {
            auto& b = bands[i];
            const bool moved = b.freq.tick() | b.gain.tick() | b.q.tick();
            const bool nowActive = std::abs (b.gain.current) > 1.0e-3f;
            if (! nowActive)
            {
                if (b.active) { b.st[0].reset(); b.st[1].reset(); }
                b.active = false;
                continue;
            }
            if (! b.active) b.dirty = true;
            b.active = true;
            if (moved || b.dirty)
            {
                b.c = bandCoefs (i, b.type, fs, std::exp (b.freq.current), b.gain.current, std::exp (b.q.current));
                b.dirty = false;
            }
        }
    }

    double fs = 48000.0;
    BandState bands[4];
    int counter = 0;
};
// Maps the parameter snapshot to EQ settings (owned by this module).
inline EqParams readEqParams (const ParamSnapshot& p)
{
    EqParams ep;
    const bool ms = p.choice (P::eqMode) == 1;
    for (int b = 0; b < 4; ++b)
    {
        const int o = P::eqLowOn + b * P::kEqBandStride;
        ep.band[b].on = p.on (o);
        ep.band[b].type = p.choice (o + 1);
        ep.band[b].freq = p.f (o + 2);
        ep.band[b].gain = p.f (o + 3);
        ep.band[b].q = p.f (o + 4);
        ep.band[b].target = ms ? p.choice (o + 5) : 0;
    }
    return ep;
}
} // namespace nl

#pragma once

#include "EqDesign.h"
#include "EqDynamics.h"
#include "ParamSnapshot.h"

namespace nl
{
struct EqBandParams
{
    bool on = true;
    int type = 0, target = 0;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    // Dynamic band: extra gain `dynRange` dB is applied as the band level rises from `dynThresh`
    // to 12 dB above it (negative range = dynamic cut). `dynSpeed`: 0 Fast, 1 Medium, 2 Slow.
    bool dyn = false, dynExt = false;
    float dynThresh = -30.0f, dynRange = -6.0f;
    int dynSpeed = 1;
};

struct EqParams
{
    EqBandParams band[4];
    int character = 0; // 0 Clean (constant Q), 1 Proportional (Q narrows with |gain|)
    int solo = 0;      // 0 off, 1..4 = audition that band's region (transient)
};

// Four-band EQ: LOW (shelf / resonant shelf / bell), MID and TONE (wide / narrow / parametric
// bells) and HIGH (shelf / resonant shelf / bell). Each band can target both channels, or only
// mid (L) or side (R), and can be dynamic.
//
// Static bands are magnitude-matched second-order sections (EqDesign.h) run on the TPT SVF. Every
// parameter is smoothed at control rate (kControlBlock samples, on absolute sample indices), so
// the output does not depend on the host block size. Discrete changes (band type, target) and
// the band solo are cross-faded, never switched. Details and numbers: docs/dsp/eq.md.
class EqModule
{
public:
    enum Band { low = 0, mid, tone, high };
    enum Character { clean = 0, proportional };

    //==========================================================================
    // Band geometry shared by the DSP and the display.
    struct BandKind
    {
        eqd::Kind kind;
        float baseQ;
    };

    static BandKind kindOf (int band, int type, float q) noexcept
    {
        const float qc = std::clamp (q, 0.1f, 40.0f);
        switch (band)
        {
            case low:  return type == 0 ? BandKind { eqd::Kind::LowShelf, 0.7071f } : type == 1 ? BandKind { eqd::Kind::LowShelf, 1.5f } : BandKind { eqd::Kind::Bell, qc };
            case high: return type == 0 ? BandKind { eqd::Kind::HighShelf, 0.7071f } : type == 1 ? BandKind { eqd::Kind::HighShelf, 1.5f } : BandKind { eqd::Kind::Bell, qc };
            case mid:  return type == 0 ? BandKind { eqd::Kind::Bell, 0.55f } : type == 1 ? BandKind { eqd::Kind::Bell, 2.5f } : BandKind { eqd::Kind::Bell, qc };
            default:   return type == 0 ? BandKind { eqd::Kind::Bell, 0.8f } : type == 1 ? BandKind { eqd::Kind::Bell, 2.5f } : BandKind { eqd::Kind::Bell, qc };
        }
    }

    // Quality factor actually used for a total band gain. Clean: constant. Proportional (console
    // style): Q rises with |gain|, so cuts and boosts get narrower the harder they are pushed:
    //   bells   Q_eff = Q * (1 + |gain_dB| / 12)   (x2.5 at +-18 dB)
    //   shelves Q_eff = Q * (1 + |gain_dB| / 72)   (x1.25 at +-18 dB; shelf resonance stays tame)
    // `amount` blends between the two (0 = Clean, 1 = Proportional) and is smoothed.
    static double effectiveQ (const BandKind& bk, float totalGainDb, float amount) noexcept
    {
        const double slope = bk.kind == eqd::Kind::Bell ? 1.0 / 12.0 : 1.0 / 72.0;
        return (double) bk.baseQ * (1.0 + (double) amount * std::abs ((double) totalGainDb) * slope);
    }

    // Maximum total band gain (static + dynamic), dB.
    static constexpr float kMaxTotalGainDb = 30.0f;

    // SVF coefficients of one band at a total gain. Pure function of its arguments.
    static SvfCoefs designCoefs (const eqd::Context& ctx, int band, int type, float freq, float totalGainDb, float q, float amount) noexcept
    {
        const auto bk = kindOf (band, type, q);
        const float g = std::clamp (totalGainDb, -kMaxTotalGainDb, kMaxTotalGainDb);
        const eqd::Shape s { bk.kind, (double) freq, (double) g, effectiveQ (bk, g, amount) };
        return eqd::toCoefs (eqd::designBand (ctx, s).p);
    }

    // Previous bilinear design, kept only so older callers compile. Not used by the DSP.
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
                if (type == 0) return makeSvf (SvfType::Bell, fs, freq, 0.8f + 0.08f * std::abs (gainDb), gainDb);
                if (type == 1) return makeSvf (SvfType::Bell, fs, freq, 2.5f, gainDb);
                return makeSvf (SvfType::Bell, fs, freq, q, gainDb);
        }
    }

    //==========================================================================
    // Display support (UI thread safe: no state, no allocation). Response in dB of the static
    // design of all bands that act on `channel` (0 = Mid / L, 1 = Side / R), honouring targets.
    // `dynGainDb` (optional, 4 values, e.g. from getDynGainDb) adds each band's current dynamic gain.
    static void magnitudeCurveDb (const EqParams& p, double sampleRate, const double* freqs, double* outDb, int n, int channel,
                                  const float* dynGainDb = nullptr)
    {
        eqd::Context ctx;
        ctx.prepare (sampleRate);
        for (int i = 0; i < n; ++i) outDb[i] = 0.0;
        const float amount = p.character == proportional ? 1.0f : 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            const auto& bp = p.band[b];
            if (! bp.on || ! (bp.target == 0 || bp.target - 1 == channel)) continue;
            const float total = bp.gain + (dynGainDb != nullptr && bp.dyn ? dynGainDb[b] : 0.0f);
            if (std::abs (total) < 1.0e-3f) continue;
            const auto c = designCoefs (ctx, b, bp.type, bp.freq, total, bp.q, amount);
            for (int i = 0; i < n; ++i) outDb[i] += 20.0 * std::log10 (std::max (1.0e-9, eqd::svfMagnitude (c, freqs[i], sampleRate)));
        }
    }

    static float magnitudeDb (const EqParams& p, double sampleRate, double freqHz, int channel)
    {
        double out;
        magnitudeCurveDb (p, sampleRate, &freqHz, &out, 1, channel);
        return (float) out;
    }

    // Same with the current dynamic gains of the four bands added.
    static float magnitudeDb (const EqParams& p, double sampleRate, double freqHz, int channel, const float* dynGainDb)
    {
        double out;
        magnitudeCurveDb (p, sampleRate, &freqHz, &out, 1, channel, dynGainDb);
        return (float) out;
    }

    //==========================================================================
    // Latency in samples. Must depend only on non-automatable settings (quality,
    // lookahead), never on whether the module is on or on automatable parameters.
    int latency() const noexcept { return 0; }
    // Current dynamic gain of one band in dB (0 when the band is static), for the display.
    float getDynGainDb (int band) const noexcept { return bands[band].dynDb; }

    void prepare (double sampleRate, int /*maxBlock*/ = 4096)
    {
        fs = sampleRate;
        ctx.prepare (fs);
        charAmt.prepare (fs, 40.0f);
        fadeLen = (uint32_t) std::max (1.0, 0.015 * fs);
        soloStep = (float) (1.0 / std::max (1.0, 0.015 * fs));
        soloPreRoll = (uint32_t) std::max (1.0, 0.025 * fs);
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
            b.sh[0].reset();
            b.sh[1].reset();
            b.det.reset();
            b.freq.snap(); b.gain.snap(); b.q.snap();
            b.freqValid = false;
            b.dirty = b.tableDirty = true;
            b.configured = false;
            b.active = false;
            b.fading = false;
            b.dynDb = 0.0f;
            b.dynMode = false;
            b.dyn = false;
            b.range = 0.0f;
            b.speed = -1;
        }
        charAmt.snap();
        for (int b = 0; b < 4; ++b)
        {
            soloW[b] = pSolo == b + 1 ? 1.0f : 0.0f;
            regOn[b] = soloW[b] > 0.0f;
            soloPre[b] = 0;
            regSt[b][0].reset();
            regSt[b][1].reset();
        }
        soloTarget = pSolo;
        soloActive = false;
        clock.reset();
    }

    // Stores targets only; nothing advances here. Everything is consumed at control ticks.
    void setParams (const EqParams& p)
    {
        charAmt.setTarget (p.character == proportional ? 1.0f : 0.0f);
        pSolo = std::clamp (p.solo, 0, 4);
        for (int i = 0; i < 4; ++i)
        {
            const auto& bp = p.band[i];
            auto& b = bands[i];
            b.freq.setTarget (std::log (std::max (bp.freq, 1.0f)));
            b.gain.setTarget (bp.on ? bp.gain : 0.0f);
            b.q.setTarget (std::log (std::max (bp.q, 0.1f)));
            b.pType = bp.type;
            b.pTarget = bp.target;
            b.pDyn = bp.on && bp.dyn;
            b.pExt = bp.dynExt;
            b.pThresh = bp.dynThresh;
            b.pRange = std::clamp (bp.dynRange, -18.0f, 18.0f);
            b.pSpeed = bp.dynSpeed;
        }
    }

    void process (float* const* ch, int numCh, int n, const float* const* key = nullptr)
    {
        numCh = std::min (numCh, 2);
        int i = 0;
        while (i < n)
        {
            const int phase = (int) clock.phase;
            if (phase == 0) updateControl (numCh);
            const int len = std::min (n - i, kControlBlock - phase);
            processSegment (ch, numCh, i, len, key);
            clock.advance (len);
            i += len;
        }
    }

private:
    static constexpr int kNodes = 21; // dynamic gain-axis table: 1 dB steps over up to 18 dB

    struct BandState
    {
        Smoothed freq, gain, q; // ln Hz, dB, ln Q
        // Requested (written by setParams, consumed at control ticks).
        int pType = 0, pTarget = 0, pSpeed = 1;
        bool pDyn = false, pExt = false;
        float pThresh = -30.0f, pRange = -6.0f;
        // Active configuration.
        int type = 0, target = 0, speed = -1;
        bool configured = false, active = false, dirty = true, tableDirty = true;
        bool dyn = false, ext = false;
        float thresh = -30.0f, range = 0.0f;
        bool moved = false;
        SvfCoefs c;
        SvfState st[2];
        // Cross-fade of a discrete change: the old configuration keeps running in the shadow.
        bool fading = false;
        uint32_t fadePos = 0;
        int shTarget = 0;
        SvfCoefs shC;
        SvfState sh[2];
        // Dynamics.
        eqd::BandDetector det;
        float lnF = 0.0f, lnQ = 0.0f, freqHz = 1000.0f, qVal = 1.0f;
        bool freqValid = false;
        float dynDb = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
        bool dynMode = false;
        int nodes = 1;
        float nodeDb[kNodes] {};
        SvfCoefs table[kNodes];
    };

    static bool covers (int target, int c, int numCh) noexcept { return numCh == 1 || target == 0 || target - 1 == c; }

    inline float fadeWeight (uint32_t pos) const noexcept
    {
        const float t = pos >= fadeLen ? 1.0f : (float) pos / (float) fadeLen;
        return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f); // smootherstep: flat start and end, so a state mismatch is weighted by ~t^3
    }

    void updateControl (int numCh)
    {
        const bool charMoved = charAmt.tick();
        soloTarget = pSolo;
        for (int i = 0; i < 4; ++i) updateBand (i, numCh, charMoved);
        updateSolo();
    }

    void updateBand (int i, int numCh, bool charMoved)
    {
        auto& b = bands[i];
        b.moved = b.freq.tick() | b.gain.tick() | b.q.tick() | charMoved;

        if (b.fading && b.fadePos >= fadeLen) endFade (b, numCh);

        // Discrete changes: cross-fade from the old configuration, unless the band is idle.
        if (! b.configured)
        {
            b.type = b.pType;
            b.target = b.pTarget;
            b.configured = true;
            b.dirty = true;
        }
        else if ((b.pType != b.type || b.pTarget != b.target) && ! b.fading)
        {
            if (b.active)
            {
                b.shC = b.c;
                b.shTarget = b.target;
                b.sh[0] = b.st[0];
                b.sh[1] = b.st[1];
                b.fading = true;
                b.fadePos = 0;
            }
            b.type = b.pType;
            b.target = b.pTarget;
            b.dirty = true;
        }

        // Dynamic settings.
        const bool wasDyn = b.dyn;
        b.dyn = b.pDyn;
        b.ext = b.pExt;
        b.thresh = b.pThresh;
        if (b.range != b.pRange)
        {
            b.range = b.pRange;
            b.tableDirty = true;
            b.dynDb = b.range < 0.0f ? std::clamp (b.dynDb, b.range, 0.0f) : std::clamp (b.dynDb, 0.0f, b.range);
        }
        if (b.speed != b.pSpeed)
        {
            b.speed = b.pSpeed;
            const auto sp = eqd::dynSpeed (b.speed);
            b.attackCoef = timeCoef (sp.attackMs, fs / kControlBlock);
            b.releaseCoef = timeCoef (sp.releaseMs, fs / kControlBlock);
        }
        if (! b.dyn && wasDyn) b.det.reset();
        if (b.dyn) b.det.sanitize();

        // Hz and Q from the smoothed logs, recomputed only when those change (not every tick).
        if (! b.freqValid || b.freq.current != b.lnF || b.q.current != b.lnQ)
        {
            b.lnF = b.freq.current;
            b.lnQ = b.q.current;
            b.freqHz = std::exp (b.lnF);
            b.qVal = std::exp (b.lnQ);
            b.freqValid = true;
        }
        const float freq = b.freqHz, q = b.qVal;

        // Dynamic gain from the detector state (levels through the previous segment).
        if (b.dyn || b.dynMode)
        {
            float target = 0.0f;
            if (b.dyn) target = eqd::dynGainDb (b.det.levelDb (numCh, b.target), b.thresh, b.range);
            const float coef = std::abs (target) > std::abs (b.dynDb) ? b.attackCoef : b.releaseCoef;
            b.dynDb = target + (b.dynDb - target) * coef;
            if (target == 0.0f && std::abs (b.dynDb) < 1.0e-3f) b.dynDb = 0.0f;
            const bool nowDyn = b.dyn || b.dynDb != 0.0f;
            if (nowDyn != b.dynMode) { b.dynMode = nowDyn; b.dirty = true; b.tableDirty = true; }
        }

        if (b.dyn && (b.moved || b.dirty || ! wasDyn))
        {
            const auto bk = kindOf (i, b.type, q);
            b.det.setFilter (bk.kind == eqd::Kind::LowShelf ? eqd::Region::LowPass : bk.kind == eqd::Kind::HighShelf ? eqd::Region::HighPass : eqd::Region::BandPass,
                             fs, freq, std::clamp (bk.baseQ, 0.3f, 12.0f));
        }

        const bool alive = std::abs (b.gain.current) > 1.0e-3f || b.dynMode;
        if (! alive)
        {
            if (b.active)
            {
                b.st[0].reset();
                b.st[1].reset();
                if (b.fading) endFade (b, numCh);
            }
            b.active = false;
            return;
        }
        if (! b.active) { b.dirty = true; b.tableDirty = true; }
        b.active = true;

        if (b.dynMode)
        {
            if (b.moved || b.dirty || b.tableDirty) rebuildTable (i, b, freq, q);
            b.c = tableCoefs (b);
        }
        else if (b.moved || b.dirty)
        {
            b.c = designCoefs (ctx, i, b.type, freq, b.gain.current, q, charAmt.current);
        }
        b.dirty = b.tableDirty = false;
    }

    void endFade (BandState& b, int numCh)
    {
        b.fading = false;
        b.sh[0].reset();
        b.sh[1].reset();
        for (int c = 0; c < 2; ++c)
            if (! covers (b.target, c, numCh)) b.st[c].reset();
    }

    // Nodes at dynamic gains 0, 1, 2 ... |range| dB (the last one at |range|), each a full design
    // at static gain + dynamic gain. Node 0 is the static design itself, so a dynamic band at rest
    // is bit-identical to the same band without dynamics.
    void rebuildTable (int band, BandState& b, float freq, float q)
    {
        const float sgn = b.range < 0.0f ? -1.0f : 1.0f, aR = std::abs (b.range);
        int n = 1;
        b.nodeDb[0] = 0.0f;
        if (aR >= 0.01f)
        {
            const int whole = (int) std::floor (aR);
            for (int j = 1; j <= whole && n < kNodes - 1; ++j) b.nodeDb[n++] = (float) j;
            if (aR - (float) whole > 0.01f) b.nodeDb[n++] = aR;
        }
        b.nodes = n;
        for (int j = 0; j < n; ++j)
            b.table[j] = designCoefs (ctx, band, b.type, freq, b.gain.current + sgn * b.nodeDb[j], q, charAmt.current);
    }

    static SvfCoefs tableCoefs (const BandState& b) noexcept
    {
        const float u = std::min (std::abs (b.dynDb), b.nodeDb[b.nodes - 1]);
        if (b.nodes == 1 || u <= 0.0f) return b.table[0];
        int j = 0;
        while (j < b.nodes - 2 && u > b.nodeDb[j + 1]) ++j;
        const float t = (u - b.nodeDb[j]) / (b.nodeDb[j + 1] - b.nodeDb[j]);
        const SvfCoefs& a = b.table[j];
        const SvfCoefs& z = b.table[j + 1];
        SvfCoefs c;
        c.g = a.g + t * (z.g - a.g);
        c.k = a.k + t * (z.k - a.k);
        c.m0 = a.m0 + t * (z.m0 - a.m0);
        c.m1 = a.m1 + t * (z.m1 - a.m1);
        c.m2 = a.m2 + t * (z.m2 - a.m2);
        c.a1 = 1.0f / (1.0f + c.g * (c.g + c.k));
        c.a2 = c.g * c.a1;
        c.a3 = c.g * c.a2;
        return c;
    }

    void updateSolo()
    {
        soloActive = false;
        for (int b = 0; b < 4; ++b)
        {
            const bool want = soloTarget == b + 1;
            const bool on = want || soloW[b] > 0.0f;
            if (on)
            {
                const auto& bs = bands[b];
                const auto bk = kindOf (b, bs.type, std::exp (bs.q.current));
                regC[b] = eqd::makeRegionFilter (bk.kind == eqd::Kind::LowShelf ? eqd::Region::LowPass : bk.kind == eqd::Kind::HighShelf ? eqd::Region::HighPass : eqd::Region::BandPass,
                                                 fs, std::exp (bs.freq.current), std::clamp (bk.baseQ, 0.3f, 12.0f));
                if (! regOn[b]) soloPre[b] = soloPreRoll; // let the region filter settle on the running signal before it is faded in
                regOn[b] = true;
                soloActive = true;
            }
            else if (regOn[b])
            {
                regOn[b] = false;
                regSt[b][0].reset();
                regSt[b][1].reset();
            }
        }
    }

    void processSegment (float* const* ch, int numCh, int off, int len, const float* const* key)
    {
        bool anyDyn = false;
        for (auto& b : bands) anyDyn |= b.dyn;

        // The band solo needs the module input after the bands have run: keep a copy for it.
        float in[2][kControlBlock];
        if (soloActive)
            for (int c = 0; c < numCh; ++c) std::copy (ch[c] + off, ch[c] + off + len, in[c]);

        // Detectors run before any band changes the signal, so they read the module input (or the
        // external key) in place, not the band-processed signal.
        if (anyDyn)
            for (auto& b : bands)
            {
                if (! b.dyn) continue;
                const float* src[2] { ch[0] + off, ch[numCh > 1 ? 1 : 0] + off };
                if (b.ext && key != nullptr)
                    for (int c = 0; c < numCh; ++c)
                        if (key[c] != nullptr) src[c] = key[c] + off;
                if (numCh == 2 && b.target == 0) b.det.run2 (src[0], src[1], len);
                else
                    for (int c = 0; c < numCh; ++c)
                        if (covers (b.target, c, numCh)) b.det.run (c, src[c], len);
            }

        for (auto& b : bands)
        {
            if (! b.active) continue;
            if (! b.fading)
            {
                if (numCh == 2 && b.target == 0)
                {
                    // Both channels in one loop (independent recursions, see EqDynamics.h run2).
                    // Locals for the state and coefficients (see EqDynamics.h run): kept in registers.
                    float* x0 = ch[0] + off;
                    float* x1 = ch[1] + off;
                    const SvfCoefs co = b.c;
                    SvfState s0 = b.st[0], s1 = b.st[1];
                    for (int k = 0; k < len; ++k)
                    {
                        x0[k] = s0.process (x0[k], co);
                        x1[k] = s1.process (x1[k], co);
                    }
                    b.st[0] = s0;
                    b.st[1] = s1;
                }
                else
                {
                    for (int c = 0; c < numCh; ++c)
                    {
                        if (! covers (b.target, c, numCh)) continue;
                        float* x = ch[c] + off;
                        const SvfCoefs co = b.c;
                        SvfState s = b.st[c];
                        for (int k = 0; k < len; ++k) x[k] = s.process (x[k], co);
                        b.st[c] = s;
                    }
                }
            }
            else
            {
                for (int c = 0; c < numCh; ++c)
                {
                    const bool newCov = covers (b.target, c, numCh), oldCov = covers (b.shTarget, c, numCh);
                    if (! newCov && ! oldCov) continue;
                    float* x = ch[c] + off;
                    for (int k = 0; k < len; ++k)
                    {
                        const float w = fadeWeight (b.fadePos + (uint32_t) k);
                        const float xi = x[k];
                        const float yn = newCov ? b.st[c].process (xi, b.c) : xi;
                        const float yo = oldCov ? b.sh[c].process (xi, b.shC) : xi;
                        x[k] = yo + w * (yn - yo);
                    }
                }
                b.fadePos = std::min (b.fadePos + (uint32_t) len, fadeLen);
            }
        }

        if (soloActive)
        {
            for (int k = 0; k < len; ++k)
            {
                float wsum = 0.0f;
                for (int b = 0; b < 4; ++b)
                {
                    if (soloPre[b] > 0) --soloPre[b];
                    const float t = soloTarget == b + 1 && soloPre[b] == 0 ? 1.0f : 0.0f;
                    soloW[b] += std::clamp (t - soloW[b], -soloStep, soloStep);
                    wsum += soloW[b];
                }
                const float w0 = std::max (0.0f, 1.0f - wsum);
                for (int c = 0; c < numCh; ++c)
                {
                    float y = w0 * ch[c][off + k];
                    for (int b = 0; b < 4; ++b)
                        if (regOn[b]) y += soloW[b] * regSt[b][c].process (in[c][k], regC[b]);
                    ch[c][off + k] = y;
                }
            }
        }
    }

    double fs = 48000.0;
    eqd::Context ctx;
    BandState bands[4];
    Smoothed charAmt;
    ControlClock clock;
    uint32_t fadeLen = 720;
    // Band solo: per-band region filters and cross-fade weights.
    int pSolo = 0, soloTarget = 0;
    bool soloActive = false;
    bool regOn[4] {};
    uint32_t soloPre[4] {}, soloPreRoll = 1200;
    float soloW[4] {};
    float soloStep = 0.001f;
    SvfCoefs regC[4];
    SvfState regSt[4][2];
};

// Maps the parameter snapshot to EQ settings (owned by this module).
inline EqParams readEqParams (const ParamSnapshot& p)
{
    EqParams ep;
    const bool ms = p.choice (P::eqMode) == 1;
    ep.character = p.choice (P::eqChar);
    ep.solo = p.choice (P::eqSolo);
    for (int b = 0; b < 4; ++b)
    {
        const int o = P::eqLowOn + b * P::kEqBandStride;
        auto& bp = ep.band[b];
        bp.on = p.on (o);
        bp.type = p.choice (o + 1);
        bp.freq = p.f (o + 2);
        bp.gain = p.f (o + 3);
        bp.q = p.f (o + 4);
        bp.target = ms ? p.choice (o + 5) : 0;
        bp.dyn = p.on (o + 6);
        bp.dynThresh = p.f (o + 7);
        bp.dynRange = p.f (o + 8);
        bp.dynSpeed = p.choice (o + 9);
        bp.dynExt = p.on (o + 10);
    }
    return ep;
}
} // namespace nl



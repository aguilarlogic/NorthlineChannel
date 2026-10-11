#pragma once

#include "ParamSnapshot.h"
#include "Dynamics.h"
#include <atomic>

namespace nl
{
struct DsBandParams
{
    bool on = true;
    int mode = 0; // 0 split (dynamic EQ), 1 wide (wideband gain from the band detector)
    int type = 3; // 0 low shelf, 1 bell, 2 narrow, 3 high shelf
    float freq = 6000.0f, thresh = -20.0f, range = 12.0f;

    bool operator== (const DsBandParams&) const = default;
};

struct DsParams
{
    bool linked = true, relative = true, ext = false;
    int listen = 0;    // 0 off, 1 band 1, 2 band 2, 3 delta (what the de-esser removes)
    int lookahead = 0; // index into kLookaheadMs
    float attackMs = 0.4f, releaseMs = 60.0f, outDb = 0.0f;
    DsBandParams band[2];

    bool operator== (const DsParams&) const = default;
};

// Two-band de-esser. A fixed band filter feeds each band's detector. In Split mode the audio runs
// through a TPT SVF shelf/bell whose gain follows the detector (a dynamic EQ: only the band is
// reduced); in Wide mode the detector drives a wideband gain. At 0 dB the SVF output is exactly
// its input, so the module is bit-transparent until it acts.
//
// Attack and release are 10-90% times of the gain-reduction step response. With lookahead the
// detector sees the undelayed signal while the audio, the listen signals and the delta reference
// are delayed by the same amount; latency() is that delay whether or not the module is in use.
class DeEsserModule
{
public:
    int latency() const noexcept { return latencyN.load (std::memory_order_relaxed); }
    // Detector level of one band in dBFS (before the relative-threshold subtraction), for the scope.
    float getKeyLevelDb (int band) const noexcept { return keyDb[band]; }
    float getGainReductionDb (int band) const noexcept { return grDb[band]; }

    void prepare (double sampleRate, int /*maxBlock*/ = 4096)
    {
        fs = sampleRate;
        const int maxLa = dyn::maxLookaheadSamples (fs);
        for (auto& d : delay) d.prepare (maxLa);
        for (auto& b : bandDelay) for (auto& d : b) d.prepare (maxLa);
        fadeLen = (uint32_t) std::max (1.0, std::round (0.015 * fs));
        listenStep = (float) (1.0 / std::max (1.0, 0.005 * fs));
        for (int b = 0; b < 2; ++b)
        {
            threshSm[b].prepare (fs, 10.0f);
            rangeSm[b].prepare (fs, 10.0f);
            freqSm[b].prepare (fs, 20.0f);
        }
        outR.prepare (fs, 20.0f);
        havePending = false;
        setParams (params);
        reset();
    }

    void reset()
    {
        clock.reset();
        main = {};
        shadow = {};
        main.cfg = pending;
        shadow.cfg = pending;
        fade.stop();
        lookaheadN = pendingLookahead;
        listenTarget = pendingListen;
        listenKind = listenTarget;
        listenW = listenTarget != 0 ? 1.0f : 0.0f;
        for (int b = 0; b < 2; ++b) { threshSm[b].snap(); rangeSm[b].snap(); freqSm[b].snap(); }
        outR.snap();
        updateLive();
        for (int b = 0; b < 2; ++b) designBand (main, b);
        for (auto& d : delay) d.reset();
        for (auto& b : bandDelay) for (auto& d : b) d.reset();
        initialised = true;
        dirty = false;
        for (int b = 0; b < 2; ++b) { grDb[b] = 0.0f; keyDb[b] = -120.0f; }
    }

    // Derives coefficients and targets only; the audio sees them from the next control tick.
    void setParams (const DsParams& p)
    {
        if (havePending && p == params) return;
        params = p;
        havePending = true;
        derive();
        dirty = true;
    }

    void process (float* const* io, int numCh, int n, const float* const* key)
    {
        if (! initialised) reset();
        numCh = std::clamp (numCh, 1, 2);
        float grMin[2] { 0.0f, 0.0f }, lastKey[2] { keyDb[0], keyDb[1] };
        int i = 0;
        while (i < n)
        {
            if (clock.phase == 0) onTick();
            const int p0 = (int) clock.phase;
            const int len = std::min (n - i, kControlBlock - p0);

            const float* det[2] { io[0] + i, numCh > 1 ? io[1] + i : nullptr };
            const float* ext[2] { nullptr, nullptr };
            const bool useExt = main.cfg.ext && key != nullptr;
            if (useExt) for (int c = 0; c < numCh; ++c) ext[c] = key[c] + i;
            const float* const* extP = useExt ? ext : nullptr;

            for (int c = 0; c < numCh; ++c)
            {
                if (lookaheadN > 0)
                    for (int j = 0; j < len; ++j) dryBuf[c][j] = delay[c].process (det[c][j], (uint32_t) lookaheadN);
                else
                    for (int j = 0; j < len; ++j) dryBuf[c][j] = det[c][j];
            }

            if (fade.active)
            {
                float g2[2] { 0.0f, 0.0f }, k2[2] { 0.0f, 0.0f };
                runCore (shadow, det, extP, shBuf, shBand, numCh, len, g2, k2);
            }
            float segKey[2] { lastKey[0], lastKey[1] };
            runCore (main, det, extP, outBuf, bandBuf, numCh, len, grMin, segKey);
            lastKey[0] = segKey[0];
            lastKey[1] = segKey[1];

            if (fade.active)
            {
                for (int j = 0; j < len; ++j)
                {
                    const float w = fade.active ? fade.next() : 1.0f;
                    for (int c = 0; c < numCh; ++c) outBuf[c][j] = shBuf[c][j] + (outBuf[c][j] - shBuf[c][j]) * w;
                }
            }

            const int bandListen = listenKind == 1 || listenKind == 2 ? listenKind - 1 : -1;
            if (bandListen >= 0 && (listenTarget == listenKind || listenW > 0.0f))
                for (int c = 0; c < numCh; ++c)
                    for (int j = 0; j < len; ++j) listenAl[c][j] = bandDelay[bandListen][c].process (bandBuf[bandListen][c][j], (uint32_t) lookaheadN);

            const float lwTarget = listenTarget != 0 ? 1.0f : 0.0f;
            for (int j = 0; j < len; ++j)
            {
                const float og = outR.at (p0 + j);
                if (listenW != lwTarget)
                    listenW = listenW < lwTarget ? std::min (lwTarget, listenW + listenStep) : std::max (lwTarget, listenW - listenStep);
                for (int c = 0; c < numCh; ++c)
                {
                    float y = outBuf[c][j] * og;
                    if (listenW > 0.0f)
                    {
                        const float l = bandListen >= 0 ? listenAl[c][j] : dryBuf[c][j] - outBuf[c][j];
                        y += (l - y) * listenW;
                    }
                    io[c][i + j] = y;
                }
            }
            if (listenW == 0.0f && listenTarget == 0) listenKind = 0;

            clock.advance (len);
            i += len;
        }
        for (int b = 0; b < 2; ++b) { grDb[b] = grMin[b]; keyDb[b] = lastKey[b]; }
    }

private:
    static constexpr float kSlope = 1.0f - 1.0f / 5.0f; // 5:1
    static constexpr float kKneeHalf = 3.0f;

    struct BandCfg
    {
        bool on = true;
        int mode = 0, type = 3;
        SvfCoefs c;          // detector / listen band filter
        float g0 = 0.0f, k0 = 1.0f;
    };

    struct Cfg
    {
        bool linked = true, relative = true, ext = false;
        float attackCoef = 0.0f, releaseCoef = 0.0f, detRel = 0.0f, fullRel = 0.0f;
        BandCfg band[2];
    };

    struct BandChan
    {
        SvfState key, dyn;
        dyn::PeakDetector env;
        float grSm = 0.0f;
    };

    struct BandState
    {
        BandChan ch[2];
        SvfCoefs dynC[2];
        float dynGr[2] { 1.0e9f, 1.0e9f };
    };

    struct State
    {
        BandState band[2];
        dyn::PeakDetector full[2];
    };

    struct Core
    {
        Cfg cfg;
        State st;
    };

    struct Live
    {
        float thresh[2] { -20.0f, -20.0f }, range[2] { 12.0f, 12.0f };
    };

    //==========================================================================
    void derive()
    {
        const DsParams& p = params;
        Cfg c;
        c.linked = p.linked;
        c.relative = p.relative;
        c.ext = p.ext;
        c.attackCoef = dyn::riseCoef (std::max (0.02f, p.attackMs), fs);
        c.releaseCoef = dyn::riseCoef (std::max (1.0f, p.releaseMs), fs);
        c.detRel = timeCoef (1.5f, fs);
        c.fullRel = timeCoef (60.0f, fs);
        for (int b = 0; b < 2; ++b)
        {
            c.band[b].on = p.band[b].on;
            c.band[b].mode = std::clamp (p.band[b].mode, 0, 1);
            c.band[b].type = std::clamp (p.band[b].type, 0, 3);
        }
        pending = c;

        pendingLookahead = dyn::lookaheadSamples (p.lookahead, fs);
        latencyN.store (pendingLookahead, std::memory_order_relaxed);
        pendingListen = std::clamp (p.listen, 0, 3);

        for (int b = 0; b < 2; ++b)
        {
            threshSm[b].setTarget (p.band[b].thresh);
            rangeSm[b].setTarget (p.band[b].range);
            freqSm[b].setTarget (std::log (std::max (20.0f, p.band[b].freq)));
        }
        outR.setTarget (dbToGain (p.outDb));
    }

    void updateLive() noexcept
    {
        for (int b = 0; b < 2; ++b) { live.thresh[b] = threshSm[b].current; live.range[b] = rangeSm[b].current; }
    }

    // Detector / listen filter of a band at the current (smoothed) frequency.
    void designBand (Core& core, int b) const
    {
        auto& band = core.cfg.band[b];
        const float f = std::exp (freqSm[b].current);
        const float q = band.type == 1 ? 1.4f : band.type == 2 ? 5.0f : 0.7071f;
        switch (band.type)
        {
            case 0:  band.c = makeSvf (SvfType::LowPass, fs, f, q); break;
            case 1:
            case 2:  band.c = makeSvf (SvfType::BandPass, fs, f, q); break;
            default: band.c = makeSvf (SvfType::HighPass, fs, f, q); break;
        }
        band.g0 = band.c.g;
        band.k0 = band.c.k;
        core.st.band[b].dynGr[0] = core.st.band[b].dynGr[1] = 1.0e9f;
    }

    void onTick()
    {
        if (dirty) applyPending();
        updateLive();
        for (int b = 0; b < 2; ++b)
        {
            threshSm[b].tick();
            rangeSm[b].tick();
            if (freqSm[b].tick()) designBand (main, b);
        }
        outR.tick();
    }

    static bool discreteChange (const Cfg& a, const Cfg& b) noexcept
    {
        for (int i = 0; i < 2; ++i)
            if (a.band[i].on != b.band[i].on || a.band[i].mode != b.band[i].mode || a.band[i].type != b.band[i].type) return true;
        return false;
    }

    void applyPending()
    {
        const bool discrete = discreteChange (pending, main.cfg);
        if (discrete && fade.active) return; // finish the running cross-fade first
        if (discrete) startSwitch();
        else
        {
            // Continuous settings only: keep the band filters designed at the smoothed frequency.
            for (int b = 0; b < 2; ++b) { pending.band[b].c = main.cfg.band[b].c; pending.band[b].g0 = main.cfg.band[b].g0; pending.band[b].k0 = main.cfg.band[b].k0; }
            main.cfg = pending;
        }
        lookaheadN = pendingLookahead;
        listenTarget = pendingListen;
        if (listenTarget != 0) listenKind = listenTarget;
        dirty = false;
    }

    // The old configuration keeps running in the shadow core. A band that is switched on starts
    // from a clean state, and a band whose filter type changes restarts its audio filter from
    // zero, which begins at unity gain instead of inheriting the wrong filter's state.
    void startSwitch()
    {
        shadow = main;
        for (int b = 0; b < 2; ++b)
        {
            const bool wasOn = main.cfg.band[b].on;
            const int oldType = main.cfg.band[b].type;
            main.cfg.band[b].on = pending.band[b].on;
            main.cfg.band[b].mode = pending.band[b].mode;
            main.cfg.band[b].type = pending.band[b].type;
            main.cfg.linked = pending.linked;
            if (pending.band[b].on && ! wasOn)
            {
                main.st.band[b] = {};
            }
            else if (pending.band[b].type != oldType)
            {
                for (auto& ch : main.st.band[b].ch) { ch.dyn = {}; ch.key = {}; }
            }
            designBand (main, b);
        }
        main.cfg.relative = pending.relative;
        main.cfg.ext = pending.ext;
        main.cfg.attackCoef = pending.attackCoef;
        main.cfg.releaseCoef = pending.releaseCoef;
        main.cfg.detRel = pending.detRel;
        main.cfg.fullRel = pending.fullRel;
        fade.start (fadeLen);
    }

    // Simper shelf / bell coefficients for a gain of grDb (<= 0) at the band.
    static inline SvfCoefs dynamicCoefs (const BandCfg& band, float grDb) noexcept
    {
        SvfCoefs c;
        const float A = fastDbToGain (0.5f * grDb); // 10^(dB/40)
        float g = band.g0, k = band.k0;
        switch (band.type)
        {
            case 0: // low shelf
                g /= std::sqrt (A);
                c.m0 = 1.0f; c.m1 = k * (A - 1.0f); c.m2 = A * A - 1.0f;
                break;
            case 1:
            case 2: // bell
                k /= A;
                c.m0 = 1.0f; c.m1 = k * (A * A - 1.0f); c.m2 = 0.0f;
                break;
            default: // high shelf
                g *= std::sqrt (A);
                c.m0 = A * A; c.m1 = k * (1.0f - A) * A; c.m2 = 1.0f - A * A;
                break;
        }
        c.g = g;
        c.k = k;
        c.a1 = 1.0f / (1.0f + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    // Recomputes only when the gain has moved by more than 0.02 dB (inaudible step) or lands
    // exactly on 0 dB, where the filter must return to bit-transparency.
    static inline const SvfCoefs& cachedCoefs (BandState& bs, const BandCfg& band, int c, float grDb) noexcept
    {
        float& last = bs.dynGr[c];
        if (std::abs (grDb - last) > 0.02f || (grDb == 0.0f && last != 0.0f))
        {
            bs.dynC[c] = dynamicCoefs (band, grDb);
            last = grDb;
        }
        return bs.dynC[c];
    }

    inline float target (float over, float range) const noexcept
    {
        float gr;
        if (over <= -kKneeHalf) gr = 0.0f;
        else if (over < kKneeHalf) { const float t = over + kKneeHalf; gr = -kSlope * t * t / (4.0f * kKneeHalf); }
        else gr = -kSlope * over;
        return std::max (gr, -range);
    }

    static inline float smooth (BandChan& st, float tgt, const Cfg& cfg) noexcept
    {
        st.grSm = tgt + (st.grSm - tgt) * (tgt < st.grSm ? cfg.attackCoef : cfg.releaseCoef);
        if (tgt == 0.0f && st.grSm > -1.0e-3f) st.grSm = 0.0f; // settle exactly: back to bit-transparent
        return st.grSm;
    }

    // mainIn is the delayed audio (dryBuf); detection reads the undelayed det / ext signals.
    void runCore (Core& core, const float* const* det, const float* const* ext, float (&out)[2][dyn::kSeg],
                  float (&bandOut)[2][2][dyn::kSeg], int numCh, int len, float (&grMin)[2], float (&lastKey)[2]) noexcept
    {
        const Cfg& cfg = core.cfg;
        State& st = core.st;
        const bool linked = cfg.linked && numCh > 1;

        for (int i = 0; i < len; ++i)
        {
            float x[2] {}, over[2][2] {}, lvlDb[2][2] {};
            for (int c = 0; c < numCh; ++c)
            {
                x[c] = dryBuf[c][i];
                const float d = ext != nullptr ? ext[c][i] : det[c][i];
                float fullDb = -400.0f;
                if (cfg.relative) fullDb = fastGainToDb (st.full[c].process (std::abs (d), 0, cfg.fullRel));
                for (int b = 0; b < 2; ++b)
                {
                    if (! cfg.band[b].on) { bandOut[b][c][i] = 0.0f; continue; }
                    BandChan& bc = st.band[b].ch[c];
                    const float sd = bc.key.process (d, cfg.band[b].c);
                    bandOut[b][c][i] = sd;
                    float lvl = fastGainToDb (bc.env.process (std::abs (sd), 0, cfg.detRel));
                    lvlDb[b][c] = lvl;
                    if (cfg.relative) lvl = fullDb > -60.0f ? lvl - fullDb : -120.0f;
                    over[b][c] = lvl - live.thresh[b];
                }
            }

            for (int b = 0; b < 2; ++b)
            {
                if (! cfg.band[b].on) continue;
                BandState& bs = st.band[b];
                const BandCfg& band = cfg.band[b];
                float g[2];
                if (linked)
                {
                    g[0] = smooth (bs.ch[0], target (std::max (over[b][0], over[b][1]), live.range[b]), cfg);
                    g[1] = g[0];
                    bs.ch[1].grSm = bs.ch[0].grSm;
                }
                else
                {
                    for (int c = 0; c < numCh; ++c) g[c] = smooth (bs.ch[c], target (over[b][c], live.range[b]), cfg);
                }

                if (band.mode == 0)
                {
                    if (linked)
                    {
                        const SvfCoefs& dc = cachedCoefs (bs, band, 0, g[0]);
                        for (int c = 0; c < numCh; ++c) x[c] = bs.ch[c].dyn.process (x[c], dc);
                    }
                    else
                    {
                        for (int c = 0; c < numCh; ++c) x[c] = bs.ch[c].dyn.process (x[c], cachedCoefs (bs, band, c, g[c]));
                    }
                }
                else
                {
                    for (int c = 0; c < numCh; ++c) x[c] *= g[c] == 0.0f ? 1.0f : fastDbToGain (g[c]);
                }

                for (int c = 0; c < numCh; ++c) grMin[b] = std::min (grMin[b], g[c]);
                lastKey[b] = numCh > 1 ? std::max (lvlDb[b][0], lvlDb[b][1]) : lvlDb[b][0];
            }

            for (int c = 0; c < numCh; ++c) out[c][i] = x[c];
        }
    }

    double fs = 48000.0;
    DsParams params;
    bool havePending = false, initialised = false, dirty = false;
    Cfg pending;
    int pendingLookahead = 0, pendingListen = 0;
    std::atomic<int> latencyN { 0 };

    Core main, shadow;
    Live live;
    ControlClock clock;
    dyn::ShadowFade fade;
    uint32_t fadeLen = 720;
    int lookaheadN = 0;

    Smoothed threshSm[2], rangeSm[2], freqSm[2];
    dyn::Ramp outR;

    dyn::DelayLine delay[2], bandDelay[2][2];
    int listenTarget = 0, listenKind = 0;
    float listenW = 0.0f, listenStep = 0.01f;

    float dryBuf[2][dyn::kSeg] {}, outBuf[2][dyn::kSeg] {}, shBuf[2][dyn::kSeg] {}, listenAl[2][dyn::kSeg] {};
    float bandBuf[2][2][dyn::kSeg] {}, shBand[2][2][dyn::kSeg] {};
    float grDb[2] { 0.0f, 0.0f }, keyDb[2] { -120.0f, -120.0f };
};

// Maps the parameter snapshot to DE-ESS settings (owned by this module).
inline DsParams readDsParams (const ParamSnapshot& p)
{
    DsParams dp;
    dp.linked = p.choice (P::dsMode) == 0;
    dp.relative = p.on (P::dsRelative);
    dp.ext = p.on (P::dsExt);
    dp.listen = p.choice (P::dsListen);
    dp.lookahead = p.choice (P::dsLookahead);
    dp.attackMs = p.f (P::dsAttack);
    dp.releaseMs = p.f (P::dsRelease);
    dp.outDb = p.f (P::dsOut);
    for (int b = 0; b < 2; ++b)
    {
        const int o = P::ds1On + b * P::kDsBandStride;
        dp.band[b].on = p.on (o);
        dp.band[b].mode = p.choice (o + 1);
        dp.band[b].type = p.choice (o + 2);
        dp.band[b].freq = p.f (o + 3);
        dp.band[b].thresh = p.f (o + 4);
        dp.band[b].range = p.f (o + 5);
    }
    return dp;
}
} // namespace nl

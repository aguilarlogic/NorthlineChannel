#pragma once

#include "ParamSnapshot.h"
#include "Dynamics.h"
#include <atomic>

namespace nl
{
struct GateParams
{
    int type = 0; // 0 gate, 1 expander, 2 duck
    bool linked = true, ext = false, listen = false;
    int lookahead = 0; // index into kLookaheadMs
    float thresh = -40.0f, hyst = 4.0f, floorDb = -40.0f, ratio = 2.0f, knee = 3.0f;
    float attackMs = 0.5f, holdMs = 30.0f, releaseMs = 150.0f, scHp = 20.0f, scLp = 20000.0f;

    bool operator== (const GateParams&) const = default;
};

// Gate, downward expander and ducker on one sidechain-filtered peak detector.
//
//  Detector : key -> HP/LP -> peak follower with instant attack and a hold of at least one period of
//             the sidechain high-pass frequency, so low-frequency content cannot make the level
//             ripple through the threshold.
//  Gate     : open at the threshold, close below threshold - hysteresis once the hold has run out.
//             Opening is a raised cosine in the dB domain over the attack time (minimum 0.02 ms);
//             closing falls at a constant dB rate, and "release" is the time to travel the whole
//             range down to the floor.
//  Expander : a true downward law (ratio, soft knee, floor). Continuous in level, so there is no
//             step at the threshold. Attack and release are 10-90% times of the gain in dB.
//  Duck     : the gate inverted: the main path is attenuated by the floor while the key is active.
//
// With lookahead the detector sees the undelayed key while the audio is delayed, so the gain is
// already open when a transient arrives. latency() is the lookahead choice, constant whether the
// module is in use.
class GateModule
{
public:
    enum Type { Gate = 0, Expander, Duck };

    int latency() const noexcept { return latencyN.load (std::memory_order_relaxed); }
    // Detector (key) level in dBFS after the sidechain filters, for the scope.
    float getKeyLevelDb() const noexcept { return keyDb; }
    float getGainReductionDb() const noexcept { return grDb; }

    void prepare (double sampleRate, int /*maxBlock*/ = 4096)
    {
        fs = sampleRate;
        const int maxLa = dyn::maxLookaheadSamples (fs);
        for (auto& d : delay) d.prepare (maxLa);
        for (auto& d : keyDelay) d.prepare (maxLa);
        fadeLen = (uint32_t) std::max (1.0, std::round (0.015 * fs));
        listenStep = (float) (1.0 / std::max (1.0, 0.005 * fs));
        for (auto* s : { &threshSm, &hystSm, &ratioSm, &kneeSm }) s->prepare (fs, 10.0f);
        floorR.prepare (fs, 10.0f);
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
        initUnits (main);
        fade.stop();
        lookaheadN = pendingLookahead;
        listenTarget = pendingListen;
        listenW = listenTarget != 0 ? 1.0f : 0.0f;
        for (auto* s : { &threshSm, &hystSm, &ratioSm, &kneeSm }) s->snap();
        floorR.snap();
        updateLive();
        for (auto& d : delay) d.reset();
        for (auto& d : keyDelay) d.reset();
        initialised = true;
        dirty = false;
        grDb = 0.0f;
        keyDb = -120.0f;
    }

    // Derives coefficients and targets only; the audio sees them from the next control tick.
    void setParams (const GateParams& p)
    {
        if (havePending && p == params) return;
        params = p;
        havePending = true;
        derive();
        dirty = true;
    }

    // key: external sidechain channels or nullptr to use the input.
    void process (float* const* io, int numCh, int n, const float* const* key)
    {
        if (! initialised) reset();
        numCh = std::clamp (numCh, 1, 2);
        float grMin = 0.0f, lastKey = keyDb;
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

            float* outP[2] { outBuf[0], outBuf[1] };
            float* keyP[2] { keyBuf[0], keyBuf[1] };
            const float* dryP[2] { dryBuf[0], dryBuf[1] };

            if (fade.active)
            {
                float* shP[2] { shBuf[0], shBuf[1] };
                float* shKey[2] { shKeyBuf[0], shKeyBuf[1] };
                float dummyGr = 0.0f, dummyKey = 0.0f;
                runCore (shadow, det, extP, dryP, shP, shKey, numCh, p0, len, dummyGr, dummyKey);
            }
            float segKey = lastKey;
            runCore (main, det, extP, dryP, outP, keyP, numCh, p0, len, grMin, segKey);
            lastKey = segKey;

            if (fade.active)
            {
                for (int j = 0; j < len; ++j)
                {
                    const float w = fade.active ? fade.next() : 1.0f;
                    for (int c = 0; c < numCh; ++c) outBuf[c][j] = shBuf[c][j] + (outBuf[c][j] - shBuf[c][j]) * w;
                }
            }

            const bool needKey = listenTarget != 0 || listenW > 0.0f;
            if (needKey)
                for (int c = 0; c < numCh; ++c)
                    for (int j = 0; j < len; ++j) keyAl[c][j] = keyDelay[c].process (keyBuf[c][j], (uint32_t) lookaheadN);

            const float lwTarget = listenTarget != 0 ? 1.0f : 0.0f;
            for (int j = 0; j < len; ++j)
            {
                if (listenW != lwTarget)
                    listenW = listenW < lwTarget ? std::min (lwTarget, listenW + listenStep) : std::max (lwTarget, listenW - listenStep);
                for (int c = 0; c < numCh; ++c)
                {
                    float y = outBuf[c][j];
                    if (listenW > 0.0f) y += (keyAl[c][j] - y) * listenW;
                    io[c][i + j] = y;
                }
            }

            clock.advance (len);
            i += len;
        }
        grDb = grMin;
        keyDb = lastKey;
    }

private:
    struct Cfg
    {
        int type = 0;
        bool linked = true, ext = false, hpOn = false, lpOn = false;
        int detHoldN = 0, holdN = 0, attackN = 1;
        float detRel = 0.0f, atkCoef = 0.0f, relCoef = 0.0f, invRelN = 0.0f;
        SvfCoefs hpC, lpC;
    };

    struct Unit // one gain stage: per channel, or shared in linked mode
    {
        bool act = false;   // key active (above threshold, with hysteresis and hold)
        int holdCnt = 0;
        int mode = 0;       // 0 idle (tracks the target), 1 raised-cosine attack, 2 constant-rate release
        int pos = 0, len = 1;
        float gDb = 0.0f, start = 0.0f, tgt = 0.0f;
    };

    struct ChanState
    {
        SvfState hp, lp;
        dyn::PeakDetector peak;
    };

    struct State
    {
        ChanState ch[2];
        Unit unit[2];
    };

    struct Core
    {
        Cfg cfg;
        State st;
    };

    struct Live
    {
        float thresh = -40.0f, hyst = 4.0f, ratio = 2.0f, knee = 3.0f;
    };

    //==========================================================================
    void derive()
    {
        const GateParams& p = params;
        Cfg c;
        c.type = std::clamp (p.type, 0, 2);
        c.linked = p.linked;
        c.ext = p.ext;
        c.hpOn = p.scHp > 21.0f;
        c.lpOn = p.scLp < 19500.0f;
        if (c.hpOn) c.hpC = makeSvf (SvfType::HighPass, fs, p.scHp, 0.7071f);
        if (c.lpOn) c.lpC = makeSvf (SvfType::LowPass, fs, p.scLp, 0.7071f);

        // The detector holds each peak for at least one period of the sidechain high-pass.
        const float hpHz = std::max (20.0f, p.scHp);
        const int periodN = (int) std::ceil (fs / (double) hpHz);
        c.holdN = (int) std::lround (0.001 * (double) p.holdMs * fs);
        c.detHoldN = c.type == Expander ? std::max (periodN, c.holdN) : periodN;
        c.detRel = timeCoef (1.0f, fs);
        c.attackN = std::max (1, (int) std::lround (0.001 * (double) std::max (0.02f, p.attackMs) * fs));
        c.atkCoef = dyn::riseCoef (std::max (0.02f, p.attackMs), fs);
        c.relCoef = dyn::riseCoef (p.releaseMs, fs);
        c.invRelN = (float) (1.0 / std::max (1.0, 0.001 * (double) p.releaseMs * fs));
        pending = c;

        pendingLookahead = dyn::lookaheadSamples (p.lookahead, fs);
        latencyN.store (pendingLookahead, std::memory_order_relaxed);
        pendingListen = p.listen ? 1 : 0;

        threshSm.setTarget (p.thresh);
        hystSm.setTarget (p.hyst);
        ratioSm.setTarget (std::max (1.0f, p.ratio));
        kneeSm.setTarget (std::clamp (p.knee, 0.0f, 12.0f));
        floorR.setTarget (std::min (0.0f, p.floorDb));
    }

    void updateLive() noexcept
    {
        live.thresh = threshSm.current;
        live.hyst = hystSm.current;
        live.ratio = ratioSm.current;
        live.knee = kneeSm.current;
    }

    // A gate starts open, a ducker starts passing; an expander starts at unity.
    void initUnits (Core& core) const noexcept
    {
        for (auto& u : core.st.unit)
        {
            u = {};
            u.act = core.cfg.type == Gate;
            u.holdCnt = core.cfg.holdN;
        }
    }

    void onTick()
    {
        if (dirty) applyPending();
        threshSm.tick(); hystSm.tick(); ratioSm.tick(); kneeSm.tick();
        updateLive();
        floorR.tick();
    }

    void applyPending()
    {
        const bool typeChange = pending.type != main.cfg.type;
        if (typeChange && fade.active) return; // finish the running cross-fade first
        if (typeChange) startSwitch();
        else main.cfg = pending;
        lookaheadN = pendingLookahead;
        listenTarget = pendingListen;
        dirty = false;
    }

    // The old type keeps running in the shadow core; the new one continues from the same gain.
    void startSwitch()
    {
        shadow = main;
        main.cfg = pending;
        const float fl = floorR.to;
        for (auto& u : main.st.unit)
        {
            u.mode = 0;
            u.holdCnt = 0;
            if (pending.type == Duck) u.act = u.gDb < 0.5f * fl;
            else if (pending.type == Gate) u.act = u.gDb > 0.5f * fl;
        }
        fade.start (fadeLen);
    }

    // Gate / duck stage: hysteresis + hold decide whether the key is active; the gain moves to
    // its target with a raised-cosine attack and a constant-rate release (both in dB).
    static inline float stepBinary (Unit& u, float levelDb, const Cfg& cfg, const Live& lv, float floorDb) noexcept
    {
        const bool wasAct = u.act;
        if (levelDb >= lv.thresh) { u.act = true; u.holdCnt = cfg.holdN; }
        else if (u.act)
        {
            if (levelDb >= lv.thresh - lv.hyst) u.holdCnt = cfg.holdN;
            else if (u.holdCnt > 0) --u.holdCnt;
            else u.act = false;
        }

        const bool duck = cfg.type == Duck;
        const float target = u.act == duck ? floorDb : 0.0f; // gate: active -> 0, inactive -> floor; duck: reversed
        const float range = std::max(1.0f, -floorDb);
        if (u.act != wasAct)
        {
            u.start = u.gDb;
            u.tgt = target;
            if (u.act)
            {
                const float frac = std::clamp (std::abs (target - u.gDb) / range, 0.05f, 1.0f);
                u.len = std::max (1, (int) std::lround ((float) cfg.attackN * frac));
                u.pos = 0;
                u.mode = 1;
            }
            else
            {
                u.mode = 2;
            }
        }

        if (u.mode == 1)
        {
            ++u.pos;
            if (u.pos >= u.len) { u.gDb = u.tgt; u.mode = 0; }
            else
            {
                const float s = 0.5f - 0.5f * std::cos (kPi * (float) u.pos / (float) u.len);
                u.gDb = u.start + (u.tgt - u.start) * s;
            }
        }
        else if (u.mode == 2)
        {
            const float step = range * cfg.invRelN;
            if (u.tgt < u.gDb) { u.gDb -= step; if (u.gDb <= u.tgt) { u.gDb = u.tgt; u.mode = 0; } }
            else               { u.gDb += step; if (u.gDb >= u.tgt) { u.gDb = u.tgt; u.mode = 0; } }
        }
        else
        {
            u.gDb = target;
        }
        return u.gDb;
    }

    static inline float stepExpander (Unit& u, float levelDb, const Cfg& cfg, const Live& lv, float floorDb) noexcept
    {
        const float tg = std::max (dyn::expanderGainDb (levelDb, lv.thresh, lv.ratio, lv.knee), floorDb);
        u.gDb = tg + (u.gDb - tg) * (tg > u.gDb ? cfg.atkCoef : cfg.relCoef);
        if (std::abs (u.gDb - tg) < 1.0e-4f) u.gDb = tg;
        return u.gDb;
    }

    void runCore (Core& core, const float* const* det, const float* const* ext, const float* const* mainIn,
                  float* const* out, float* const* keyOut, int numCh, int p0, int len, float& grMin, float& lastKey) noexcept
    {
        const Cfg& cfg = core.cfg;
        State& st = core.st;
        const bool linked = cfg.linked && numCh > 1;
        const bool expander = cfg.type == Expander;
        for (int i = 0; i < len; ++i)
        {
            const float floorDb = floorR.at (p0 + i);
            float lvl[2] { -400.0f, -400.0f };
            for (int c = 0; c < numCh; ++c)
            {
                ChanState& s = st.ch[c];
                float k = ext != nullptr ? ext[c][i] : det[c][i];
                if (cfg.hpOn) k = s.hp.process (k, cfg.hpC);
                if (cfg.lpOn) k = s.lp.process (k, cfg.lpC);
                keyOut[c][i] = k;
                lvl[c] = fastGainToDb (s.peak.process (std::abs (k), cfg.detHoldN, cfg.detRel));
            }
            lastKey = numCh > 1 ? std::max (lvl[0], lvl[1]) : lvl[0];

            float g[2];
            if (linked)
            {
                const float l = std::max (lvl[0], lvl[1]);
                g[0] = expander ? stepExpander (st.unit[0], l, cfg, live, floorDb) : stepBinary (st.unit[0], l, cfg, live, floorDb);
                g[1] = g[0];
                st.unit[1] = st.unit[0];
            }
            else
            {
                for (int c = 0; c < numCh; ++c)
                    g[c] = expander ? stepExpander (st.unit[c], lvl[c], cfg, live, floorDb) : stepBinary (st.unit[c], lvl[c], cfg, live, floorDb);
            }

            for (int c = 0; c < numCh; ++c)
            {
                grMin = std::min (grMin, g[c]);
                out[c][i] = g[c] == 0.0f ? mainIn[c][i] : mainIn[c][i] * fastDbToGain (g[c]);
            }
        }
    }

    double fs = 48000.0;
    GateParams params;
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

    Smoothed threshSm, hystSm, ratioSm, kneeSm;
    dyn::Ramp floorR;

    dyn::DelayLine delay[2], keyDelay[2];
    int listenTarget = 0;
    float listenW = 0.0f, listenStep = 0.01f;

    float dryBuf[2][dyn::kSeg] {}, outBuf[2][dyn::kSeg] {}, shBuf[2][dyn::kSeg] {};
    float keyBuf[2][dyn::kSeg] {}, shKeyBuf[2][dyn::kSeg] {}, keyAl[2][dyn::kSeg] {};
    float grDb = 0.0f, keyDb = -120.0f;
};

// Maps the parameter snapshot to GATE settings (owned by this module).
inline GateParams readGateParams (const ParamSnapshot& p)
{
    GateParams gp;
    gp.type = std::clamp (p.choice (P::gateType), 0, 2);
    gp.linked = p.choice (P::gateMode) == 0;
    gp.ext = p.on (P::gateExt);
    gp.listen = p.on (P::gateListen);
    gp.lookahead = p.choice (P::gateLookahead);
    gp.thresh = p.f (P::gateThresh);
    gp.hyst = p.f (P::gateHyst);
    gp.floorDb = p.f (P::gateFloor);
    gp.ratio = p.f (P::gateRatio);
    gp.knee = p.f (P::gateKnee);
    gp.attackMs = p.f (P::gateAttack);
    gp.holdMs = p.f (P::gateHold);
    gp.releaseMs = p.f (P::gateRelease);
    gp.scHp = p.f (P::gateScHp);
    gp.scLp = p.f (P::gateScLp);
    return gp;
}
} // namespace nl

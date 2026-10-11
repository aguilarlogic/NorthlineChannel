#pragma once

#include "ParamSnapshot.h"
#include "Dynamics.h"
#include <atomic>

namespace nl
{
struct CompParams
{
    int type = 0;      // 0 VCA, 1 FET, 2 OPT, 3 SOFT
    int listen = 0;    // 0 off, 1 key (sidechain), 2 delta (what the compressor removes)
    int lookahead = 0; // index into kLookaheadMs
    bool makeup = false, ext = false, autoRel = false;
    float thresh = -18.0f, ratio = 3.0f, knee = 6.0f, attackMs = 10.0f, releaseMs = 150.0f;
    float mix = 1.0f, outDb = 0.0f, rangeDb = 40.0f; // range >= 40 means unlimited
    float link = 1.0f;                               // 0..1 cross-channel detector sharing (0 outside ST mode)
    float scHp = 20.0f, scLp = 20000.0f, scBellFreq = 3000.0f, scBellGain = 0.0f;
    float opLevelDb = -18.0f;

    bool operator== (const CompParams&) const = default;
};

// Compressor with four detector / release characters.
//
//  Signal flow:  wet = x . g_gr . g_makeup  ->  y = dry + (wet - dry) . mix  ->  y . g_out
//  The dry path is never scaled by the make-up gain. With lookahead the detector sees the
//  undelayed input while the audio (and the dry path of mix, listen and delta) is delayed by the
//  lookahead, so everything stays aligned and latency() is that delay whether or not the module
//  is in use.
//
//  VCA : peak detector (instant attack, short hold), gain smoothed in the dB domain.
//  FET : like VCA plus GR-dependent odd-harmonic colour from a first-order ADAA shaper.
//  OPT : RMS detector, program-dependent two-stage release with a memory of recent gain
//        reduction.
//  SOFT: RMS detector, plain branching release.
//
//  Attack and release are 10-90% times of the gain-reduction step response. The RMS types have
//  a detector with a window of about a quarter of the attack; the attack smoother is shortened
//  to compensate, so the measured attack still matches the knob. Below minAttackMs() the RMS
//  window itself is the limit.
class CompModule
{
public:
    enum Type { VCA = 0, FET, OPT, SOFT };

    // Static curve (gain change in dB, <= 0) for a detector level, including the range limit.
    static float staticGain (const CompParams& p, float levelDb) noexcept
    {
        const float invR = p.ratio >= kRatioInf ? 0.0f : 1.0f / p.ratio;
        const float g = dyn::compressorGainDb (levelDb, p.thresh, invR, p.knee);
        return p.rangeDb >= 39.9f ? g : std::max (g, -p.rangeDb);
    }

    // Smallest attack the type can honour (the RMS detector window is the limit).
    static float minAttackMs (int type) noexcept { return type >= OPT ? 0.75f : 0.0f; }

    // Static make-up that restores the loudness of a reference signal (pink noise) at the
    // Operating Level. The detector reads such a signal with a type-dependent offset and spread;
    // the make-up integrates the static curve over that level distribution, which accounts for
    // the loudness the compressor leaves in the signal's peaks.
    static float autoMakeupDb (const CompParams& p) noexcept
    {
        const Reference ref = referenceFor (p.type);
        const double mu = (double) p.opLevelDb + ref.offsetDb;
        double sum = 0.0, wsum = 0.0;
        for (int k = -12; k <= 12; ++k)
        {
            const double z = 0.25 * k;
            const double w = std::exp (-0.5 * z * z);
            const double g = staticGain (p, (float) (mu + ref.sigmaDb * z));
            sum += w * std::pow (10.0, g * 0.1);
            wsum += w;
        }
        return (float) (-10.0 * std::log10 (sum / wsum));
    }

    // Latency in samples: the lookahead choice. Constant whether the module is in use or not.
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
        for (auto* s : { &threshSm, &invRatioSm, &kneeSm, &rangeSm, &linkSm }) s->prepare (fs, 15.0f);
        for (auto* r : { &mixR, &outR, &makeupR }) r->prepare (fs, 20.0f);
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
        for (auto* s : { &threshSm, &invRatioSm, &kneeSm, &rangeSm, &linkSm }) s->snap();
        for (auto* r : { &mixR, &outR, &makeupR }) r->snap();
        updateLive();
        for (auto& d : delay) d.reset();
        for (auto& d : keyDelay) d.reset();
        initialised = true;
        dirty = false;
        grDb = 0.0f;
        keyDb = -120.0f;
    }

    // Derives coefficients and targets only; the audio sees them from the next control tick.
    void setParams (const CompParams& p)
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

            // Audio path (delayed by the lookahead) and the undelayed detector input.
            for (int c = 0; c < numCh; ++c)
            {
                if (lookaheadN > 0)
                    for (int j = 0; j < len; ++j) dryBuf[c][j] = delay[c].process (det[c][j], (uint32_t) lookaheadN);
                else
                    for (int j = 0; j < len; ++j) dryBuf[c][j] = det[c][j];
            }

            float* wetP[2] { wetBuf[0], wetBuf[1] };
            float* keyP[2] { keyBuf[0], keyBuf[1] };
            const float* dryP[2] { dryBuf[0], dryBuf[1] };

            if (fade.active)
            {
                float* shP[2] { shBuf[0], shBuf[1] };
                float* shKey[2] { shKeyBuf[0], shKeyBuf[1] };
                float dummyGr = 0.0f, dummyKey = 0.0f;
                runCore (shadow, det, extP, dryP, shP, shKey, numCh, len, dummyGr, dummyKey);
            }
            float segKey = lastKey;
            runCore (main, det, extP, dryP, wetP, keyP, numCh, len, grMin, segKey);
            lastKey = segKey;

            if (fade.active)
            {
                for (int j = 0; j < len; ++j)
                {
                    const float w = fade.active ? fade.next() : 1.0f;
                    for (int c = 0; c < numCh; ++c) wetBuf[c][j] = shBuf[c][j] + (wetBuf[c][j] - shBuf[c][j]) * w;
                }
            }

            const bool needKey = listenKind == 1 && (listenTarget == 1 || listenW > 0.0f);
            if (needKey)
                for (int c = 0; c < numCh; ++c)
                    for (int j = 0; j < len; ++j) keyAl[c][j] = keyDelay[c].process (keyBuf[c][j], (uint32_t) lookaheadN);

            const float lwTarget = listenTarget != 0 ? 1.0f : 0.0f;
            for (int j = 0; j < len; ++j)
            {
                const int p = p0 + j;
                const float mg = mixR.at (p), og = outR.at (p), mk = makeupR.at (p);
                if (listenW != lwTarget)
                    listenW = listenW < lwTarget ? std::min (lwTarget, listenW + listenStep) : std::max (lwTarget, listenW - listenStep);
                for (int c = 0; c < numCh; ++c)
                {
                    const float d = dryBuf[c][j], wet = wetBuf[c][j];
                    float y = (d + (wet * mk - d) * mg) * og;
                    if (listenW > 0.0f)
                    {
                        const float l = listenKind == 1 ? keyAl[c][j] : d - wet;
                        y += (l - y) * listenW;
                    }
                    io[c][i + j] = y;
                }
            }
            if (listenW == 0.0f && listenTarget == 0) listenKind = 0;

            clock.advance (len);
            i += len;
        }
        grDb = grMin;
        keyDb = lastKey;
    }

private:
    struct Reference { double offsetDb, sigmaDb; };

    // How the detector reads pink noise at the Operating Level (mean offset and spread in dB).
    // Measured with the `dynamics` suite at the default timing.
    static Reference referenceFor (int type) noexcept
    {
        switch (type)
        {
            case FET: return { 5.0, 2.5 };
            case OPT: return { 2.0, 2.5 };
            case SOFT: return { 2.0, 2.5 };
            case VCA:
            default: return { 5.0, 2.5 };
        }
    }

    struct Cfg
    {
        int type = 0;
        bool ext = false, autoRel = false, hpOn = false, lpOn = false, bellOn = false;
        int holdN = 0;
        float peakRel = 0.0f, rmsCoef = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f, optFastCoef = 0.0f;
        float relMs = 150.0f, autoSlowMs = 900.0f;
        float memCharge = 0.0f, memDischarge = 0.0f;
        SvfCoefs hpC, lpC, bellC;
    };

    // Colour: odd-harmonic residual of a soft saturator, first-order ADAA. Only the nonlinear
    // part r(w) = w - w / sqrt (1 + d2 w^2) goes through the antiderivative filter, so the
    // linear signal path is not low-passed.
    struct Colour { float w1 = 0.0f, R1 = 0.0f; };

    struct ChanState
    {
        SvfState hp, lp, bell;
        dyn::PeakDetector peak;
        float ms = 0.0f, y = 0.0f; // RMS power, smoothed gain in dB (<= 0)
        float colourD2 = 0.0f;
        Colour colour;
    };

    struct State
    {
        ChanState ch[2];
        dyn::Memory mem;
        float relCoef = 0.0f; // current program-dependent release coefficient
    };

    struct Core
    {
        Cfg cfg;
        State st;
    };

    struct Live
    {
        float thresh = -18.0f, invRatio = 0.33f, knee = 6.0f, rangeDb = 120.0f, link = 1.0f;
    };

    //==========================================================================
    void derive()
    {
        const CompParams& p = params;
        Cfg c;
        c.type = std::clamp (p.type, 0, 3);
        c.ext = p.ext;
        c.autoRel = p.autoRel;
        c.hpOn = p.scHp > 21.0f;
        c.lpOn = p.scLp < 19500.0f;
        c.bellOn = std::abs (p.scBellGain) > 0.05f;
        if (c.hpOn) c.hpC = makeSvf (SvfType::HighPass, fs, p.scHp, 0.7071f);
        if (c.lpOn) c.lpC = makeSvf (SvfType::LowPass, fs, p.scLp, 0.7071f);
        if (c.bellOn) c.bellC = makeSvf (SvfType::Bell, fs, p.scBellFreq, 1.0f, p.scBellGain);

        const float A = std::max (0.0f, p.attackMs), R = std::max (1.0f, p.releaseMs);
        c.relMs = R;
        c.releaseCoef = dyn::riseCoef (R, fs);
        c.autoSlowMs = std::min (6.0f * R, 10000.0f);
        if (c.type <= FET)
        {
            c.holdN = (int) std::lround (0.005 * fs);
            c.peakRel = timeCoef (1.5f, fs);
            c.attackCoef = dyn::riseCoef (A, fs);
        }
        else
        {
            // RMS window: about a quarter of the shortest time that has to come through it.
            const float fastest = std::min (A, c.type == OPT ? 0.25f * R : R);
            const float tau = std::clamp (0.173f * fastest, 0.5f, 30.0f);
            const float rise = 1.446f * tau;
            const float aEff = std::sqrt (std::max (A * A - rise * rise, 0.09f * rise * rise));
            c.rmsCoef = timeCoef (tau, fs);
            c.attackCoef = dyn::riseCoef (aEff, fs);
        }
        c.optFastCoef = dyn::riseCoef (0.25f * R, fs);
        const double tickRate = fs / kControlBlock;
        c.memCharge = c.type == OPT ? timeCoef (2000.0f, tickRate) : timeCoef (500.0f, tickRate);
        c.memDischarge = c.type == OPT ? timeCoef (4000.0f, tickRate) : timeCoef (2000.0f, tickRate);
        pending = c;

        pendingLookahead = dyn::lookaheadSamples (p.lookahead, fs);
        latencyN.store (pendingLookahead, std::memory_order_relaxed);
        pendingListen = std::clamp (p.listen, 0, 2);

        threshSm.setTarget (p.thresh);
        invRatioSm.setTarget (p.ratio >= kRatioInf ? 0.0f : 1.0f / std::max (1.0f, p.ratio));
        kneeSm.setTarget (std::clamp (p.knee, 0.0f, 24.0f));
        rangeSm.setTarget (p.rangeDb >= 39.9f ? 120.0f : p.rangeDb);
        linkSm.setTarget (std::clamp (p.link, 0.0f, 1.0f));
        mixR.setTarget (std::clamp (p.mix, 0.0f, 1.0f));
        outR.setTarget (dbToGain (p.outDb));
        makeupR.setTarget (p.makeup ? dbToGain (autoMakeupDb (p)) : 1.0f);
    }

    void updateLive() noexcept
    {
        live.thresh = threshSm.current;
        live.invRatio = invRatioSm.current;
        live.knee = kneeSm.current;
        live.rangeDb = rangeSm.current;
        live.link = linkSm.current;
    }

    void onTick()
    {
        if (dirty) applyPending();
        threshSm.tick(); invRatioSm.tick(); kneeSm.tick(); rangeSm.tick(); linkSm.tick();
        updateLive();
        mixR.tick(); outR.tick(); makeupR.tick();
        controlTick (main);
        if (fade.active) controlTick (shadow);
    }

    void applyPending()
    {
        const bool typeChange = pending.type != main.cfg.type;
        if (typeChange && fade.active) return; // finish the running cross-fade first
        if (typeChange) startSwitch();
        else main.cfg = pending;
        lookaheadN = pendingLookahead;
        listenTarget = pendingListen;
        if (listenTarget != 0) listenKind = listenTarget;
        dirty = false;
    }

    // Keeps the old configuration running in the shadow core and fades to the new one. The new
    // core inherits the gain state, and its detector is primed from the old detector reading.
    void startSwitch()
    {
        shadow = main;
        main.cfg = pending;
        const bool toRms = pending.type >= OPT, fromRms = shadow.cfg.type >= OPT;
        for (auto& s : main.st.ch)
        {
            if (toRms && ! fromRms) s.ms = 0.5f * s.peak.env * s.peak.env;
            if (! toRms && fromRms) { s.peak.env = std::sqrt (2.0f * s.ms); s.peak.hold = 0; }
        }
        fade.start (fadeLen);
    }

    // Once per control tick: the program-dependent release and the FET colour amount.
    void controlTick (Core& core) noexcept
    {
        auto& st = core.st;
        const auto& cfg = core.cfg;
        const float yMin = std::min (st.ch[0].y, st.ch[1].y);
        st.mem.update (std::min (1.0f, -yMin * (cfg.type == OPT ? 0.1f : 0.1667f)), cfg.memCharge, cfg.memDischarge);
        if (cfg.type == OPT)
            st.relCoef = dyn::riseCoef (cfg.relMs * (1.0f + 2.0f * st.mem.value), fs);
        else if (cfg.autoRel)
            st.relCoef = dyn::riseCoef (cfg.relMs + (cfg.autoSlowMs - cfg.relMs) * st.mem.value, fs);
        for (auto& ch : st.ch) ch.colourD2 = cfg.type == FET ? 0.1f * std::min (1.0f, -ch.y * 0.05f) : 0.0f;
    }

    static inline float colourProcess (Colour& st, float w, float d2) noexcept
    {
        const float s = std::sqrt (1.0f + d2 * w * w);
        const float R = w * w * (s - 1.0f) / (2.0f * (s + 1.0f));
        const float dw = w - st.w1;
        float r;
        if (std::abs (dw) < 1.0e-5f)
        {
            const float wm = 0.5f * (w + st.w1);
            r = wm * (1.0f - 1.0f / std::sqrt (1.0f + d2 * wm * wm));
        }
        else
        {
            r = (R - st.R1) / dw;
        }
        st.w1 = w;
        st.R1 = R;
        return w - r;
    }

    template <int T>
    static void runType (Core& core, const Live& lv, const float* const* det, const float* const* ext, const float* const* mainIn,
                         float* const* out, float* const* keyOut, int numCh, int len, float& grMin, float& lastKey) noexcept
    {
        const Cfg& cfg = core.cfg;
        State& st = core.st;
        const bool linkOn = numCh > 1 && lv.link > 0.0f;
        for (int i = 0; i < len; ++i)
        {
            float lvl[2] { -400.0f, -400.0f };
            for (int c = 0; c < numCh; ++c)
            {
                ChanState& s = st.ch[c];
                float k = ext != nullptr ? ext[c][i] : det[c][i];
                if (cfg.hpOn) k = s.hp.process (k, cfg.hpC);
                if (cfg.bellOn) k = s.bell.process (k, cfg.bellC);
                if (cfg.lpOn) k = s.lp.process (k, cfg.lpC);
                keyOut[c][i] = k;
                if constexpr (T <= FET)
                {
                    lvl[c] = fastGainToDb (s.peak.process (std::abs (k), cfg.holdN, cfg.peakRel));
                }
                else
                {
                    s.ms = k * k + 1.0e-30f + (s.ms - k * k) * cfg.rmsCoef;
                    lvl[c] = 0.5f * fastGainToDb (s.ms) + 3.0103f; // sine peak convention
                }
            }
            const float mx = numCh > 1 ? std::max (lvl[0], lvl[1]) : lvl[0];
            lastKey = mx;

            for (int c = 0; c < numCh; ++c)
            {
                ChanState& s = st.ch[c];
                const float lk = linkOn ? lvl[c] + lv.link * (mx - lvl[c]) : lvl[c];
                const float gc = std::max (dyn::compressorGainDb (lk, lv.thresh, lv.invRatio, lv.knee), -lv.rangeDb);
                float y = s.y;
                if constexpr (T == OPT)
                {
                    if (gc < y) y = gc + (y - gc) * cfg.attackCoef;
                    else y = gc + (y - gc) * ((gc - y) > 3.0f ? cfg.optFastCoef : st.relCoef);
                }
                else
                {
                    const float rc = cfg.autoRel ? st.relCoef : cfg.releaseCoef;
                    y = gc + (y - gc) * (gc < y ? cfg.attackCoef : rc);
                }
                if (gc == 0.0f && y > -1.0e-4f) y = 0.0f;
                s.y = y;
                grMin = std::min (grMin, y);

                float w = mainIn[c][i] * fastDbToGain (y);
                if constexpr (T == FET)
                {
                    if (s.colourD2 > 1.0e-4f) w = colourProcess (s.colour, w, s.colourD2);
                    else { s.colour.w1 = w; s.colour.R1 = 0.0f; }
                }
                out[c][i] = w;
            }
        }
    }

    void runCore (Core& core, const float* const* det, const float* const* ext, const float* const* mainIn,
                  float* const* out, float* const* keyOut, int numCh, int len, float& grMin, float& lastKey) noexcept
    {
        switch (core.cfg.type)
        {
            case FET:  runType<FET> (core, live, det, ext, mainIn, out, keyOut, numCh, len, grMin, lastKey); break;
            case OPT:  runType<OPT> (core, live, det, ext, mainIn, out, keyOut, numCh, len, grMin, lastKey); break;
            case SOFT: runType<SOFT> (core, live, det, ext, mainIn, out, keyOut, numCh, len, grMin, lastKey); break;
            case VCA:
            default:   runType<VCA> (core, live, det, ext, mainIn, out, keyOut, numCh, len, grMin, lastKey); break;
        }
    }

    double fs = 48000.0;
    CompParams params;
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

    Smoothed threshSm, invRatioSm, kneeSm, rangeSm, linkSm;
    dyn::Ramp mixR, outR, makeupR;

    dyn::DelayLine delay[2], keyDelay[2];
    int listenTarget = 0, listenKind = 0;
    float listenW = 0.0f, listenStep = 0.01f;

    float dryBuf[2][dyn::kSeg] {}, wetBuf[2][dyn::kSeg] {}, shBuf[2][dyn::kSeg] {};
    float keyBuf[2][dyn::kSeg] {}, shKeyBuf[2][dyn::kSeg] {}, keyAl[2][dyn::kSeg] {};
    float grDb = 0.0f, keyDb = -120.0f;
};

// Maps the parameter snapshot to COMP settings (owned by this module).
inline CompParams readCompParams (const ParamSnapshot& p)
{
    CompParams cp;
    cp.type = p.choice (P::compType);
    cp.link = p.choice (P::compMode) == 0 ? p.f (P::compLink) * 0.01f : 0.0f; // link only exists in ST mode
    cp.makeup = p.on (P::compMakeup);
    cp.ext = p.on (P::compExt);
    cp.autoRel = p.on (P::compAutoRel);
    cp.listen = p.choice (P::compListen);
    cp.lookahead = p.choice (P::compLookahead);
    cp.thresh = p.f (P::compThresh);
    cp.ratio = p.f (P::compRatio);
    cp.knee = p.f (P::compKnee);
    cp.attackMs = p.f (P::compAttack);
    cp.releaseMs = p.f (P::compRelease);
    cp.mix = p.f (P::compMix) * 0.01f;
    cp.outDb = p.f (P::compOut);
    cp.rangeDb = p.f (P::compRange);
    cp.scHp = p.f (P::compScHp);
    cp.scLp = p.f (P::compScLp);
    cp.scBellFreq = p.f (P::compScBellFreq);
    cp.scBellGain = p.f (P::compScBellGain);
    cp.opLevelDb = p.f (P::opLevel);
    return cp;
}
} // namespace nl

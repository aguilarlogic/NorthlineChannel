#pragma once

#include "Filters.h"

namespace nl
{
struct CompParams
{
    int type = 0; // 0 VCA, 1 FET, 2 OPT, 3 SOFT
    bool linked = true, makeup = false, ext = false, listen = false;
    float thresh = -18.0f, ratio = 3.0f, attackMs = 10.0f, releaseMs = 150.0f;
    float mix = 1.0f, outDb = 0.0f, scHp = 20.0f, scBellFreq = 3000.0f, scBellGain = 0.0f;
};

// Compressor with four detector/topology characters, all zero latency.
//  VCA : feed-forward, peak detector, 2 dB knee, log-domain branching smoothing
//        (Giannoulis, Massberg & Reiss, JAES 2012).
//  FET : feedback topology solved without a unit delay (implicit), so it stays
//        stable at any ratio; hard knee; GR-dependent odd-harmonic colour.
//  OPT : RMS detector, 10 dB knee, attack floor of 5 ms, and a two-stage,
//        program-dependent release with memory of recent gain reduction.
//  SOFT: RMS detector, 18 dB knee that starts at the threshold.
class CompModule
{
public:
    static float gainComputer (float levelDb, float thresh, float invRatio, float knee) noexcept
    {
        const float over = levelDb - thresh;
        if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
        {
            const float t = over + 0.5f * knee;
            return (invRatio - 1.0f) * t * t / (2.0f * knee);
        }
        return over <= 0.0f ? 0.0f : (invRatio - 1.0f) * over;
    }

    static float kneeFor (int type) noexcept
    {
        switch (type) { case 0: return 2.0f; case 1: return 0.0f; case 2: return 10.0f; default: return 18.0f; }
    }

    // Static curve as used for display and auto make-up.
    static float staticGain (const CompParams& p, float levelDb) noexcept
    {
        const float invR = p.ratio >= kRatioInfLocal ? 0.0f : 1.0f / p.ratio;
        const float knee = kneeFor (p.type);
        const float t = p.type == 3 ? p.thresh + 0.5f * knee : p.thresh;
        return gainComputer (levelDb, t, invR, knee);
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        rmsCoefOpt = timeCoef (10.0f, fs);
        rmsCoefSoft = timeCoef (5.0f, fs);
        memCharge = timeCoef (2000.0f, fs / kControlBlock);
        memDischarge = timeCoef (4000.0f, fs / kControlBlock);
        mixSm.prepare (fs, 20.0f);
        outSm.prepare (fs, 20.0f);
        reset();
    }

    void reset()
    {
        for (auto& c : chans) c = {};
        grDb = 0.0f;
        mem = 0.0f;
        counter = 0;
    }

    void setParams (const CompParams& p)
    {
        params = p;
        invRatio = p.ratio >= kRatioInfLocal ? 0.0f : 1.0f / p.ratio;
        fbRatio = std::min (p.ratio >= kRatioInfLocal ? 100.0f : p.ratio, 100.0f);
        knee = kneeFor (p.type);
        threshEff = p.type == 3 ? p.thresh + 0.5f * knee : p.thresh;

        float attack = p.attackMs;
        if (p.type == 1) attack *= 0.5f;                 // FET reacts faster
        if (p.type == 2) attack = std::max (attack, 5.0f); // opto cell cannot react instantly
        attackCoef = timeCoef (attack, fs);
        releaseCoef = timeCoef (p.releaseMs, fs);
        optFastCoef = timeCoef (std::max (40.0f, 0.1f * p.releaseMs), fs);

        hpActive = p.scHp > 21.0f;
        bellActive = std::abs (p.scBellGain) > 0.05f;
        if (hpActive) hpC = makeSvf (SvfType::HighPass, fs, p.scHp, 0.7071f);
        if (bellActive) bellC = makeSvf (SvfType::Bell, fs, p.scBellFreq, 1.0f, p.scBellGain);

        const float makeupDb = p.makeup ? -0.5f * staticGain (p, 0.0f) : 0.0f;
        outSm.setTarget (dbToGain (p.outDb + makeupDb));
        mixSm.setTarget (p.mix);
    }

    void process (float* const* ch, int numCh, int n, const float* const* key)
    {
        const bool useExt = params.ext && key != nullptr;
        const int type = params.type;
        float minGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            if ((counter++ % kControlBlock) == 0) updateControl();
            const float mix = mixSm.current, og = outSm.current;

            float k[2] {}, lvl[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                float s = useExt ? key[c][i] : ch[c][i];
                auto& st = chans[c];
                if (hpActive) s = st.hp.process (s, hpC);
                if (bellActive) s = st.bell.process (s, bellC);
                k[c] = s;
                if (type >= 2)
                {
                    const float rc = type == 2 ? rmsCoefOpt : rmsCoefSoft;
                    st.ms = s * s + (st.ms - s * s) * rc;
                    lvl[c] = 0.5f * fastGainToDb (st.ms); // power -> dB
                }
                else
                {
                    lvl[c] = fastGainToDb (std::abs (s));
                }
            }

            float gr[2];
            if (params.linked && numCh > 1)
            {
                gr[0] = gr[1] = detect (chans[0], std::max (lvl[0], lvl[1]));
                chans[1].y = chans[0].y;
            }
            else
            {
                for (int c = 0; c < numCh; ++c) gr[c] = detect (chans[c], lvl[c]);
            }

            for (int c = 0; c < numCh; ++c)
            {
                minGr = std::min (minGr, gr[c]);
                const float dry = ch[c][i];
                float wet = dry * fastDbToGain (gr[c]);
                if (type == 1)
                {
                    const float colour = 0.05f * std::min (1.0f, -gr[c] * (1.0f / 20.0f));
                    wet -= colour * wet * wet * wet;
                }
                const float out = dry + (wet - dry) * mix;
                ch[c][i] = params.listen ? k[c] : out * og;
            }
        }
        grDb = minGr;
    }

    float getGainReductionDb() const noexcept { return grDb; }

private:
    static constexpr float kRatioInfLocal = 30.0f;

    struct Chan
    {
        SvfState hp, bell;
        float ms = 0.0f, y = 0.0f; // y: smoothed gain in dB (<= 0)
    };

    void updateControl()
    {
        mixSm.tick();
        outSm.tick();
        if (params.type == 2)
        {
            // Opto memory charges while compressing hard, decays slowly.
            const float depth = std::min (1.0f, -grDbRunning * 0.1f);
            mem = depth + (mem - depth) * (depth > mem ? memCharge : memDischarge);
            optSlowCoef = timeCoef (params.releaseMs * (1.0f + 2.0f * mem), fs);
        }
    }

    inline float detect (Chan& st, float levelDb) noexcept
    {
        float y;
        switch (params.type)
        {
            case 1: // FET: implicit feedback
            {
                const float over = levelDb - params.thresh;
                const float a = attackCoef;
                float cand = st.y;
                if (over > 0.0f)
                {
                    const float r1 = fbRatio - 1.0f;
                    cand = (a * st.y - (1.0f - a) * r1 * over) / (1.0f + (1.0f - a) * r1);
                    cand = std::max (cand, -over); // never past the threshold
                }
                if (over > 0.0f && cand < st.y)
                    y = cand;
                else
                {
                    const float gc = gainComputer (levelDb, params.thresh, invRatio, 0.0f);
                    y = gc < st.y ? st.y : gc + (st.y - gc) * releaseCoef;
                }
                break;
            }
            case 2: // OPT: two-stage release
            {
                const float gc = gainComputer (levelDb, threshEff, invRatio, knee);
                if (gc < st.y) y = gc + (st.y - gc) * attackCoef;
                else
                {
                    const float coef = (gc - st.y) > 3.0f ? optFastCoef : optSlowCoef;
                    y = gc + (st.y - gc) * coef;
                }
                break;
            }
            default: // VCA / SOFT: branching smoothing in the log domain
            {
                const float gc = gainComputer (levelDb, threshEff, invRatio, knee);
                y = gc + (st.y - gc) * (gc < st.y ? attackCoef : releaseCoef);
                break;
            }
        }
        st.y = std::min (0.0f, y);
        grDbRunning = std::min (st.y, grDbRunning * 0.9999f);
        return st.y;
    }

    double fs = 48000.0;
    CompParams params;
    Chan chans[2];
    SvfCoefs hpC, bellC;
    bool hpActive = false, bellActive = false;
    float invRatio = 1.0f / 3.0f, fbRatio = 3.0f, knee = 2.0f, threshEff = -18.0f;
    float attackCoef = 0.0f, releaseCoef = 0.0f, optFastCoef = 0.0f, optSlowCoef = 0.0f;
    float rmsCoefOpt = 0.0f, rmsCoefSoft = 0.0f, memCharge = 0.0f, memDischarge = 0.0f, mem = 0.0f;
    float grDb = 0.0f, grDbRunning = 0.0f;
    int counter = 0;
    Smoothed mixSm, outSm;
};
} // namespace nl

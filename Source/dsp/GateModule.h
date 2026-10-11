#pragma once

#include "ParamSnapshot.h"
#include "Filters.h"

namespace nl
{
struct GateParams
{
    int type = 0; // 0 gate, 1 expander
    bool linked = true, ext = false, listen = false;
    float thresh = -40.0f, hyst = 4.0f, floorDb = -40.0f, ratio = 2.0f;
    float attackMs = 0.5f, holdMs = 30.0f, releaseMs = 150.0f, scHp = 20.0f, scLp = 20000.0f;
};

// Gate / downward expander with open/close hysteresis and hold. Opening is a
// one-pole in the linear domain (fast, click-free); closing falls at a constant
// rate in dB, like a natural decay, and "release" is the time to fall through
// the whole range down to the floor. No lookahead: zero latency.
class GateModule
{
public:
    // Latency in samples. Must depend only on non-automatable settings (quality,
    // lookahead), never on whether the module is on or on automatable parameters.
    int latency() const noexcept { return 0; }
    // Detector (key) level in dBFS after the sidechain filters, for the scope.
    float getKeyLevelDb() const noexcept { return -120.0f; } // TODO(gate)

    void prepare (double sampleRate, int /*maxBlock*/ = 4096)
    {
        fs = sampleRate;
        detRelease = timeCoef (10.0f, fs);
        reset();
    }

    void reset()
    {
        for (auto& c : chans) c = {};
        for (auto& c : chans) c.gain = 1.0f;
        grDb = 0.0f;
    }

    void setParams (const GateParams& p)
    {
        params = p;
        attackCoef = timeCoef (p.attackMs, fs);
        const float rangeDb = std::min (p.floorDb, -1.0f);
        releaseMul = fastDbToGain (rangeDb / std::max (1.0f, p.releaseMs * 0.001f * (float) fs));
        holdSamples = (int) (p.holdMs * 0.001f * (float) fs);
        hpActive = p.scHp > 21.0f;
        lpActive = p.scLp < 19500.0f;
        if (hpActive) hpC = makeSvf (SvfType::HighPass, fs, p.scHp, 0.7071f);
        if (lpActive) lpC = makeSvf (SvfType::LowPass, fs, p.scLp, 0.7071f);
        floorGain = dbToGain (p.floorDb);
    }

    // key: external sidechain channels or nullptr to use the input.
    void process (float* const* ch, int numCh, int n, const float* const* key)
    {
        const bool useExt = params.ext && key != nullptr;
        float minGain = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float k[2] {}, lvl[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                float s = useExt ? key[c][i] : ch[c][i];
                auto& st = chans[c];
                if (hpActive) s = st.hp.process (s, hpC);
                if (lpActive) s = st.lp.process (s, lpC);
                k[c] = s;
                const float a = std::abs (s);
                st.env = a > st.env ? a : a + (st.env - a) * detRelease;
                lvl[c] = st.env;
            }

            if (params.linked && numCh > 1)
            {
                const float g = runDetector (chans[0], std::max (lvl[0], lvl[1]));
                minGain = std::min (minGain, g);
                for (int c = 0; c < numCh; ++c)
                    ch[c][i] = params.listen ? k[c] : ch[c][i] * g;
            }
            else
            {
                for (int c = 0; c < numCh; ++c)
                {
                    const float g = runDetector (chans[c], lvl[c]);
                    minGain = std::min (minGain, g);
                    ch[c][i] = params.listen ? k[c] : ch[c][i] * g;
                }
            }
        }
        grDb = fastGainToDb (minGain);
    }

    float getGainReductionDb() const noexcept { return grDb; }

private:
    struct Chan
    {
        SvfState hp, lp;
        float env = 0.0f, gain = 1.0f;
        bool open = false;
        int hold = 0;
    };

    inline float runDetector (Chan& st, float level) noexcept
    {
        const float levelDb = fastGainToDb (level);
        const float openTh = params.thresh, closeTh = params.thresh - params.hyst;
        if (levelDb >= openTh) { st.open = true; st.hold = holdSamples; }
        else if (st.open)
        {
            if (levelDb >= closeTh) st.hold = holdSamples;
            else if (st.hold > 0) --st.hold;
            else st.open = false;
        }

        float target = 1.0f;
        if (! st.open)
        {
            if (params.type == 0) target = floorGain;
            else target = fastDbToGain (std::max (params.floorDb, (levelDb - params.thresh) * (params.ratio - 1.0f)));
        }
        if (target > st.gain) st.gain = target + (st.gain - target) * attackCoef;
        else st.gain = std::max (target, st.gain * releaseMul);
        return st.gain;
    }

    double fs = 48000.0;
    GateParams params;
    Chan chans[2];
    SvfCoefs hpC, lpC;
    bool hpActive = false, lpActive = false;
    float detRelease = 0.0f, attackCoef = 0.0f, releaseMul = 0.999f, floorGain = 0.01f, grDb = 0.0f;
    int holdSamples = 0;
};
// Maps the parameter snapshot to GATE settings (owned by this module).
inline GateParams readGateParams (const ParamSnapshot& p)
{
    GateParams gp;
    gp.type = std::min (p.choice (P::gateType), 1); // TODO(gate): Duck
    gp.linked = p.choice (P::gateMode) == 0;
    gp.ext = p.on (P::gateExt);
    gp.listen = p.on (P::gateListen);
    gp.thresh = p.f (P::gateThresh); gp.hyst = p.f (P::gateHyst); gp.floorDb = p.f (P::gateFloor); gp.ratio = p.f (P::gateRatio);
    gp.attackMs = p.f (P::gateAttack); gp.holdMs = p.f (P::gateHold); gp.releaseMs = p.f (P::gateRelease);
    gp.scHp = p.f (P::gateScHp); gp.scLp = p.f (P::gateScLp);
    return gp;
}
} // namespace nl

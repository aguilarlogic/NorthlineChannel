#pragma once

#include "Filters.h"
#include "SatSection.h"
#ifndef NL_PRE_STANDALONE
 #include "ParamSnapshot.h"
#endif
#include <algorithm>

namespace nl
{
struct PreParams
{
    bool satOn = false, hq = false, satAutoGain = true, hpOn = false, lpOn = false, weightOn = false, weightTrack = false;
    int satType = 0, hpSlope = 1, lpSlope = 1, filtTarget = 0, weightAmt = 0;
    float drive = 30.0f, satMix = 100.0f, satTone = 0.0f, opLevel = -18.0f;
    float hpFreq = 80.0f, hpRes = 0.7071f, lpFreq = 16000.0f, lpRes = 0.7071f, weightFreq = 72.0f;

    bool operator== (const PreParams&) const = default;
};

// PRE: high-pass -> saturation -> low-pass -> Weight. The high-pass sits before the shaper so
// sub-bass does not drive it; the low-pass follows it to tame generated harmonics.
//
// Parameter changes are latched: setParams() only stores the target, and the module picks it up
// at the next control tick (an absolute multiple of kControlBlock samples), so the output does
// not depend on the host block size or on where in a tick interval a parameter arrived.
class PreModule
{
public:
    // Latency in samples. Depends only on the quality setting (non-automatable): HQ delays the
    // audio by the oversampler latency whether or not saturation is on.
    int latency() const noexcept { return pending.hq ? sat::SatSection::kHqLatency : 0; }

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        (void) maxBlock; // the module works in kControlBlock segments
        hp.prepare (fs);
        lp.prepare (fs);
        hpFade.prepare (fs, 10.0f);
        lpFade.prepare (fs, 10.0f);
        sat.prepare (fs);
        weightFreq.prepare (fs, 40.0f);
        weightAmp.prepare (fs, 30.0f);
        reset();
    }

    void reset()
    {
        params = pending;
        clock.reset();
        hp.snapTo (true, params.hpFreq, params.hpSlope, params.hpRes);
        lp.snapTo (false, params.lpFreq, params.lpSlope, params.lpRes);
        hp.updateControl();
        lp.updateControl();
        hpFade.snap (params.hpOn);
        lpFade.snap (params.lpOn);
        sat.reset (toSat (params));
        weightFreq.setTarget (std::log (weightHz (params)));
        weightFreq.snap();
        weightAmp.setTarget (params.weightOn ? weightDb (params.weightAmt) : 0.0f);
        weightAmp.snap();
        for (auto& c : weightState) { c[0].reset(); c[1].reset(); }
        updateWeight();
    }

    void setParams (const PreParams& p) noexcept { pending = p; }

    void process (float* const* ch, int numCh, int n, const float* const* /*key*/ = nullptr)
    {
        int i = 0;
        while (i < n)
        {
            const int pos = (int) clock.phase;
            if (pos == 0) tick();
            const int len = std::min (n - i, kControlBlock - pos);
            float* seg[2] { ch[0] + i, numCh > 1 ? ch[1] + i : nullptr };
            bool chOn[2] { true, numCh > 1 };
            if (numCh > 1 && params.filtTarget > 0) { chOn[0] = params.filtTarget == 1; chOn[1] = params.filtTarget == 2; }

            runFilter (hp, hpFade, seg, numCh, len, chOn);
            sat.process (seg, numCh, len, pos);
            runFilter (lp, lpFade, seg, numCh, len, chOn);
            if (weightActive) runWeight (seg, numCh, len);

            clock.advance (len);
            i += len;
        }
    }

    // Combined magnitude of the filter sections for the EQ display.
    float filterMagnitude (double f) const
    {
        float m = 1.0f;
        if (params.hpOn) m *= hp.magnitude (f);
        if (params.lpOn) m *= lp.magnitude (f);
        return m;
    }

private:
    static float weightDb (int amt) noexcept { return amt <= 0 ? 2.0f : amt == 1 ? 4.0f : 6.0f; }

    static float weightHz (const PreParams& p) noexcept
    {
        const float f = p.weightTrack && p.hpOn ? p.hpFreq * 1.41421356f : p.weightFreq;
        return std::clamp (f, 40.0f, 120.0f);
    }

    static sat::SatParams toSat (const PreParams& p) noexcept
    {
        sat::SatParams s;
        s.on = p.satOn;
        s.hq = p.hq;
        s.autoGain = p.satAutoGain;
        s.type = std::clamp (p.satType, 0, shaper::numTypes - 1);
        s.drive = p.drive;
        s.mix = p.satMix;
        s.tone = p.satTone;
        s.opLevel = p.opLevel;
        return s;
    }

    void tick()
    {
        if (! (pending == params)) commit();
        hp.updateControl();
        lp.updateControl();
        if (weightFreq.tick() | weightAmp.tick()) updateWeight();
        sat.tick();
    }

    void commit()
    {
        params = pending;
        hp.setParams (true, params.hpFreq, params.hpSlope, params.hpRes);
        lp.setParams (false, params.lpFreq, params.lpSlope, params.lpRes);
        hpFade.setTarget (params.hpOn);
        lpFade.setTarget (params.lpOn);
        sat.commit (toSat (params));
        weightFreq.setTarget (std::log (weightHz (params)));
        weightAmp.setTarget (params.weightOn ? weightDb (params.weightAmt) : 0.0f);
    }

    void updateWeight()
    {
        const float f = std::exp (weightFreq.current), a = weightAmp.current;
        weightActive = a != 0.0f || weightAmp.target != 0.0f;
        weightB = makeSvf (SvfType::Bell, fs, 250.0f, 1.2f, -0.35f * a);
        // The 250 Hz cut lowers the gain at the bump, so the bump is raised by the same amount:
        // the response at the Weight frequency is the requested amount in dB.
        const float cutDb = 20.0f * std::log10 (std::max (1.0e-6f, weightB.magnitude (f, fs)));
        weightA = makeSvf (SvfType::Bell, fs, f, 0.9f, a - cutDb);
        if (! weightActive) for (auto& c : weightState) { c[0].reset(); c[1].reset(); }
    }

    void runWeight (float* const* seg, int numCh, int len) noexcept
    {
        for (int c = 0; c < numCh; ++c)
        {
            auto& st = weightState[c];
            float* x = seg[c];
            for (int i = 0; i < len; ++i) x[i] = st[1].process (st[0].process (x[i], weightA), weightB);
        }
    }

    void runFilter (SlopeFilter& f, Fade& fade, float* const* seg, int numCh, int len, const bool* chOn) noexcept
    {
        if (fade.isSilent()) return;
        if (fade.isFullyOn())
        {
            for (int i = 0; i < len; ++i)
            {
                for (int c = 0; c < numCh; ++c)
                    if (chOn[c]) seg[c][i] = f.processSample (c, seg[c][i]);
                f.advance();
            }
            return;
        }
        for (int i = 0; i < len; ++i)
        {
            const float w = fade.next();
            for (int c = 0; c < numCh; ++c)
            {
                const float x = seg[c][i];
                const float y = chOn[c] ? f.processSample (c, x) : x;
                seg[c][i] = x + (y - x) * w;
            }
            f.advance();
        }
        if (fade.isSilent()) f.clearState();
    }

    double fs = 48000.0;
    ControlClock clock;
    PreParams params, pending;
    SlopeFilter hp, lp;
    Fade hpFade, lpFade;
    sat::SatSection sat;
    Smoothed weightFreq, weightAmp;
    bool weightActive = false;
    SvfCoefs weightA, weightB;
    SvfState weightState[2][2];
};

#ifndef NL_PRE_STANDALONE
// Maps the parameter snapshot to PRE settings (owned by this module).
inline PreParams readPreParams (const ParamSnapshot& p)
{
    PreParams pp;
    pp.satOn = p.on (P::satOn);
    pp.satType = std::clamp (p.choice (P::satType), 0, shaper::numTypes - 1);
    pp.drive = p.f (P::satDrive);
    pp.satMix = p.f (P::satMix);
    pp.satTone = p.f (P::satTone);
    pp.satAutoGain = p.on (P::satAutoGain);
    pp.opLevel = p.f (P::opLevel);
    pp.hq = p.choice (P::quality) == 1;
    pp.hpOn = p.on (P::hpOn); pp.hpFreq = p.f (P::hpFreq); pp.hpSlope = std::clamp (p.choice (P::hpSlope), 0, 5); pp.hpRes = p.f (P::hpRes);
    pp.lpOn = p.on (P::lpOn); pp.lpFreq = p.f (P::lpFreq); pp.lpSlope = std::clamp (p.choice (P::lpSlope), 0, 5); pp.lpRes = p.f (P::lpRes);
    pp.filtTarget = p.choice (P::preMode) == 1 ? p.choice (P::filtTarget) : 0;
    pp.weightOn = p.on (P::weightOn);
    pp.weightFreq = p.f (P::weightFreq);
    pp.weightAmt = std::clamp (p.choice (P::weightAmt), 0, 2);
    pp.weightTrack = p.on (P::weightTrack);
    return pp;
}
#endif
} // namespace nl

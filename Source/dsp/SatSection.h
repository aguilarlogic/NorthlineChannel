#pragma once

#include "Oversampling.h"
#include "SatEngine.h"

namespace nl
{
namespace sat
{
struct SatParams
{
    bool on = false, hq = false, autoGain = true;
    int type = 0;
    float drive = 30.0f, mix = 100.0f, tone = 0.0f, opLevel = -18.0f; // percent, percent, percent (-100..100), dBFS

    bool operator== (const SatParams&) const = default;
};

// The stereo saturation section of PRE: engines for the old and the incoming type (cross-faded
// when the type changes), 4x oversampling in HQ, a latency-aligned dry path for Mix, and the
// calibrated gain staging. Everything that changes over time is driven from control ticks
// (commit() and tick() are called by the module on absolute multiples of kControlBlock) or by
// per-sample fades, so the output does not depend on the host block size.
class SatSection
{
public:
    static constexpr int kSeg = kControlBlock;
    // HQ latency in base samples: the oversampler plus the zero-phase droop and the ADAA-2 average (both at 4x).
    static constexpr int kHqLatency = Oversampler4::latency() + (kHqDroopDelay + 1) / 4;

    // Latency in base-rate samples; depends only on the quality setting.
    int latency() const noexcept { return hq ? kHqLatency : 0; }
    bool hqActive() const noexcept { return hq; }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        cal = &calibration (false);
        calHq = &calibration (true);
        droopEco = makeDroopFir (kEcoOrder, fs, fs);
        droopHq = makeDroopFirZeroPhase (kHqOrder, 4.0 * fs, fs, kHqDroopDelay);
        os.prepare (kSeg, 2);
        osTwin.prepare (kSeg, 2);
        delay.prepare (kHqLatency, 2);
        fadeOn.prepare (fs, 15.0f);
        fadeX.prepare (fs, 20.0f);
        driveSm.prepare (fs, 50.0f);
        toneSm.prepare (fs, 30.0f);
        mixSm.prepare (fs, 30.0f);
        autoSm.prepare (fs, 30.0f);
        opSm.prepare (fs, 100.0f);
        reset (params);
    }

    // Hard reset to the given parameters (no smoothing).
    void reset (const SatParams& p)
    {
        params = p;
        hq = p.hq;
        wantType = p.type;
        driveSm.setTarget (p.drive); driveSm.snap();
        toneSm.setTarget (p.tone * 0.01f); toneSm.snap();
        mixSm.setTarget (p.mix * 0.01f); mixSm.snap();
        autoSm.setTarget (p.autoGain ? 1.0f : 0.0f); autoSm.snap();
        opSm.setTarget (p.opLevel); opSm.snap();
        os.reset();
        osTwin.reset();
        delay.reset();
        fadeOn.snap (p.on);
        fadeX.snap (false);
        xfActive = false;
        curSlot = 0;
        warmLeft = 0;
        resetEngines();
    }

    // Parameter changes, applied by the module at a control tick.
    void commit (const SatParams& p)
    {
        if (p == params) return;
        const bool wasActive = isActive();
        if (p.hq != hq)
        {
            hq = p.hq;
            os.reset();
            osTwin.reset();
            delay.reset();
            if (wasActive) resetEngines();
        }
        wantType = p.type;
        driveSm.setTarget (p.drive);
        toneSm.setTarget (p.tone * 0.01f);
        mixSm.setTarget (p.mix * 0.01f);
        autoSm.setTarget (p.autoGain ? 1.0f : 0.0f);
        opSm.setTarget (p.opLevel);
        if (p.on && ! params.on)
        {
            if (! isActive()) { resetEngines(); os.reset(); warmLeft = hq ? 2 * kHqLatency : 8; }
            fadeOn.setTarget (true);
        }
        else if (! p.on && params.on)
        {
            fadeOn.setTarget (false);
        }
        params = p;
    }

    // Control-rate update: smoothers, type switching, gains and coefficients.
    void tick()
    {
        const bool toneMoved = toneSm.tick();
        driveSm.tick();
        mixSm.tick();
        autoSm.tick();
        opSm.tick();
        if (! isActive()) return;

        if (xfActive && fadeX.value >= 1.0f)
        {
            curSlot = 1 - curSlot;
            xfActive = false;
            fadeX.snap (false);
        }
        if (! xfActive && wantType != slot[curSlot].type) beginTypeSwitch();
        updateGains (false, toneMoved);
        for (int k = 0; k < (xfActive ? 2 : 1); ++k)
        {
            auto& s = slot[k == 0 ? curSlot : 1 - curSlot];
            for (int c = 0; c < 2; ++c) updateBias (s.cf, s.st[c]);
        }
    }

    // Processes `len` (<= kControlBlock) samples; `pos` is the position inside the control interval.
    void process (float* const* io, int numCh, int len, int pos)
    {
        float dry[2][kSeg];
        if (hq)
            for (int c = 0; c < numCh; ++c) delay.process (c, io[c], dry[c], len, kHqLatency);

        if (! isActive())
        {
            if (hq)
                for (int c = 0; c < numCh; ++c) std::copy (dry[c], dry[c] + len, io[c]);
            return;
        }

        // per-sample weights
        float sf[kSeg], xv[kSeg], mv[kSeg];
        const float mStep = (mTo - mFrom) * (1.0f / (float) kSeg);
        for (int i = 0; i < len; ++i)
        {
            if (warmLeft > 0) { --warmLeft; sf[i] = 0.0f; }
            else sf[i] = fadeOn.next();
            xv[i] = xfActive ? fadeX.next() : 0.0f;
            mv[i] = mFrom + mStep * (float) (pos + i);
        }

        auto ramp = [&] (const Slot& s, double scale)
        {
            Ramp r;
            const double t = (double) pos / (double) kSeg;
            r.g = s.gInFrom + (s.gInTo - s.gInFrom) * t;
            r.dg = (s.gInTo - s.gInFrom) / (double) kSeg * scale;
            r.go = s.gOutFrom + (s.gOutTo - s.gOutFrom) * t;
            r.dgo = (s.gOutTo - s.gOutFrom) / (double) kSeg * scale;
            return r;
        };

        for (int c = 0; c < numCh; ++c)
        {
            float* x = io[c];
            float wetA[kSeg], wetB[kSeg];
            if (hq)
            {
                float up[4 * kSeg], outA[4 * kSeg], outB[4 * kSeg];
                os.up (c, x, up, len);
                shape (slot[curSlot], c, up, outA, 4 * len, ramp (slot[curSlot], 0.25));
                if (xfActive)
                {
                    shape (slot[1 - curSlot], c, up, outB, 4 * len, ramp (slot[1 - curSlot], 0.25));
                    for (int i = 0; i < 4 * len; ++i) outA[i] += (outB[i] - outA[i]) * xv[i >> 2];
                }
                os.down (c, outA, wetA, len);
                // Linear twin of the oversampled path (droop FIR and ADAA average at 4x, same decimator), so the
                // mix is phase-coherent with the wet path; the integer dry delay alone is not.
                float lin4[4 * kSeg], lin[kSeg];
                linearTwin4 (c, up, lin4, 4 * len);
                osTwin.down (c, lin4, lin, len);
                for (int i = 0; i < len; ++i)
                {
                    const float mixed = lin[i] + mv[i] * (wetA[i] - lin[i]);
                    x[i] = dry[c][i] + sf[i] * (mixed - dry[c][i]);
                }
            }
            else
            {
                shape (slot[curSlot], c, x, wetA, len, ramp (slot[curSlot], 1.0));
                if (xfActive)
                {
                    shape (slot[1 - curSlot], c, x, wetB, len, ramp (slot[1 - curSlot], 1.0));
                    for (int i = 0; i < len; ++i) wetA[i] += (wetB[i] - wetA[i]) * xv[i];
                }
                // linear twin of the wet path (droop FIR + ADAA average) keeps Mix phase-coherent
                auto& tw = twin[c];
                const auto& d = slot[curSlot].cf.droop;
                for (int i = 0; i < len; ++i)
                {
                    tw.hist[tw.pos] = x[i];
                    float acc = 0.0f;
                    for (int k = 0; k < d.taps; ++k) acc += d.h[(size_t) k] * tw.hist[(tw.pos - k) & 63];
                    tw.pos = (tw.pos + 1) & 63;
                    const float lin = 0.5f * (acc + tw.prev);
                    tw.prev = acc;
                    const float mixed = lin + mv[i] * (wetA[i] - lin);
                    x[i] += sf[i] * (mixed - x[i]);
                }
            }
        }
    }

private:
    struct Slot
    {
        Coefs cf;
        State st[2];
        int type = -1;
        double gInFrom = 1.0, gInTo = 1.0, gOutFrom = 1.0, gOutTo = 1.0;
        float lsTrim = 0.0f, hsTrim = 0.0f;
    };

    struct Twin
    {
        float hist[64] {};
        int pos = 0;
        float prev = 0.0f;
        void reset() { std::fill (std::begin (hist), std::end (hist), 0.0f); pos = 0; prev = 0.0f; }
    };

    struct Twin4
    {
        float hist[64] {};
        int pos = 0;
        float d1 = 0.0f, d2 = 0.0f;
        void reset() { std::fill (std::begin (hist), std::end (hist), 0.0f); pos = 0; d1 = d2 = 0.0f; }
    };

    // Linear response of the 4x path: droop FIR, then the order-2 ADAA average (u + x1 + x2) / 3.
    void linearTwin4 (int c, const float* up4, float* lin4, int n4) noexcept
    {
        auto& tw = twin4[c];
        const auto& d = droopHq;
        for (int i = 0; i < n4; ++i)
        {
            tw.hist[tw.pos] = up4[i];
            float acc = 0.0f;
            for (int k = 0; k < d.taps; ++k) acc += d.h[(size_t) k] * tw.hist[(tw.pos - k) & 63];
            tw.pos = (tw.pos + 1) & 63;
            lin4[i] = (acc + tw.d1 + tw.d2) * (1.0f / 3.0f);
            tw.d2 = tw.d1;
            tw.d1 = acc;
        }
    }

    bool isActive() const noexcept { return fadeOn.target > 0.5f || fadeOn.value > 0.0f || warmLeft > 0; }

    void shape (Slot& s, int c, const float* in, float* out, int n, const Ramp& r) noexcept
    {
        process_ (s, c, in, out, n, r);
    }
    void process_ (Slot& s, int c, const float* in, float* out, int n, const Ramp& r) noexcept
    {
        sat::process (s.st[c], s.cf, in, out, n, r);
    }

    void configureSlot (Slot& s, int type)
    {
        s.type = type;
        s.cf.configure (type, hq ? 4.0 * fs : fs, hq ? kHqOrder : kEcoOrder, hq ? droopHq : droopEco);
        s.cf.setTone (toneSm.current);
        s.st[0].reset (s.cf);
        s.st[1].reset (s.cf);
        s.lsTrim = s.hsTrim = 0.0f;
        applyGains (s, true);
    }

    void resetEngines()
    {
        xfActive = false;
        fadeX.snap (false);
        curSlot = 0;
        configureSlot (slot[0], wantType);
        slot[1].type = -1;
        for (auto& t : twin) t.reset();
        for (auto& t : twin4) t.reset();
        mFrom = mTo = mixSm.current;
    }

    void beginTypeSwitch()
    {
        auto& incoming = slot[1 - curSlot];
        configureSlot (incoming, wantType);
        fadeX.snap (false);
        fadeX.setTarget (true);
        xfActive = true;
    }

    void applyGains (Slot& s, bool snap)
    {
        const auto& table = hq ? *calHq : *cal;
        const auto d = driveSetting (table, s.type, shaper::driveDb (s.type, driveSm.current), (double) opSm.current, (double) autoSm.current);
        s.gInFrom = snap ? std::pow (10.0, d.gInDb / 20.0) : s.gInTo;
        s.gOutFrom = snap ? std::pow (10.0, d.gOutDb / 20.0) : s.gOutTo;
        s.gInTo = std::pow (10.0, d.gInDb / 20.0);
        s.gOutTo = std::pow (10.0, d.gOutDb / 20.0);
        s.cf.biasScale = d.biasScale;
        if (std::abs (d.lsTrim - s.lsTrim) > 1.0e-3f || std::abs (d.hsTrim - s.hsTrim) > 1.0e-3f)
        {
            s.lsTrim = d.lsTrim;
            s.hsTrim = d.hsTrim;
            s.cf.setTrim (d.lsTrim, d.hsTrim);
        }
    }

    void updateGains (bool snap, bool toneMoved)
    {
        for (int k = 0; k < (xfActive ? 2 : 1); ++k)
        {
            auto& s = slot[k == 0 ? curSlot : 1 - curSlot];
            if (toneMoved) s.cf.setTone (toneSm.current);
            applyGains (s, snap);
        }
        mFrom = snap ? mixSm.current : mTo;
        mTo = mixSm.current;
    }

    double fs = 48000.0;
    const std::array<CalTable, shaper::numTypes>* cal = nullptr;
    const std::array<CalTable, shaper::numTypes>* calHq = nullptr;
    DroopFir droopEco, droopHq;
    Oversampler4 os, osTwin;
    DelayLine delay;
    Fade fadeOn, fadeX;
    Smoothed driveSm, toneSm, mixSm, autoSm, opSm;
    Slot slot[2];
    Twin twin[2];
    Twin4 twin4[2];
    SatParams params;
    int curSlot = 0, wantType = 0, warmLeft = 0;
    bool hq = false, xfActive = false;
    float mFrom = 1.0f, mTo = 1.0f;
};
} // namespace sat
} // namespace nl

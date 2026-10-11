#pragma once

#include "Common.h"
#include "LoudnessTruePeak.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace nl
{
// Output limiter with three modes. Real-time safe; allocates only in prepare().
//
//  Safety Clip  Smooth soft clipper, 0 latency in Eco. Transparent (bit-exact) below 0.7 x ceiling,
//               first-order ADAA near and above the ceiling. In HQ it runs inside a 4x half-band FIR
//               oversampler and reports that latency (rounded up to an integer, padded exactly).
//  Brickwall    Lookahead peak limiter: sliding minimum of the required gain over the lookahead,
//               program-dependent release, box-filtered gain ramp. 1.5 ms latency.
//  True Peak    Brickwall with 4x polyphase inter-sample peak detection; same latency, the detector's
//               own delay is taken out of the lookahead.
//
// Latency depends on (mode, quality) only. When the limiter is off the audio still runs through the
// same delay (delayed dry), and on/off is a 10 ms crossfade between aligned wet and dry, so timing
// never shifts and nothing clicks. Everything the audio thread does is driven by the control clock
// (absolute multiples of kControlBlock), so the output does not depend on the host block size.
class Limiter
{
public:
    enum Mode { SafetyClip = 0, Brickwall = 1, TruePeak = 2 };

    struct Params
    {
        bool on = false;
        int mode = SafetyClip;
        bool hq = false;
        float ceilingDb = -0.3f;
        float releaseMs = 100.0f;
    };

    static constexpr float kKnee = 0.7f; // Safety Clip is transparent below this fraction of the ceiling
    static constexpr float kTruePeakMargin = 0.98855f; // -0.1 dB

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        lookahead = std::clamp ((int) std::lround (0.0015 * fs), 2, kRing / 2);
        tpKernel.design (fs, 8); // 8x grid for the limiter detector (the meter keeps the standard 4x)
        for (auto& r : ring) r.fill (0.0f);
        dq.assign ((size_t) lookahead + 4, 0.0f);
        dqIdx.assign ((size_t) lookahead + 4, 0u);
        box.assign ((size_t) lookahead + 1, 1.0f);

        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
        os->initProcessing ((size_t) kPrimeHistory);
        const float exact = os->getLatencyInSamples();
        hqLatency = (int) std::ceil (exact - 1.0e-4f);
        hqExtra4 = std::clamp ((int) std::lround ((hqLatency - exact) * 4.0f), 0, 3);

        fade.prepare (fs, 10.0f);
        outFade.prepare (fs, 10.0f);
        ceilingSm.prepare (fs, 20.0f);
        resetNow();
    }

    // Stores targets only. The audio thread adopts them at the next control tick.
    void setParams (const Params& p) noexcept
    {
        pending = p;
        reportedLatency.store (latencyFor (p.mode, p.hq), std::memory_order_relaxed);
    }

    // Reported latency, from (mode, quality) only. Safe from any thread.
    int latency() const noexcept { return reportedLatency.load (std::memory_order_relaxed); }

    // Adopts the pending parameters at once and clears all state (prepare / reset paths).
    void resetNow()
    {
        active = pending;
        latencyActive = latencyFor (active.mode, active.hq);
        reportedLatency.store (latencyActive, std::memory_order_relaxed);
        clearAudioState();
        configureMode();
        fade.snap (active.on);
        outFade.snap (true);
        ceilingSm.target = pending.ceilingDb;
        ceilingSm.current = pending.ceilingDb;
        ceilingSm.first = false;
        cFrom = cTo = dbToGain (pending.ceilingDb);
        lastCeilingTarget = pending.ceilingDb;
        updateRelease();
        resetGainState();
        primeFromHistory();
        armHold = 0;
        outHold = 0;
        switching = false;
        segMinGain = 1.0f;
    }

    // Control-rate tick: call when the control clock phase is 0, before the segment's samples.
    void controlTick() noexcept
    {
        const bool configChange = pending.mode != active.mode || pending.hq != active.hq;
        if (configChange && ! switching)
        {
            switching = true; // fade the old configuration out before swapping (the swap clears the delay line)
            outFade.setTarget (false);
        }
        else if (! configChange && switching)
        {
            switching = false; // the request was withdrawn
            outFade.setTarget (true);
        }
        if (switching && outFade.value <= 0.0f)
        {
            switching = false;
            active.mode = pending.mode;
            active.hq = pending.hq;
            latencyActive = latencyFor (active.mode, active.hq);
            reportedLatency.store (latencyActive, std::memory_order_relaxed);
            clearAudioState();
            configureMode();
            fade.snap (active.on);
            outFade.value = 0.0f; // silence while the cleared delay line drains, then a short fade-in
            outFade.target = 1.0f;
            outHold = latencyActive;
            resetGainState();
            primeFromHistory();
            armHold = armSamples();
        }
        if (pending.on != active.on)
        {
            if (pending.on && fade.isSilent())
            {
                resetGainState(); // off -> on: nothing from before may leak into the new run
                primeFromHistory();
            }
            active.on = pending.on;
            fade.setTarget (active.on);
            armHold = armSamples();
        }
        if (pending.releaseMs != active.releaseMs)
        {
            active.releaseMs = pending.releaseMs;
            updateRelease();
        }
        if (pending.ceilingDb != lastCeilingTarget)
        {
            lastCeilingTarget = pending.ceilingDb;
            armHold = armSamples();
        }

        ceilingSm.setTarget (pending.ceilingDb);
        cFrom = cTo;
        if (ceilingSm.tick()) cTo = dbToGain (ceilingSm.current);
    }

    // Processes one segment of at most kControlBlock samples that does not cross a control tick.
    // `phase` is the control clock phase of the first sample.
    void processSegment (float* const* io, int numCh, int len, int phase) noexcept
    {
        const int nc = numCh >= 2 ? 2 : 1;
        segMinGain = 1.0f;
        const float dC = (cTo - cFrom) * (1.0f / (float) kControlBlock);
        const float c0 = cFrom + dC * (float) phase;

        if (active.mode == SafetyClip)
        {
            if (active.hq) processSafetyHq (io, nc, len, c0, dC);
            else processSafetyEco (io, nc, len, c0, dC);
        }
        else
        {
            processLookahead (io, nc, len, c0, dC);
        }

        if (outHold > 0 || ! outFade.isFullyOn())
            for (int j = 0; j < len; ++j)
            {
                float g = 0.0f;
                if (outHold > 0) --outHold; // the cleared delay line is still draining: stay silent
                else g = outFade.next();
                for (int c = 0; c < nc; ++c) io[c][j] *= g;
            }
        armHold = std::max (0, armHold - len);
    }

    // Lowest gain (dB, <= 0) in the last segment, including the on/off crossfade.
    float segmentGainReductionDb() const noexcept { return gainToDb (segMinGain); }

    // Linear level above which output samples count as overs, or 0 while the ceiling is not in
    // force (limiter off, fading, ceiling moving, just switched).
    float oversThreshold() const noexcept
    {
        if (! active.on || ! fade.isFullyOn() || armHold > 0 || ceilingSm.current != ceilingSm.target) return 0.0f;
        return cTo * (active.mode == TruePeak ? 1.0116f : 1.0012f); // +0.1 dB for true peak, +0.01 dB for samples
    }
    bool oversUseTruePeak() const noexcept { return active.mode == TruePeak; }

    int hqLatencySamples() const noexcept { return hqLatency; }
    int lookaheadSamples() const noexcept { return lookahead; }

private:
    static constexpr int kRing = 2048;         // power of two
    static constexpr int kRingMask = kRing - 1;
    static constexpr int kPrimeHistory = 64;   // input history replayed into detectors when the limiter starts

    int latencyFor (int mode, bool hq) const noexcept { return mode == SafetyClip ? (hq ? hqLatency : 0) : lookahead; }
    int armSamples() const noexcept { return latencyActive + (int) (0.06 * fs); }

    static float sane (float x) noexcept { return std::abs (x) <= 1.0e6f ? x : 0.0f; }

    //==========================================================================
    // Safety Clip transfer function in units of the ceiling: identity up to kKnee, then
    // kKnee + (1 - kKnee) tanh ((a - kKnee) / (1 - kKnee)). C2 at the knee, approaches 1 from below.
    static double softF (double a) noexcept
    {
        constexpr double k = kKnee;
        return a <= k ? a : k + (1.0 - k) * std::tanh ((a - k) / (1.0 - k));
    }
    // Antiderivative of softF (even extension).
    static double softF1 (double a) noexcept
    {
        constexpr double k = kKnee;
        if (a <= k) return 0.5 * a * a;
        const double t = (a - k) / (1.0 - k);
        const double lnCosh = t + std::log1p (std::exp (-2.0 * t)) - 0.6931471805599453;
        return 0.5 * k * k + k * (a - k) + (1.0 - k) * (1.0 - k) * lnCosh;
    }

    // Memoryless clip, for the oversampled path.
    static float clipPlain (float x, float c) noexcept
    {
        const double u = (double) x / (double) c, a = std::abs (u);
        if (a <= (double) kKnee) return x;
        return (float) ((u < 0.0 ? -1.0 : 1.0) * (double) c * softF (a));
    }

    // First-order ADAA clipper blended in as the level approaches the ceiling, so it is transparent
    // below the knee and never exceeds the ceiling (both blend inputs stay below it).
    static float clipAdaa (float x, float xp, float c) noexcept
    {
        const double inv = 1.0 / (double) c, u = (double) x * inv, v = (double) xp * inv;
        const double m = std::max (std::abs (u), std::abs (v));
        if (m <= (double) kKnee) return x;
        const double d = u - v;
        double y;
        if (std::abs (d) < 1.0e-6)
        {
            const double mid = 0.5 * (u + v);
            y = (mid < 0.0 ? -1.0 : 1.0) * softF (std::abs (mid));
        }
        else
        {
            y = (softF1 (std::abs (u)) - softF1 (std::abs (v))) / d;
        }
        const double t = std::min (1.0, (m - (double) kKnee) / (1.0 - (double) kKnee));
        const double w = t * t * (3.0 - 2.0 * t);
        return (float) ((double) c * ((1.0 - w) * u + w * y));
    }

    void processSafetyEco (float* const* io, int nc, int len, float c0, float dC) noexcept
    {
        for (int j = 0; j < len; ++j)
        {
            const float c = c0 + dC * (float) j;
            const bool wetOn = ! fade.isSilent();
            const float w = wetOn ? fade.next() : 0.0f;
            for (int ch = 0; ch < nc; ++ch)
            {
                const float x = sane (io[ch][j]);
                float out = x;
                if (wetOn)
                {
                    const float y = clipAdaa (x, xPrev[ch], c);
                    out = x + w * (y - x);
                    const float ax = std::abs (x);
                    if (ax > kKnee * c && ax > 1.0e-9f)
                        segMinGain = std::min (segMinGain, 1.0f + (std::abs (y) / ax - 1.0f) * w);
                }
                xPrev[ch] = x;
                io[ch][j] = out;
            }
        }
    }

    // Runs `len` samples through the 4x oversampler with the soft clipper; returns delayed-by-hqLatency
    // output in wet[].
    void hqChain (const float* const* in, float* const* wet, int nc, int len, float c0, float dC) noexcept
    {
        juce::dsp::AudioBlock<const float> inBlock (in, (size_t) nc, (size_t) len);
        auto up = os->processSamplesUp (inBlock);
        const int n4 = len * 4;
        for (int ch = 0; ch < nc; ++ch)
        {
            float* u = up.getChannelPointer ((size_t) ch);
            for (int k = 0; k < n4; ++k)
            {
                const float c = c0 + dC * (float) (k >> 2);
                u[k] = clipPlain (u[k], c);
            }
            if (hqExtra4 > 0) // pad the 4x signal so the total latency is exactly hqLatency samples
            {
                float* h = edHist[ch].data();
                int hp = edPos[ch];
                for (int k = 0; k < n4; ++k)
                {
                    h[hp] = u[k];
                    u[k] = h[(hp - hqExtra4) & 3];
                    hp = (hp + 1) & 3;
                }
                edPos[ch] = hp;
            }
        }
        juce::dsp::AudioBlock<float> outBlock (wet, (size_t) nc, (size_t) len);
        os->processSamplesDown (outBlock);
    }

    void processSafetyHq (float* const* io, int nc, int len, float c0, float dC) noexcept
    {
        float inB[2][kControlBlock], dryD[2][kControlBlock], wetB[2][kControlBlock];
        for (int j = 0; j < len; ++j)
        {
            for (int ch = 0; ch < nc; ++ch)
            {
                const float x = sane (io[ch][j]);
                inB[ch][j] = x;
                ring[(size_t) ch][(size_t) (wp & kRingMask)] = x;
                dryD[ch][j] = ring[(size_t) ch][(size_t) ((wp - (uint32_t) latencyActive) & kRingMask)];
            }
            ++wp;
        }
        if (fade.isSilent())
        {
            for (int ch = 0; ch < nc; ++ch)
                for (int j = 0; j < len; ++j) io[ch][j] = dryD[ch][j];
            return;
        }
        const float* inPtr[2] { inB[0], inB[1] };
        float* wetPtr[2] { wetB[0], wetB[1] };
        hqChain (inPtr, wetPtr, nc, len, c0, dC);
        for (int j = 0; j < len; ++j)
        {
            const float w = fade.next();
            for (int ch = 0; ch < nc; ++ch)
            {
                io[ch][j] = dryD[ch][j] + w * (wetB[ch][j] - dryD[ch][j]);
                segMinGain = std::min (segMinGain, 1.0f);
            }
            // Gain reduction estimate: ratio of wet to dry magnitude where the clipper is working.
            for (int ch = 0; ch < nc; ++ch)
            {
                const float ad = std::abs (dryD[ch][j]);
                if (ad > kKnee * (c0 + dC * (float) j) && ad > 1.0e-9f)
                    segMinGain = std::min (segMinGain, 1.0f + (std::abs (wetB[ch][j]) / ad - 1.0f) * w);
            }
        }
    }

    //==========================================================================
    void processLookahead (float* const* io, int nc, int len, float c0, float dC) noexcept
    {
        const bool tpMode = active.mode == TruePeak;
        const int La = windowLen;
        const uint32_t cap = (uint32_t) La + 1u;
        const int rs = La + 3; // deque ring size (never holds more than cap entries)
        for (int j = 0; j < len; ++j)
        {
            float x[2] { 0.0f, 0.0f }, d[2] { 0.0f, 0.0f };
            for (int ch = 0; ch < nc; ++ch)
            {
                x[ch] = sane (io[ch][j]);
                ring[(size_t) ch][(size_t) (wp & kRingMask)] = x[ch];
                d[ch] = ring[(size_t) ch][(size_t) ((wp - (uint32_t) latencyActive) & kRingMask)];
            }
            ++wp;

            if (fade.isSilent())
            {
                for (int ch = 0; ch < nc; ++ch) io[ch][j] = d[ch];
                continue;
            }

            // True Peak mode keeps 0.1 dB in hand: applying a gain that varies across the interpolation span
            // can raise the inter-sample peak a little above gain x (input peak).
            const float c = (c0 + dC * (float) j) * (tpMode ? kTruePeakMargin : 1.0f);
            float peak;
            if (tpMode)
            {
                float bound = 0.0f;
                for (int ch = 0; ch < nc; ++ch)
                {
                    tp[ch].push (x[ch]);
                    bound = std::max (bound, tp[ch].windowMax());
                }
                const bool skip = bound * tpKernel.l1 * 1.1f <= c;
                peak = 0.0f;
                for (int ch = 0; ch < nc; ++ch) peak = std::max (peak, tp[ch].slotPeak (tpKernel, true, skip));
            }
            else
            {
                peak = std::abs (x[0]);
                if (nc > 1) peak = std::max (peak, std::abs (x[1]));
            }
            const float req = peak > c ? c / peak : 1.0f;

            // Sliding minimum of the required gain over the lookahead window.
            while (dqTail != dqHead && (sampleIndex - dqIdx[(size_t) dqHead]) >= cap) dqHead = dqHead + 1 == rs ? 0 : dqHead + 1;
            while (dqTail != dqHead)
            {
                const int last = dqTail == 0 ? rs - 1 : dqTail - 1;
                if (dq[(size_t) last] < req) break;
                dqTail = last;
            }
            dq[(size_t) dqTail] = req;
            dqIdx[(size_t) dqTail] = sampleIndex;
            dqTail = dqTail + 1 == rs ? 0 : dqTail + 1;
            const float wmin = dq[(size_t) dqHead];
            ++sampleIndex;

            // Two-stage release: instant attack onto the window minimum; a fast and a slow recovery
            // run side by side and the slow one takes over as gain reduction becomes sustained.
            hf = wmin < hf ? wmin : wmin + (hf - wmin) * rcFast;
            hs = wmin < hs ? wmin : wmin + (hs - wmin) * rcSlow;
            const float m = std::clamp (sustain * (1.0f / 0.15f), 0.0f, 1.0f);
            const float h = hf + (hs - hf) * m;
            sustain += (1.0f - wmin - sustain) * sustainK; // depth of the *required* reduction (not the released gain: no feedback)

            // Box filter over the lookahead turns the steps into a smooth ramp that reaches the
            // required gain exactly when the peak leaves the delay line.
            boxSum += (double) h - (double) box[(size_t) boxPos];
            box[(size_t) boxPos] = h;
            boxPos = boxPos + 1 == La ? 0 : boxPos + 1;
            const float gain = std::min (1.0f, (float) (boxSum / (double) La));

            const float w = fade.next();
            const float geff = 1.0f + (gain - 1.0f) * w;
            for (int ch = 0; ch < nc; ++ch) io[ch][j] = d[ch] * geff;
            segMinGain = std::min (segMinGain, geff);
        }
    }

    //==========================================================================
    void updateRelease() noexcept
    {
        rcFast = timeCoef (std::max (1.0f, 0.25f * active.releaseMs), fs);
        rcSlow = timeCoef (4.0f * active.releaseMs, fs);
        sustainK = (float) (1.0 - std::exp (-1.0 / (0.004 * (double) active.releaseMs * fs)));
    }

    void clearAudioState() noexcept
    {
        for (auto& r : ring) r.fill (0.0f);
        wp = 0;
        xPrev[0] = xPrev[1] = 0.0f;
        for (auto& e : edHist) e.fill (0.0f);
        edPos[0] = edPos[1] = 0;
        if (os != nullptr) os->reset();
    }

    void configureMode() noexcept
    {
        const int tpDelay = active.mode == TruePeak ? TruePeakKernel::kHalf : 0;
        windowLen = std::max (2, lookahead - tpDelay);
    }

    void resetGainState() noexcept
    {
        dqHead = dqTail = 0;
        sampleIndex = 0;
        hf = hs = 1.0f;
        sustain = 0.0f;
        std::fill (box.begin(), box.end(), 1.0f);
        boxPos = 0;
        boxSum = (double) windowLen;
        for (auto& t : tp) t.reset();
    }

    // Replays the recent input into the stateful detectors so they match a run that never stopped.
    void primeFromHistory() noexcept
    {
        if (active.mode == TruePeak)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int k = TruePeakKernel::kTaps; k >= 1; --k)
                    tp[ch].push (ring[(size_t) ch][(size_t) ((wp - (uint32_t) k) & kRingMask)]);
        }
        else if (active.mode == SafetyClip && active.hq && os != nullptr)
        {
            os->reset();
            for (auto& e : edHist) e.fill (0.0f);
            edPos[0] = edPos[1] = 0;
            float in[2][kControlBlock], out[2][kControlBlock];
            for (int chunk = 0; chunk < kPrimeHistory; chunk += kControlBlock)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int k = 0; k < kControlBlock; ++k)
                        in[ch][k] = ring[(size_t) ch][(size_t) ((wp - (uint32_t) (kPrimeHistory - chunk - k)) & kRingMask)];
                const float* inPtr[2] { in[0], in[1] };
                float* outPtr[2] { out[0], out[1] };
                hqChain (inPtr, outPtr, 2, kControlBlock, cTo, 0.0f);
            }
        }
    }

    //==========================================================================
    double fs = 48000.0;
    int lookahead = 72, windowLen = 72, hqLatency = 0, hqExtra4 = 0;
    Params pending, active;
    std::atomic<int> reportedLatency { 0 };
    int latencyActive = 0;

    Smoothed ceilingSm;
    float cFrom = 1.0f, cTo = 1.0f, lastCeilingTarget = -0.3f;
    Fade fade, outFade;
    int armHold = 0, outHold = 0;
    bool switching = false;
    float segMinGain = 1.0f;

    // Delay line shared by the dry and the wet path.
    std::array<std::array<float, kRing>, 2> ring {};
    uint32_t wp = 0;

    // Safety Clip.
    float xPrev[2] {};
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::array<std::array<float, 4>, 2> edHist {};
    int edPos[2] {};

    // Brickwall / True Peak.
    TruePeakKernel tpKernel;
    TruePeakChannel tp[2];
    std::vector<float> dq, box;
    std::vector<uint32_t> dqIdx;
    int dqHead = 0, dqTail = 0, boxPos = 0;
    uint32_t sampleIndex = 0;
    float hf = 1.0f, hs = 1.0f, sustain = 0.0f, rcFast = 0.0f, rcSlow = 0.0f, sustainK = 0.0f;
    double boxSum = 0.0;
};
} // namespace nl

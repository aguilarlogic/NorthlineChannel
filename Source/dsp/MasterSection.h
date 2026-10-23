#pragma once

#include "Limiter.h"
#include "MeterHub.h"
#include "ParamSnapshot.h"

namespace nl
{
// Last stage of the strip, after output gain, level match and global mix: the limiter followed by
// the processor-owned meters. Real-time safe. Latency depends only on lim_mode and quality.
//
// The block is cut into segments at absolute multiples of kControlBlock (the persistent
// ControlClock), so parameter changes, smoothing and the output do not depend on the host's
// block size.
class MasterSection
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        limiter.prepare (fs, maxBlock);
        meterHub.prepare (fs, maxBlock);
        clock.reset();
    }

    void reset (const ParamSnapshot& p)
    {
        setParams (p);
        limiter.resetNow();
        meterHub.reset();
        clock.reset();
        grDb = 0.0f;
    }

    // Stores targets only; takes effect at the next control tick.
    void setParams (const ParamSnapshot& p)
    {
        Limiter::Params lp;
        lp.on = p.on (P::limOn);
        lp.mode = std::clamp (p.choice (P::limMode), 0, 2);
        lp.hq = p.choice (P::quality) == 1;
        lp.ceilingDb = p.f (P::limCeiling);
        lp.releaseMs = p.f (P::limRelease);
        limiter.setParams (lp);
    }

    // Reported latency in samples: a function of lim_mode and quality only (never of lim_on).
    int latency() const noexcept { return limiter.latency(); }

    void process (float* const* io, int numCh, int n) noexcept
    {
        numCh = std::clamp (numCh, 1, 2);
        float minGr = 0.0f;
        int i = 0;
        while (i < n)
        {
            const int phase = (int) clock.phase;
            const int len = std::min (n - i, kControlBlock - phase);
            if (phase == 0) limiter.controlTick();

            float* seg[2] { io[0] + i, numCh > 1 ? io[1] + i : nullptr };
            limiter.processSegment (seg, numCh, len, phase);

            const float gr = limiter.segmentGainReductionDb();
            minGr = std::min (minGr, gr);
            meterHub.setOverThreshold (limiter.oversThreshold(), limiter.oversUseTruePeak());
            meterHub.pushGainReduction (gr, len);
            const float* cseg[2] { seg[0], seg[1] };
            meterHub.accumulate (cseg, numCh, len);

            clock.advance (len);
            i += len;
        }
        meterHub.publish();
        grDb = minGr;
    }

    // Lowest limiter gain (dB, <= 0) over the last block.
    float getGainReductionDb() const noexcept { return grDb; }
    MeterHub& meters() noexcept { return meterHub; }
    const MeterHub& meters() const noexcept { return meterHub; }
    const Limiter& getLimiter() const noexcept { return limiter; }

private:
    double fs = 48000.0;
    Limiter limiter;
    MeterHub meterHub;
    ControlClock clock;
    float grDb = 0.0f;
};
} // namespace nl

#pragma once

#include "Limiter.h"
#include "MeterHub.h"
#include "ParamSnapshot.h"
#include <vector>

namespace nl
{
// Last stage of the strip, after output gain, level match and global mix: the limiter
// (with click-free on/off) followed by the processor-owned meters. Real-time safe.
class MasterSection
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        limiter.prepare (fs);
        limFade.prepare (fs, 10.0f);
        for (auto& d : dry) d.assign ((size_t) std::max (1, maxBlock), 0.0f);
        meterHub.prepare (fs, maxBlock);
    }

    void reset (const ParamSnapshot& p)
    {
        setParams (p);
        limiter.reset();
        limFade.snap (on);
        grDb = 0.0f;
        meterHub.reset();
    }

    void setParams (const ParamSnapshot& p)
    {
        on = p.on (P::limOn);
        mode = p.choice (P::limMode);
        limiter.setParams (p.f (P::limCeiling), p.f (P::limRelease), mode != 0);
        limFade.setTarget (on);
    }

    // Reported latency in samples. TODO(master): must depend on lim_mode / quality only (D2).
    int latency() const noexcept { return limiter.latencySamples (on && mode != 0); }

    void process (float* const* io, int numCh, int n) noexcept
    {
        grDb = 0.0f;
        if (on || ! limFade.isSilent())
        {
            if (mode != 0)
            {
                // Lookahead changes latency, so no cross-fade: the host re-aligns.
                limFade.snap (on);
                if (limFade.isFullyOn()) limiter.process (io, numCh, n);
            }
            else if (limFade.isFullyOn())
            {
                limiter.process (io, numCh, n);
            }
            else
            {
                for (int c = 0; c < numCh; ++c) std::copy (io[c], io[c] + n, dry[c].data());
                limiter.process (io, numCh, n);
                for (int i = 0; i < n; ++i)
                {
                    const float w = limFade.next();
                    for (int c = 0; c < numCh; ++c) io[c][i] = dry[c][i] + (io[c][i] - dry[c][i]) * w;
                }
            }
            grDb = limiter.getGainReductionDb();
        }
        meterHub.process (io, numCh, n);
    }

    float getGainReductionDb() const noexcept { return grDb; }
    MeterHub& meters() noexcept { return meterHub; }
    const MeterHub& meters() const noexcept { return meterHub; }

private:
    double fs = 48000.0;
    Limiter limiter;
    Fade limFade;
    MeterHub meterHub;
    std::vector<float> dry[2];
    bool on = false;
    int mode = 0;
    float grDb = 0.0f;
};
} // namespace nl

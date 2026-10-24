// Temporary: CPU probe (to be replaced by the real tests).
#include "MasterSignals.h"

using namespace nl;

namespace tool
{
using namespace master;

static double nowS() { return (double) juce::Time::getHighResolutionTicks() / (double) juce::Time::getHighResolutionTicksPerSecond(); }

template <typename F>
static double bestOf (int runs, F&& f)
{
    f();
    double best = 1.0e9;
    for (int i = 0; i < runs; ++i) { const double t0 = nowS(); f(); best = std::min (best, nowS() - t0); }
    return best;
}

NL_TEST (zz_meterCpu)
{
    juce::ScopedNoDenormals noDenormals;
    const double fs = 48000.0, secs = 20.0;
    auto sig = pinkStereo ((int) (fs * secs), -1.0f);
    const float* ch[2] { sig.getReadPointer (0), sig.getReadPointer (1) };
    const int N = sig.getNumSamples();

    auto hubRun = [&] (int block)
    {
        auto hub = makeHub (fs, 64);
        return bestOf (5, [&]
        {
            for (int pos = 0; pos + block <= N; pos += block)
            {
                const float* p[2] { ch[0] + pos, ch[1] + pos };
                hub->process (p, 2, block);
            }
        });
    };
    std::printf ("CPU hub 64-block   %.4f %%\n", 100.0 * hubRun (64) / secs);
    std::printf ("CPU hub 4096-block %.4f %%\n", 100.0 * hubRun (4096) / secs);

    // K-weighting only (both channels interleaved, as in MeterHub).
    {
        TruePeakKernel k; k.design (fs);
        BiquadD p[2]; KWeighting::design (fs, p[0], p[1]);
        BiquadD kw[2][2] { { p[0], p[1] }, { p[0], p[1] } };
        double sink = 0;
        const double t = bestOf (5, [&]
        {
            double e0 = 0, e1 = 0;
            for (int j = 0; j < N; ++j)
            {
                const double y0 = kw[0][1].process (kw[0][0].process ((double) ch[0][j]));
                const double y1 = kw[1][1].process (kw[1][0].process ((double) ch[1][j]));
                e0 += y0 * y0; e1 += y1 * y1;
            }
            sink += e0 + e1;
        });
        std::printf ("CPU K-weight only  %.4f %% (%g)\n", 100.0 * t / secs, sink > 0 ? 1.0 : 0.0);
    }
    // True peak only (push, bound, skip, slotPeak), as in MeterHub.
    {
        TruePeakKernel k; k.design (fs);
        TruePeakChannel tp[2];
        float sink = 0;
        const double t = bestOf (5, [&]
        {
            float tpMax = 0;
            for (int j = 0; j < N; ++j)
            {
                float bound = 0;
                for (int c = 0; c < 2; ++c) { tp[c].push (ch[c][j]); bound = std::max (bound, tp[c].windowMax()); }
                const bool skip = bound * k.l1 * 1.1f <= tpMax;
                float peak = 0;
                for (int c = 0; c < 2; ++c) peak = std::max (peak, tp[c].slotPeak (k, false, skip));
                tpMax = std::max (tpMax, peak);
            }
            sink += tpMax;
        });
        std::printf ("CPU true-peak only %.4f %% (%g)\n", 100.0 * t / secs, sink > 0 ? 1.0 : 0.0);
    }
    {
        // Histogram-free check: sub-block sum loop cost (300 adds per 480 samples).
        std::array<double, 300> ring {};
        for (int i = 0; i < 300; ++i) ring[(size_t) i] = 0.001 * i;
        double sink = 0;
        const double t = bestOf (5, [&]
        {
            int head = 0;
            for (int b = 0; b < N / 480; ++b)
            {
                ring[(size_t) head] = 0.5;
                head = (head + 1) % 300;
                double sumM = 0, sumS = 0;
                for (int k = 1; k <= 300; ++k)
                {
                    const double v = ring[(size_t) ((head - k + 300) % 300)];
                    sumS += v;
                    if (k <= 40) sumM += v;
                }
                sink += sumM + sumS;
            }
        });
        std::printf ("CPU sub-block sums %.4f %% (%g)\n", 100.0 * t / secs, sink > 0 ? 1.0 : 0.0);
    }
    {
        TruePeakKernel k; k.design (fs);
        for (int variant = 0; variant < 3; ++variant)
        {
            TruePeakChannel tp[2];
            float sink = 0;
            const double t = bestOf (5, [&]
            {
                float tpMax = 0;
                int skips = 0;
                for (int j = 0; j < N; ++j)
                {
                    float bound = 0;
                    for (int c = 0; c < 2; ++c) { tp[c].push (ch[c][j]); bound = std::max (bound, tp[c].windowMax()); }
                    const bool skip = variant == 2 ? true : (bound * k.l1 * 1.1f <= tpMax);
                    skips += skip;
                    if (variant == 0) continue; // push + bound only
                    float peak = 0;
                    for (int c = 0; c < 2; ++c) peak = std::max (peak, tp[c].slotPeak (k, false, skip));
                    tpMax = std::max (tpMax, peak);
                }
                sink += tpMax + skips;
            });
            std::printf ("CPU TP variant %d  %.4f %% (%g)\n", variant, 100.0 * t / secs, sink > 0 ? 1.0 : 0.0);
        }
    }
    check (true, "probe");
}
} // namespace tool

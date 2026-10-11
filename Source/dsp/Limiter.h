#pragma once

#include "Common.h"
#include <vector>

namespace nl
{
// Brickwall output limiter.
//  Zero-latency mode: instant attack, so sample peaks never exceed the ceiling.
//  Lookahead mode: 1.5 ms of delay (reported to the host); sliding-minimum gain,
//  release smoothing and a box filter give a smooth gain ramp that still
//  reaches the required gain before the peak leaves the delay line.
class Limiter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lookahead = std::max (1, (int) std::lround (0.0015 * fs));
        ringSize = lookahead + 3;
        for (auto& d : delay) d.assign ((size_t) lookahead, 0.0f); // delay of exactly lookahead samples
        minVals.assign ((size_t) ringSize, 1.0f);
        minIdx.assign ((size_t) ringSize, 0);
        box.assign ((size_t) lookahead, 1.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        boxSum = (double) lookahead;
        head = 0;
        dqHead = dqTail = 0;
        sampleIndex = 0;
        g = 1.0f;
        h = 1.0f;
        grDb = 0.0f;
    }

    int latencySamples (bool useLookahead) const noexcept { return useLookahead ? lookahead : 0; }

    void setParams (float ceilingDb, float releaseMs, bool useLookahead)
    {
        ceiling = dbToGain (ceilingDb);
        releaseCoef = timeCoef (releaseMs, fs);
        if (useLookahead != lookaheadOn) { lookaheadOn = useLookahead; reset(); }
    }

    void process (float* const* ch, int numCh, int n)
    {
        float minGain = 1.0f;
        if (! lookaheadOn)
        {
            for (int i = 0; i < n; ++i)
            {
                float peak = std::abs (ch[0][i]);
                if (numCh > 1) peak = std::max (peak, std::abs (ch[1][i]));
                const float req = peak > ceiling ? ceiling / peak : 1.0f;
                g = req < g ? req : req + (g - req) * releaseCoef;
                for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
                minGain = std::min (minGain, g);
            }
        }
        else
        {
            const int cap = lookahead + 1;
            for (int i = 0; i < n; ++i)
            {
                float peak = std::abs (ch[0][i]);
                if (numCh > 1) peak = std::max (peak, std::abs (ch[1][i]));
                const float req = peak > ceiling ? ceiling / peak : 1.0f;

                // Sliding minimum over the last lookahead + 1 samples (monotonic deque
                // in a ring of ringSize slots; it never holds more than cap entries).
                const int rs = ringSize;
                while (dqTail != dqHead && minIdx[(size_t) dqHead] <= sampleIndex - cap) dqHead = (dqHead + 1) % rs;
                while (dqTail != dqHead && minVals[(size_t) ((dqTail - 1 + rs) % rs)] >= req)
                    dqTail = (dqTail - 1 + rs) % rs;
                minVals[(size_t) dqTail] = req;
                minIdx[(size_t) dqTail] = sampleIndex;
                dqTail = (dqTail + 1) % rs;
                const float wmin = minVals[(size_t) dqHead];
                ++sampleIndex;

                h = wmin < h ? wmin : wmin + (h - wmin) * releaseCoef;

                const size_t bi = (size_t) (sampleIndex % lookahead);
                boxSum += (double) h - (double) box[bi];
                box[bi] = h;
                const float gain = std::min (1.0f, (float) (boxSum / lookahead));

                for (int c = 0; c < numCh; ++c)
                {
                    auto& d = delay[c];
                    const float delayed = d[(size_t) head];
                    d[(size_t) head] = ch[c][i];
                    ch[c][i] = delayed * gain;
                }
                head = (head + 1) % lookahead;
                minGain = std::min (minGain, gain);
            }
            // Re-accumulate the box sum occasionally to stop float drift.
            if ((sampleIndex & 0xffff) < n)
            {
                double s = 0.0;
                for (float v : box) s += v;
                boxSum = s;
            }
        }
        grDb = gainToDb (minGain);
    }

    float getGainReductionDb() const noexcept { return grDb; }

private:
    double fs = 48000.0;
    int lookahead = 72, ringSize = 75, head = 0, dqHead = 0, dqTail = 0;
    long long sampleIndex = 0;
    bool lookaheadOn = false;
    float ceiling = 1.0f, releaseCoef = 0.0f, g = 1.0f, h = 1.0f, grDb = 0.0f;
    double boxSum = 0.0;
    std::vector<float> delay[2], minVals, box;
    std::vector<long long> minIdx;
};
} // namespace nl

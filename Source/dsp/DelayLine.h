#pragma once

#include <algorithm>
#include <vector>

namespace nl
{
// Multi-channel integer delay. All memory is allocated in prepare(); process() and write() are
// real-time safe. The delay may be changed at any time (the output then jumps, so only do that
// for non-automatable settings such as lookahead or quality).
class DelayLine
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (int numChannels, int maxDelaySamples)
    {
        channels = std::clamp (numChannels, 1, kMaxChannels);
        maxDelay = std::max (0, maxDelaySamples);
        int size = 1;
        while (size < maxDelay + 1) size <<= 1;
        mask = size - 1;
        for (int c = 0; c < kMaxChannels; ++c) buf[c].assign ((size_t) (c < channels ? size : 1), 0.0f);
        pos = 0;
        delay = std::min (delay, maxDelay);
    }

    void reset() noexcept
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        pos = 0;
    }

    void setDelay (int samples) noexcept { delay = std::clamp (samples, 0, maxDelay); }
    int getDelay() const noexcept { return delay; }
    int getMaxDelay() const noexcept { return maxDelay; }

    // Pushes n samples per channel and writes the delayed samples to `out`. `in` and `out` may
    // alias (in-place), or be the same pointers.
    void process (const float* const* in, float* const* out, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, channels);
        for (int c = 0; c < numCh; ++c)
        {
            float* b = buf[c].data();
            const float* x = in[c];
            float* y = out[c];
            unsigned w = (unsigned) pos;
            const unsigned m = (unsigned) mask, d = (unsigned) delay;
            for (int i = 0; i < n; ++i, ++w)
            {
                const float v = x[i];
                b[w & m] = v;
                y[i] = b[(w - d) & m];
            }
        }
        pos = (int) (((unsigned) pos + (unsigned) n) & (unsigned) mask);
    }

    // Pushes n samples per channel without reading (keeps the line current while it is not heard).
    void write (const float* const* in, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, channels);
        for (int c = 0; c < numCh; ++c)
        {
            float* b = buf[c].data();
            const float* x = in[c];
            unsigned w = (unsigned) pos;
            const unsigned m = (unsigned) mask;
            for (int i = 0; i < n; ++i, ++w) b[w & m] = x[i];
        }
        pos = (int) (((unsigned) pos + (unsigned) n) & (unsigned) mask);
    }

private:
    std::vector<float> buf[kMaxChannels];
    int channels = 2, maxDelay = 0, mask = 0, pos = 0, delay = 0;
};
} // namespace nl

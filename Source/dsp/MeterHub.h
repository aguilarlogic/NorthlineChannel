#pragma once

#include "Common.h"
#include <atomic>

namespace nl
{
// Values the UI reads from MeterHub. Loudness in LUFS / LU, levels in dB.
struct MeterSnapshot
{
    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0;
    double maxMomentary = -200.0, maxShortTerm = -200.0, lra = 0.0;
    float truePeakDb = -200.0f, samplePeakDb = -200.0f, correlation = 0.0f, vuDb = -200.0f;
    uint32_t sequence = 0;
};

// Processor-owned loudness and peak metering (BS.1770 / EBU R128), fed on the audio thread
// after the limiter, so it measures exactly what leaves the plugin, also while the editor is
// closed and during offline renders. The UI reads consistent snapshots with read().
// TODO(master): implement (roadmap D6). This stub keeps the interface stable for integration.
class MeterHub
{
public:
    void prepare (double /*sampleRate*/, int /*maxBlock*/) {}
    void reset() noexcept {}
    void resetIntegrated() noexcept {}
    void process (const float* const* /*io*/, int /*numCh*/, int /*n*/) noexcept {}
    MeterSnapshot read() const noexcept { return {}; }
};
} // namespace nl

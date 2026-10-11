#pragma once

#include "../Params.h"
#include "CompModule.h"
#include "DeEsserModule.h"
#include "EqModule.h"
#include "GateModule.h"
#include "Limiter.h"
#include "Meters.h"
#include "PreModule.h"
#include <array>
#include <atomic>

namespace nl
{
// The whole signal path. Owned by the processor; process() is real-time safe:
// no allocation, locks or system calls (verified by the tool's allocation test).
class ChannelStrip
{
public:
    using Order = std::array<int, numModules>;
    static constexpr Order defaultOrder { modPre, modGate, modDs, modEq, modComp };

    explicit ChannelStrip (juce::AudioProcessorValueTreeState& state);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // In-place processing of numChannels (1 or 2) channels. sidechain may be null.
    void process (float* const* io, int numChannels, int numSamples, const float* const* sidechain, int sidechainChannels);

    int getLatencySamples() const noexcept;

    // Module order (message thread writes, audio thread reads).
    void setOrder (const Order& order) noexcept;
    Order getOrder() const noexcept;
    static bool isValidOrder (const Order& order) noexcept;
    static uint32_t packOrder (const Order& order) noexcept;
    static Order unpackOrder (uint32_t packed) noexcept;

    // Metering (consumer side).
    SpscRing<MeterFrame>& meterFrames() noexcept { return frames; }
    SpscRing<StereoSample>& outputSamples() noexcept { return samples; }
    void setSampleStreamingEnabled (bool on) noexcept { streamSamples.store (on); }
    double getSampleRate() const noexcept { return fs; }
    int getNumChannels() const noexcept { return channels; }

private:
    float pv (int index) const noexcept { return values[(size_t) index]->load (std::memory_order_relaxed); }
    bool pb (int index) const noexcept { return pv (index) >= 0.5f; }
    int pi (int index) const noexcept { return (int) std::lround (pv (index)); }

    void readParams();
    bool moduleWantsMs (int id) const noexcept;
    bool moduleEnabled (int id) const noexcept;
    void runModule (int id, float* const* io, int numChannels, int n, const float* const* key);
    void resetModule (int id);

    std::array<std::atomic<float>*, P::count> values {};

    double fs = 48000.0;
    int maxBlock = 512, channels = 2;

    PreModule pre;
    GateModule gate;
    DeEsserModule ds;
    EqModule eq;
    CompModule comp;
    Limiter limiter;

    std::array<Fade, numModules> moduleFade {};
    std::atomic<uint32_t> packedOrder { 0 };

    Smoothed inGain, outGain, matchGain;
    Fade limFade;
    double matchInPow = 0.0, matchOutPow = 0.0, matchCoef = 0.0;
    bool limLookaheadActive = false;

    std::vector<float> dry[2], key[2], keyMs[2];
    std::vector<StereoSample> sampleScratch;

    SpscRing<MeterFrame> frames;
    SpscRing<StereoSample> samples;
    std::atomic<bool> streamSamples { false };
    std::array<float, 6> moduleUsSmoothed {};
    double ticksToUs = 1.0;
};
} // namespace nl

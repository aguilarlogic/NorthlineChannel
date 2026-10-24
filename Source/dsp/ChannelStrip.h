#pragma once

#include "../Params.h"
#include "CompModule.h"
#include "DeEsserModule.h"
#include "DelayLine.h"
#include "EqModule.h"
#include "GateModule.h"
#include "LevelMatch.h"
#include "MasterSection.h"
#include "Meters.h"
#include "PreModule.h"
#include <array>
#include <atomic>

namespace nl
{
// The whole signal path. Owned by the processor; process() is real-time safe:
// no allocation, locks or system calls (verified by the tool's allocation test).
//
//   input stage (swap, polarity, gain)
//     -> dry tap (delayed by the chain latency)
//     -> module chain in the user's order (bypassed modules delay their input by their latency)
//     -> level match -> global mix -> output gain
//     -> master (limiter, meters) -> monitor
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

    // Total latency in samples: the sum of the module latencies plus the master section. Updated
    // on the audio thread when a latency-relevant setting changes; safe to read from any thread.
    int getLatencySamples() const noexcept { return latencyTotal.load (std::memory_order_relaxed); }
    // The part before the global mix tap (modules only).
    int getChainLatencySamples() const noexcept { return latencyChain.load (std::memory_order_relaxed); }

    // Module order (message thread writes, audio thread reads).
    void setOrder (const Order& order) noexcept;
    Order getOrder() const noexcept;
    static bool isValidOrder (const Order& order) noexcept;
    static uint32_t packOrder (const Order& order) noexcept;
    static Order unpackOrder (uint32_t packed) noexcept;

    //==============================================================================
    // Metering (consumer side, one consumer thread). The rings are allocated once, in the
    // constructor; prepare() bumps ringGeneration instead of reallocating. The pop functions
    // discard stale content when the generation changed (and report it through wasReset), so a
    // consumer never reads data from before a prepare(). meterFrames()/outputSamples() give
    // direct access for code that handles the generation itself.
    SpscRing<MeterFrame>& meterFrames() noexcept { return frames; }
    SpscRing<StereoSample>& outputSamples() noexcept { return samples; }
    uint32_t getRingGeneration() const noexcept { return ringGeneration.load (std::memory_order_acquire); }
    size_t popMeterFrames (MeterFrame* out, size_t maxCount, bool* wasReset = nullptr) noexcept;
    size_t popOutputSamples (StereoSample* out, size_t maxCount, bool* wasReset = nullptr) noexcept;

    void setSampleStreamingEnabled (bool on) noexcept { streamSamples.store (on); }
    MeterHub& meterHub() noexcept { return master.meters(); }
    double getSampleRate() const noexcept { return fs; }
    int getNumChannels() const noexcept { return channels; }

    //==============================================================================
    // Level Match (D1). Message-thread API; the processor mirrors a learned trim into match_trim.
    void relearnLevelMatch() noexcept { levelMatch.requestRelearn(); }
    // Keeps the current match_trim (used after a session or preset load in Learn mode).
    void holdLevelMatch() noexcept { levelMatch.requestHold(); }
    LevelMatchStatus getLevelMatchStatus() const noexcept { return levelMatch.getStatus(); }

    //==============================================================================
    // Test and measurement aids (message thread, not while processing).
    //  setTestModuleLatency: makes a module behave as if it had `samples` more latency (its
    //  output is delayed), to exercise the latency alignment before real lookahead exists.
    //  getParamUpdateCount: number of setParams calls a module received (index numModules = master).
    //  setForceFullParamUpdates: bypasses the dirty flags (every module is updated every block).
    void setTestModuleLatency (int moduleId, int extraSamples);
    uint32_t getParamUpdateCount (int index) const noexcept { return paramUpdates[(size_t) index].load (std::memory_order_relaxed); }
    void setForceFullParamUpdates (bool on) noexcept { forceFull.store (on); }
    static int maxModuleLatency (double sampleRate) noexcept { return (int) std::ceil (0.020 * sampleRate) + 1024; }

    //==============================================================================
    // Compare and Delta (roadmap 9.3). Transient: never saved, reset when a state loads. Written from the
    // message thread, read by the audio thread once per block.
    //  Compare target: -1 off, 0 the whole strip (the input, latency-aligned, replaces the output), or
    //                  1 + moduleId, which bypasses that module with its learned level added back.
    //  Delta: PRE, GATE and EQ output minus its latency-aligned input. DE-ESS and COMP have their own
    //         Delta listen mode, which the processor sets, so setDelta ignores them here.
    void setCompare (int target) noexcept { compareTarget.store (std::clamp (target, -1, (int) numModules)); }
    int getCompare() const noexcept { return compareTarget.load (std::memory_order_relaxed); }
    void setDelta (int moduleId, bool on) noexcept;
    bool getDelta (int moduleId) const noexcept;

    // Gain reduction as the meters show it, for the Instance Hub: 0 gate, 1 de-ess band 1, 2 de-ess
    // band 2, 3 largest dynamic EQ band, 4 compressor, 5 limiter. Updated once per block.
    float getHubGrDb (int index) const noexcept { return hubGr[(size_t) index].load (std::memory_order_relaxed); }

private:
    float pv (int index) const noexcept { return snap.f (index); }
    bool pb (int index) const noexcept { return snap.on (index); }
    int pi (int index) const noexcept { return snap.choice (index); }

    void updateParams (bool force);
    void refreshLatency();
    bool moduleWantsMs (int id) const noexcept;
    bool moduleEnabled (int id) const noexcept;
    void runModule (int id, float* const* io, int numChannels, int n, const float* const* key);
    void resetModule (int id);
    void inputStage (float* const* io, int numChannels, int n);
    void matchStage (float* const* io, int numChannels, int n);
    void mixStage (float* const* io, int numChannels, int n);

    // Compare / Delta (9.3).
    struct ModuleLearn { float ew = 0.0f, ed = 0.0f, gainLin = 1.0f; }; // smoothed energy wet and dry, learned wet/dry level
    void learnModule (int id, const float* const* wet, const float* const* dry, int numChannels, int n, uint32_t phase) noexcept;
    void applyDelta (int id, float* const* io, const float* const* dry, int numChannels, int n, uint32_t phase) noexcept;
    void applyWholeCompare (float* const* io, int numChannels, int n, uint32_t phase) noexcept;
    void applyModuleCompare (float* const* io, int numChannels, int n, uint32_t phase) noexcept;

    //==============================================================================
    struct Monitor
    {
        static constexpr size_t kMatrixSize = 4;
        std::array<float, kMatrixSize> from { 1, 0, 0, 1 }, to { 1, 0, 0, 1 };
        float t = 1.0f, step = 0.01f;
        int mode = 0;

        void prepare (double sampleRate, float ms) noexcept { step = (float) (1.0 / std::max (1.0, 0.001 * ms * sampleRate)); }
        static std::array<float, kMatrixSize> matrixFor (int mode) noexcept;
        void snap (int newMode) noexcept { mode = newMode; from = to = matrixFor (newMode); t = 1.0f; }
        void set (int newMode) noexcept;
        bool isIdentity() const noexcept { return t >= 1.0f && to[0] == 1.0f && to[1] == 0.0f && to[2] == 0.0f && to[3] == 1.0f; }
        void process (float* l, float* r, int n) noexcept;
    };

    std::array<std::atomic<float>*, P::count> values {};
    ParamSnapshot snap, prevSnap; // plain values captured once per block; the previous block's, for dirty flags
    bool haveSnap = false;
    std::atomic<bool> forceFull { false };
    std::array<std::atomic<uint32_t>, numModules + 1> paramUpdates {};

    double fs = 48000.0;
    int maxBlock = 512, channels = 2;
    ControlClock clock; // one clock for the whole strip, advanced once per block after every user

    PreModule pre;
    GateModule gate;
    DeEsserModule ds;
    EqModule eq;
    CompModule comp;
    MasterSection master;

    std::array<Fade, numModules> moduleFade {};
    std::atomic<uint32_t> packedOrder { 0 };

    // Latency alignment.
    std::array<DelayLine, numModules> bypassDelay; // module input delayed by the module latency
    std::array<DelayLine, numModules> testWet;     // wet-side delay used by setTestModuleLatency only
    DelayLine dryDelay;                             // input-stage output delayed by the chain latency
    std::array<int, numModules> moduleLat {}, testExtra {}, holdoff {};
    std::atomic<int> latencyTotal { 0 }, latencyChain { 0 };

    // Strip-level gains, all deterministic control-rate ramps.
    std::array<RampedGain, 2> inGain;
    RampedValue swapAmt, mixAmt;
    RampedGain matchGain, outGain;
    float lastMatchDb = 0.0f;
    LevelMatch levelMatch;
    Monitor monitor;

    std::vector<float> fadeDry[2], tapDry[2], key[2], keyMs[2];
    std::vector<StereoSample> sampleScratch;

    SpscRing<MeterFrame> frames;
    SpscRing<StereoSample> samples;
    std::atomic<uint32_t> ringGeneration { 0 };
    uint32_t seenFrameGeneration = 0, seenSampleGeneration = 0; // consumer side
    std::atomic<bool> streamSamples { false };
    std::array<float, 6> moduleUsSmoothed {};
    double ticksToUs = 1.0;

    // Compare and Delta state.
    std::atomic<int> compareTarget { -1 }, deltaMask { 0 };
    int lastCompareTarget = -1;                  // audio thread
    RampedValue wholeW, modW, modGainV;          // compare weights and the bypassed module's learned gain
    RampedValue deltaW[numModules];              // Delta weights (PRE, GATE, EQ)
    RampedGain cmpOutGain;                       // output gain for the whole-strip bypass path
    DelayLine cmpDelay;                          // input stage output delayed by the total latency
    std::vector<float> cmpBuf[2], dryScratch[2], modGainBuf;
    ModuleLearn learn[numModules];
    float learnAlpha = 0.0f;
    std::array<std::atomic<float>, 6> hubGr {};  // read by the Instance Hub
};
} // namespace nl

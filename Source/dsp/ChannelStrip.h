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
#include "ScopeTap.h"
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
    // Scope taps for the module displays: one per module slot, plus DE-ESS band 2 (its own GR and
    // key level). Consumer side: one UI component per tap.
    ScopeTap& scope (int moduleId) noexcept { return scopes[(size_t) moduleId]; }
    ScopeTap& deEsserBand2Scope() noexcept { return dsBand2Scope; }

    // Analyser taps: the mono signal (Mid, or the average of L and R in stereo) entering and leaving
    // the EQ slot, and leaving the PRE slot. Written only while an analyser is attached. Each tap has
    // one consumer thread (the analyser that owns it).
    enum class AnalyserTap : int { eqIn = 0, eqOut, preOut, count };
    void analyserStart() noexcept { analyserUsers.fetch_add (1); }
    void analyserStop() noexcept { analyserUsers.fetch_sub (1); }
    size_t popAnalyser (AnalyserTap tap, float* out, size_t maxCount) noexcept;

    // Published once per block with relaxed atomics, for the UI.
    float getEqDynGainDb (int band) const noexcept { return eqDynLive[(size_t) band].load (std::memory_order_relaxed); }
    // Peak level of the external sidechain input in dBFS (-120 when no sidechain is connected).
    float getSidechainDb() const noexcept { return scLive.load (std::memory_order_relaxed); }

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

    // Views (see the public accessors above).
    void scopeSlot (int id, const float* const* in, const float* const* out, int numCh, int n, bool silent) noexcept;
    void tapMono (AnalyserTap tap, const float* const* io, int numCh, int n, bool ms) noexcept;
    std::array<ScopeTap, numModules> scopes;
    ScopeTap dsBand2Scope;
    std::array<SpscRing<float>, (size_t) AnalyserTap::count> analyserRings;
    std::array<uint32_t, (size_t) AnalyserTap::count> seenAnalyserGeneration {};
    std::atomic<int> analyserUsers { 0 };
    std::vector<float> scopeIn[2], analyserScratch;
    std::array<std::atomic<float>, 4> eqDynLive {};
    std::atomic<float> scLive { -120.0f };
};
} // namespace nl

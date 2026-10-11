#pragma once

#include "Common.h"
#include "Filters.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace nl
{
// What the UI shows for Level Match.
struct LevelMatchStatus
{
    enum State : int { Idle = 0, Learning, Learned, Live };
    State state = Idle;
    float progress = 0.0f;      // 0..1 while learning, 1 once learned
    float gainDb = 0.0f;        // gain the strip is currently asked to apply
    float learnedTrimDb = 0.0f; // last trim published by Learn
    uint32_t learnedSerial = 0; // increments every time a trim is published
};

// Level Match v2 (roadmap D1). Compares the loudness of a reference (the signal after the input
// stage, delayed to line up with the chain output) with the loudness of the measured signal (the
// chain output, before the match gain), both K-weighted (BS.1770) in 400 ms blocks with 75 %
// overlap (one block per 100 ms hop).
//
// A block counts only if the reference is above max(-60 LUFS, running reference - 20 LU) and the
// chain is not essentially muting it, so silence and gated passages never change the gain. The
// running reference is a slow average of the counted blocks; it re-bases itself after 60 s spent
// above the absolute gate but below the relative one (a lasting level change, not a pause).
//
//  Off  : unity. Nothing is measured.
//  Learn: measures counted blocks until at least 3 s are in and the estimate has been stable
//         within 0.2 LU for the last second (10 s at most), then publishes a static trim
//         (learnedSerial/learnedTrimDb; the processor mirrors it into match_trim) and stops
//         measuring. The gain is that trim; it follows match_trim when the parameter changes.
//  Live : the gain follows the (averaged) loudness difference, updated only on counted blocks, at
//         most 1 dB/s, held when nothing counts, clamped to +/-12 dB. Not written to a parameter.
//
// The processing side is driven by the strip: update() once per block, then process() for
// segments that end at hop boundaries so a new gain is requested at an exact sample position.
class LevelMatch
{
public:
    enum Mode : int { Off = 0, Learn = 1, Live = 2 };

    static constexpr float kMaxGainDb = 12.0f;
    static constexpr double kAbsoluteGateLufs = -60.0, kRelativeGateLu = 20.0;
    static constexpr int kLearnMinBlocks = 30, kLearnMaxBlocks = 100, kStableBlocks = 10;
    static constexpr float kStableRangeLu = 0.2f;
    static constexpr double kLiveAverageSeconds = 3.0, kRunningAverageSeconds = 60.0, kRebaseSeconds = 60.0;
    static constexpr float kLiveSlewDbPerSecond = 1.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hopSamples = std::max (1, (int) std::lround (0.1 * fs));
        hopSeconds = (double) hopSamples / fs;
        designKWeighting();
        resetMeasurement();
        publish();
    }

    // Clears the signal history. Mode, learned/live gain and state are kept, except that an
    // unfinished Learn starts over.
    void resetMeasurement() noexcept
    {
        for (auto& s : kw)
            for (auto& c : s)
                for (auto& b : c) b.reset();
        for (auto& a : acc) a[0] = a[1] = 0.0;
        for (auto& h : hopE) h.fill (0.0);
        hopCount = 0;
        hopFill = 0;
        hopsSeen = 0;
        runValid = false;
        liveValid = false;
        rebaseHops = 0;
        if (mode == Learn && state == LevelMatchStatus::Learning) clearLearn();
        publish();
    }

    //==============================================================================
    // Message-thread requests (consumed by update()).
    void requestRelearn() noexcept { relearnRequested.store (true); }
    void requestHold() noexcept { holdRequested.store (true); }

    LevelMatchStatus getStatus() const noexcept
    {
        LevelMatchStatus s;
        s.learnedSerial = uiSerial.load (std::memory_order_acquire);
        s.state = (LevelMatchStatus::State) uiState.load (std::memory_order_relaxed);
        s.progress = uiProgress.load (std::memory_order_relaxed);
        s.gainDb = uiGain.load (std::memory_order_relaxed);
        s.learnedTrimDb = uiLearned.load (std::memory_order_relaxed);
        return s;
    }

    //==============================================================================
    // Audio thread. Call once per block before process().
    void update (int newMode, float trimParamDb) noexcept
    {
        if (newMode != mode) enter ((Mode) newMode, trimParamDb);
        if (relearnRequested.exchange (false))
        {
            if (mode == Learn) startLearning();
            else if (mode == Live) { liveValid = false; runValid = false; }
        }
        if (holdRequested.exchange (false) && mode == Learn)
        {
            state = LevelMatchStatus::Learned;
            progress = 1.0f;
            applied = trimParamDb;
            lastParam = trimParamDb;
        }
        if (mode == Learn && ! (lastParam == trimParamDb))
        {
            lastParam = trimParamDb;
            if (std::abs (trimParamDb - applied) > 0.005f) applied = trimParamDb;
        }
        publish();
    }

    float gainDb() const noexcept { return mode == Off ? 0.0f : (mode == Live ? liveGain : applied); }
    bool isMeasuring() const noexcept { return mode == Live || (mode == Learn && state == LevelMatchStatus::Learning); }
    int samplesToHop() const noexcept { return hopSamples - hopCount; }

    // Feeds n <= samplesToHop() samples of reference and measured signal. Returns true when a
    // hop finished (gainDb() may have changed).
    bool process (const float* const* ref, const float* const* meas, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, 2);
        const float* const* sig[2] { ref, meas };
        for (int s = 0; s < 2; ++s)
            for (int c = 0; c < numCh; ++c)
            {
                auto& st = kw[s][c];
                const float* x = sig[s][c];
                double sum = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double y = st[1].process (st[0].process ((double) x[i]));
                    sum += y * y;
                }
                acc[s][c] += sum;
            }
        hopCount += n;
        if (hopCount < hopSamples) return false;
        finishHop();
        return true;
    }

    static double lufs (double meanSquare) noexcept { return meanSquare > 1.0e-30 ? -0.691 + 10.0 * std::log10 (meanSquare) : -300.0; }

private:
    void designKWeighting() noexcept
    {
        // BS.1770 stage 1 (head-related high shelf) and stage 2 (RLB high-pass), generalised to any rate.
        BiquadD s1, s2;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPiD * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1.b0 = (Vh + Vb * K / Q + K * K) / a0;
            s1.b1 = 2.0 * (K * K - Vh) / a0;
            s1.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s1.a1 = 2.0 * (K * K - 1.0) / a0;
            s1.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPiD * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2.b0 = 1.0; s2.b1 = -2.0; s2.b2 = 1.0;
            s2.a1 = 2.0 * (K * K - 1.0) / a0;
            s2.a2 = (1.0 - K / Q + K * K) / a0;
        }
        for (auto& s : kw)
            for (auto& c : s)
            {
                c[0] = s1;
                c[1] = s2;
            }
    }

    void enter (Mode m, float trimParamDb) noexcept
    {
        const float previous = gainDb();
        mode = m;
        runValid = false;
        liveValid = false;
        switch (m)
        {
            case Off:
                state = LevelMatchStatus::Idle;
                progress = 0.0f;
                break;
            case Learn:
                applied = trimParamDb;
                lastParam = trimParamDb;
                startLearning();
                break;
            case Live:
                liveGain = std::clamp (previous, -kMaxGainDb, kMaxGainDb);
                state = LevelMatchStatus::Live;
                progress = 1.0f;
                break;
        }
    }

    void clearLearn() noexcept
    {
        sumRef = sumMeas = 0.0;
        activeBlocks = 0;
        histCount = histPos = 0;
        progress = 0.0f;
    }

    void startLearning() noexcept
    {
        clearLearn();
        state = LevelMatchStatus::Learning;
        runValid = false;
        rebaseHops = 0;
    }

    void finishHop() noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            hopE[s][(size_t) hopFill] = (acc[s][0] + acc[s][1]) / (double) hopSamples;
            acc[s][0] = acc[s][1] = 0.0;
        }
        hopFill = (hopFill + 1) & 3;
        hopCount = 0;
        if (hopsSeen < 4) ++hopsSeen;
        if (hopsSeen < 4) return;

        const double refE = 0.25 * (hopE[0][0] + hopE[0][1] + hopE[0][2] + hopE[0][3]);
        const double measE = 0.25 * (hopE[1][0] + hopE[1][1] + hopE[1][2] + hopE[1][3]);
        const double refL = lufs (refE), measL = lufs (measE);

        const bool aboveAbsolute = refL > kAbsoluteGateLufs;
        const bool aboveRelative = ! runValid || refL > lufs (runE) - kRelativeGateLu;
        const bool chainAudible = measL > -100.0 && std::abs (refL - measL) < 30.0;

        // A level that stays below the relative gate (but above the absolute one) for long enough
        // is a new level, not a pause: re-base the running reference.
        if (aboveAbsolute && ! aboveRelative)
        {
            if (++rebaseHops >= (int) std::lround (kRebaseSeconds / hopSeconds)) { runValid = false; rebaseHops = 0; }
        }
        else
        {
            rebaseHops = 0;
        }

        if (! (aboveAbsolute && aboveRelative && chainAudible)) return;

        if (! runValid) { runE = refE; runValid = true; }
        else runE += (refE - runE) * (hopSeconds / kRunningAverageSeconds);

        if (mode == Learn && state == LevelMatchStatus::Learning)
        {
            sumRef += refE;
            sumMeas += measE;
            ++activeBlocks;
            const float estimate = (float) (10.0 * std::log10 (sumRef / sumMeas));
            hist[(size_t) histPos] = estimate;
            histPos = (histPos + 1) % kStableBlocks;
            if (histCount < kStableBlocks) ++histCount;
            progress = std::min (1.0f, (float) activeBlocks / (float) kLearnMinBlocks);

            if (activeBlocks >= kLearnMinBlocks)
            {
                bool stable = false;
                if (histCount >= kStableBlocks)
                {
                    float lo = hist[0], hi = hist[0];
                    for (float h : hist) { lo = std::min (lo, h); hi = std::max (hi, h); }
                    stable = hi - lo <= kStableRangeLu;
                }
                if (stable || activeBlocks >= kLearnMaxBlocks)
                {
                    applied = std::clamp (estimate, -kMaxGainDb, kMaxGainDb);
                    state = LevelMatchStatus::Learned;
                    progress = 1.0f;
                    uiLearned.store (applied, std::memory_order_relaxed);
                    uiSerial.fetch_add (1, std::memory_order_release);
                }
            }
        }
        else if (mode == Live)
        {
            if (! liveValid) { liveRef = refE; liveMeas = measE; liveValid = true; }
            else
            {
                const double a = hopSeconds / kLiveAverageSeconds;
                liveRef += (refE - liveRef) * a;
                liveMeas += (measE - liveMeas) * a;
            }
            const float target = (float) std::clamp (10.0 * std::log10 (liveRef / liveMeas), (double) -kMaxGainDb, (double) kMaxGainDb);
            const float maxStep = kLiveSlewDbPerSecond * (float) hopSeconds;
            liveGain = std::clamp (liveGain + std::clamp (target - liveGain, -maxStep, maxStep), -kMaxGainDb, kMaxGainDb);
        }
        publish();
    }

    void publish() noexcept
    {
        uiState.store ((int) (mode == Off ? LevelMatchStatus::Idle : state), std::memory_order_relaxed);
        uiProgress.store (progress, std::memory_order_relaxed);
        uiGain.store (gainDb(), std::memory_order_relaxed);
    }

    double fs = 48000.0, hopSeconds = 0.1;
    int hopSamples = 4800, hopCount = 0, hopFill = 0, hopsSeen = 0;

    BiquadD kw[2][2][2]; // [reference / measured][channel][stage]
    double acc[2][2] {};
    std::array<double, 4> hopE[2] {};

    Mode mode = Off;
    LevelMatchStatus::State state = LevelMatchStatus::Idle;
    float progress = 0.0f;
    float applied = 0.0f, lastParam = std::numeric_limits<float>::quiet_NaN(), liveGain = 0.0f;

    double runE = 0.0;
    bool runValid = false;
    int rebaseHops = 0;

    double sumRef = 0.0, sumMeas = 0.0;
    int activeBlocks = 0, histCount = 0, histPos = 0;
    std::array<float, kStableBlocks> hist {};

    double liveRef = 0.0, liveMeas = 0.0;
    bool liveValid = false;

    std::atomic<bool> relearnRequested { false }, holdRequested { false };
    std::atomic<int> uiState { 0 };
    std::atomic<float> uiProgress { 0.0f }, uiGain { 0.0f }, uiLearned { 0.0f };
    std::atomic<uint32_t> uiSerial { 0 };
};
} // namespace nl

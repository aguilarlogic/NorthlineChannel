#pragma once

#include "Common.h"
#include "LoudnessFilters.h"
#include "LoudnessTruePeak.h"
#include <array>
#include <atomic>
#include <cstring>
#include <type_traits>
#include <vector>

namespace nl
{
// Values the UI reads from MeterHub. Loudness in LUFS / LU, levels in dB.
// Fields marked "new" were added for the processor-owned meter; the earlier ones keep their meaning.
struct MeterSnapshot
{
    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0;
    double maxMomentary = -200.0, maxShortTerm = -200.0, lra = 0.0;
    float truePeakDb = -200.0f, samplePeakDb = -200.0f, correlation = 0.0f, vuDb = -200.0f;
    uint32_t sequence = 0;
    uint32_t overs = 0;               // new: output samples above the limiter ceiling (see setOverThreshold)
    double plr = 0.0;                 // new: true peak max minus integrated loudness, LU (0 until both are valid)
    double lraLow = -200.0, lraHigh = -200.0; // new: 10th and 95th percentile of short-term loudness, LUFS
    float samplePeakLDb = -200.0f, samplePeakRDb = -200.0f; // new: per channel
    uint32_t integrating = 1;         // new: 0 while integration is paused (transport stopped)
    uint32_t reserved = 0;
};
static_assert (std::is_trivially_copyable_v<MeterSnapshot> && sizeof (MeterSnapshot) % 8 == 0, "MeterSnapshot must be copyable as 64-bit words");

// Processor-owned loudness and peak metering (BS.1770-4 / EBU R128 / Tech 3341 / Tech 3342),
// fed on the audio thread after the limiter, so it measures exactly what leaves the plugin,
// also while the editor is closed and during offline renders.
//
// Threads: process()/accumulate()/publish()/reset()/prepare() belong to the audio side.
// read(), readGrHistory(), resetIntegrated(), setTransportPlaying() and the option setters are
// safe from any thread (read() is a seqlock reader, the others are atomic requests).
// Nothing allocates after prepare().
class MeterHub
{
public:
    static constexpr int kGrPointsPerSecond = 100; // gain-reduction history resolution
    static constexpr int kGrHistoryPoints = 1000;  // 10 s

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        subLen = std::max (1, (int) std::lround (0.010 * fs));
        grBinLen = std::max (1, (int) std::lround (fs / kGrPointsPerSecond));
        tpKernel.design (fs);
        KWeighting::design (fs, kwProto[0], kwProto[1]);
        vu.prepare (fs / kControlBlock);
        corrDecay = std::exp (-(double) kControlBlock / (0.3 * fs));
        histCount.assign ((size_t) kBins, 0u);
        histEnergy.assign ((size_t) kBins, 0.0);
        lraCount.assign ((size_t) kBins, 0u);
        lraEnergy.assign ((size_t) kBins, 0.0);
        reset();
    }

    // Full reset including filter state. Not for use while process() runs on another thread.
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            kw[c][0] = kwProto[0];
            kw[c][1] = kwProto[1];
            kw[c][0].reset();
            kw[c][1].reset();
            tp[c].reset();
            acc[c] = 0.0;
        }
        subRing.fill (0.0);
        subHead = 0;
        subCount = 0;
        hopPhase = 0;
        ctrlCount = 0;
        momentary = shortTerm = -200.0;
        corrXY = corrXX = corrYY = 0.0;
        accXY = accXX = accYY = 0.0;
        vuAcc = 0.0;
        vu.reset();
        for (auto& g : grRing) g.store (0.0f, std::memory_order_relaxed);
        grHead.store (0, std::memory_order_relaxed);
        grBinCount = 0;
        grBinMin = 0.0f;
        wasPlaying = playing.load();
        resetPending.store (false);
        clearIntegratedData();
        publish();
    }

    // Resets integrated loudness, LRA, maxima, peaks and the overs counter (the meter's
    // "reset" button). Safe from the UI thread: the audio thread performs it at the next block.
    void resetIntegrated() noexcept { resetPending.store (true); }

    // Transport state, from the audio thread (getPlayHead() in processBlock) or any other.
    void setTransportPlaying (bool isPlaying) noexcept { playing.store (isPlaying); }
    // Options. With `pause`, integration (integrated loudness, LRA, maxima) stops while the
    // transport is stopped; with `reset`, it restarts from scratch when the transport starts.
    void setPauseIntegrationWhenStopped (bool pause) noexcept { pauseWhenStopped.store (pause); }
    void setResetIntegrationOnPlay (bool reset) noexcept { resetOnPlay.store (reset); }

    // Set by the limiter section before each segment: output samples (or inter-sample peaks when
    // `truePeak`) above `linear` count as overs. 0 disables counting.
    void setOverThreshold (float linear, bool truePeak) noexcept
    {
        overThr = linear;
        overTrue = truePeak;
    }

    // Meters a block and publishes the result.
    void process (const float* const* io, int numCh, int n) noexcept
    {
        accumulate (io, numCh, n);
        publish();
    }

    void accumulate (const float* const* io, int numCh, int n) noexcept
    {
        handleRequests();
        const int nc = numCh >= 2 ? 2 : 1;
        int i = 0;
        while (i < n)
        {
            const int len = std::min ({ n - i, kControlBlock - ctrlCount, subLen - subCount });
            float sx[2][kControlBlock];
            for (int c = 0; c < nc; ++c)
                for (int j = 0; j < len; ++j)
                {
                    const float s = io[c][i + j];
                    sx[c][j] = std::abs (s) <= 1.0e6f ? s : 0.0f; // NaN and Inf measure as silence
                }
            processChunk (sx, nc, len);
            i += len;
            ctrlCount += len;
            subCount += len;
            if (ctrlCount == kControlBlock) controlTick (nc);
            if (subCount == subLen) endSubBlock();
        }
    }

    // Publishes the current values (seqlock write). Audio thread only.
    void publish() noexcept
    {
        MeterSnapshot s;
        s.momentary = momentary;
        s.shortTerm = shortTerm;
        s.integrated = integrated;
        s.maxMomentary = maxMomentary;
        s.maxShortTerm = maxShortTerm;
        s.lra = lra;
        s.lraLow = lraLow;
        s.lraHigh = lraHigh;
        s.truePeakDb = linToDb (tpMax);
        s.samplePeakDb = linToDb (std::max (spMax[0], spMax[1]));
        s.samplePeakLDb = linToDb (spMax[0]);
        s.samplePeakRDb = linToDb (spMax[1]);
        s.plr = (tpMax > 0.0f && integrated > -199.0) ? (double) s.truePeakDb - integrated : 0.0;
        s.correlation = correlationNow;
        s.vuDb = vuDbNow;
        s.overs = overs;
        s.integrating = gatingOn() ? 1u : 0u;
        s.sequence = ++sequenceCounter;

        std::uint64_t words[kWords];
        std::memcpy (words, &s, sizeof (s));
        const uint32_t q = seq.load (std::memory_order_relaxed);
        seq.store (q + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (size_t w = 0; w < kWords; ++w) data[w].store (words[w], std::memory_order_relaxed);
        seq.store (q + 2, std::memory_order_release);
    }

    // Consistent snapshot; any thread.
    MeterSnapshot read() const noexcept
    {
        std::uint64_t words[kWords];
        for (int attempt = 0; attempt < 1000; ++attempt)
        {
            const uint32_t s1 = seq.load (std::memory_order_acquire);
            if (s1 & 1u) continue;
            for (size_t w = 0; w < kWords; ++w) words[w] = data[w].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s1) break;
        }
        MeterSnapshot s;
        std::memcpy (&s, words, sizeof (s));
        if (resetPending.load (std::memory_order_relaxed)) // not yet applied by the audio thread
        {
            s.integrated = s.maxMomentary = s.maxShortTerm = -200.0;
            s.lra = s.plr = 0.0;
            s.lraLow = s.lraHigh = -200.0;
            s.truePeakDb = s.samplePeakDb = s.samplePeakLDb = s.samplePeakRDb = -200.0f;
            s.overs = 0;
        }
        return s;
    }

    // Gain-reduction history for the UI: the newest `maxPoints` (at most kGrHistoryPoints) values,
    // oldest first, in dB (<= 0), kGrPointsPerSecond per second. Returns the number written.
    int readGrHistory (float* dst, int maxPoints) const noexcept
    {
        const uint32_t head = grHead.load (std::memory_order_acquire);
        const int n = (int) std::min<uint32_t> ((uint32_t) std::min (maxPoints, kGrHistoryPoints), head);
        for (int k = 0; k < n; ++k)
            dst[k] = grRing[(size_t) ((head - (uint32_t) n + (uint32_t) k) % (uint32_t) kGrHistoryPoints)].load (std::memory_order_relaxed);
        return n;
    }

    // Audio thread: the limiter's gain reduction (dB, <= 0) over the last `len` samples.
    void pushGainReduction (float grDb, int len) noexcept
    {
        grBinMin = std::min (grBinMin, grDb);
        grBinCount += len;
        while (grBinCount >= grBinLen)
        {
            const uint32_t h = grHead.load (std::memory_order_relaxed);
            grRing[(size_t) (h % (uint32_t) kGrHistoryPoints)].store (grBinMin, std::memory_order_relaxed);
            grHead.store (h + 1, std::memory_order_release);
            grBinCount -= grBinLen;
            grBinMin = 0.0f;
        }
    }

    // Exposed for tests.
    const TruePeakKernel& truePeakKernel() const noexcept { return tpKernel; }
    const VuBallistics& vuModel() const noexcept { return vu; }
    const BiquadD& kStage (int i) const noexcept { return kwProto[i]; }

private:
    static constexpr int kSubPerHop = 10;      // 10 ms sub-blocks, 100 ms gating hop
    static constexpr int kMomentarySubs = 40;  // 400 ms
    static constexpr int kShortSubs = 300;     // 3 s
    static constexpr double kHistMin = -70.0;  // absolute gate, LUFS
    static constexpr double kBinsPerLu = 100.0; // 0.01 LU resolution
    static constexpr int kBins = 9000;         // -70 .. +20 LUFS
    static constexpr size_t kWords = sizeof (MeterSnapshot) / 8;

    static float linToDb (float lin) noexcept { return lin > 1.0e-10f ? 20.0f * std::log10 (lin) : -200.0f; }

    bool gatingOn() const noexcept { return ! (pauseWhenStopped.load (std::memory_order_relaxed) && ! playing.load (std::memory_order_relaxed)); }

    void handleRequests() noexcept
    {
        const bool pl = playing.load (std::memory_order_relaxed);
        if (pl && ! wasPlaying && resetOnPlay.load (std::memory_order_relaxed)) clearIntegratedData();
        wasPlaying = pl;
        if (resetPending.exchange (false)) clearIntegratedData();
    }

    void clearIntegratedData() noexcept
    {
        std::fill (histCount.begin(), histCount.end(), 0u);
        std::fill (histEnergy.begin(), histEnergy.end(), 0.0);
        std::fill (lraCount.begin(), lraCount.end(), 0u);
        std::fill (lraEnergy.begin(), lraEnergy.end(), 0.0);
        integrated = -200.0;
        lra = 0.0;
        lraLow = lraHigh = -200.0;
        maxMomentary = maxShortTerm = -200.0;
        tpMax = 0.0f;
        spMax[0] = spMax[1] = 0.0f;
        overs = 0;
        sinceReset = 0; // gating blocks and LRA windows must be made of audio after the reset
        hopPhase = 0;
    }

    void processChunk (const float (&sx)[2][kControlBlock], int nc, int len) noexcept
    {
        // Sample peak, K-weighted energy.
        for (int c = 0; c < nc; ++c)
        {
            float pk = spMax[c];
            double e = acc[c]; // sequential accumulation: the sum does not depend on how the stream was cut into blocks
            auto& s1 = kw[c][0];
            auto& s2 = kw[c][1];
            for (int j = 0; j < len; ++j)
            {
                const float x = sx[c][j];
                pk = std::max (pk, std::abs (x));
                const double y = s2.process (s1.process ((double) x));
                e += y * y;
            }
            spMax[c] = pk;
            acc[c] = e;
        }

        // Correlation and VU inputs.
        if (nc == 2)
        {
            double xy = accXY, xx = accXX, yy = accYY, rect = vuAcc;
            for (int j = 0; j < len; ++j)
            {
                const double l = sx[0][j], r = sx[1][j];
                xy += l * r;
                xx += l * l;
                yy += r * r;
                rect += 0.5 * (std::abs (l) + std::abs (r));
            }
            accXY = xy;
            accXX = xx;
            accYY = yy;
            vuAcc = rect;
        }
        else
        {
            double rect = vuAcc, xx = accXX;
            for (int j = 0; j < len; ++j)
            {
                rect += std::abs (sx[0][j]);
                xx += (double) sx[0][j] * sx[0][j];
            }
            vuAcc = rect;
            accXX = xx;
        }

        // True peak (4x polyphase), with a cheap bound that skips the interpolation when it cannot
        // change the maximum or the over count.
        const float thr = overThr;
        for (int j = 0; j < len; ++j)
        {
            float bound = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                tp[c].push (sx[c][j]);
                bound = std::max (bound, tp[c].windowMax());
            }
            float skipLevel = tpMax;
            if (thr > 0.0f && overTrue) skipLevel = std::min (skipLevel, thr);
            const bool skip = bound * tpKernel.l1 * 1.1f <= skipLevel;
            float peak = 0.0f;
            for (int c = 0; c < nc; ++c) peak = std::max (peak, tp[c].slotPeak (tpKernel, false, skip));
            tpMax = std::max (tpMax, peak);
            if (thr > 0.0f)
            {
                float v = peak;
                if (! overTrue)
                {
                    v = std::abs (sx[0][j]);
                    if (nc > 1) v = std::max (v, std::abs (sx[1][j]));
                }
                if (v > thr) ++overs;
            }
        }
    }

    void controlTick (int nc) noexcept
    {
        ctrlCount = 0;
        corrXY = corrXY * corrDecay + accXY;
        corrXX = corrXX * corrDecay + accXX;
        corrYY = corrYY * corrDecay + accYY;
        accXY = accXX = accYY = 0.0;
        if (nc == 2)
        {
            const double d = std::sqrt (corrXX * corrYY);
            correlationNow = d > 1.0e-12 ? (float) std::clamp (corrXY / d, -1.0, 1.0) : 0.0f;
        }
        else
        {
            correlationNow = corrXX > 1.0e-12 ? 1.0f : 0.0f; // mono is fully correlated with itself
        }

        vu.tick ((double) VuBallistics::kSineRmsCal * vuAcc / kControlBlock);
        vuAcc = 0.0;
        const double v = vu.value();
        vuDbNow = v > 1.0e-10 ? (float) (20.0 * std::log10 (v)) : -200.0f;
    }

    void endSubBlock() noexcept
    {
        subCount = 0;
        const double e = (acc[0] + acc[1]) / subLen;
        acc[0] = acc[1] = 0.0;
        subRing[(size_t) subHead] = e;
        subHead = (subHead + 1) % kShortSubs;
        if (sinceReset < 0x7fffffffu) ++sinceReset;

        double sumM = 0.0, sumS = 0.0;
        for (int k = 1; k <= kShortSubs; ++k)
        {
            const double v = subRing[(size_t) ((subHead - k + kShortSubs) % kShortSubs)];
            sumS += v;
            if (k <= kMomentarySubs) sumM += v;
        }
        const double eM = sumM / kMomentarySubs, eS = sumS / kShortSubs;
        momentary = energyToLufs (eM);
        shortTerm = energyToLufs (eS);

        if (gatingOn())
        {
            maxMomentary = std::max (maxMomentary, momentary);
            maxShortTerm = std::max (maxShortTerm, shortTerm);
        }
        if (++hopPhase == kSubPerHop)
        {
            hopPhase = 0;
            endHop (eM, eS);
        }
    }

    static int binOf (double lufs) noexcept { return std::clamp ((int) std::floor ((lufs - kHistMin) * kBinsPerLu), 0, kBins - 1); }
    static double binCentre (int b) noexcept { return kHistMin + ((double) b + 0.5) / kBinsPerLu; }

    void endHop (double blockEnergy, double shortEnergy) noexcept
    {
        if (! gatingOn()) return;
        if (sinceReset >= (uint32_t) kMomentarySubs)
        {
            const double l = energyToLufs (blockEnergy);
            if (l > kHistMin)
            {
                const int b = binOf (l);
                ++histCount[(size_t) b];
                histEnergy[(size_t) b] += blockEnergy;
            }
        }
        if (sinceReset >= (uint32_t) kShortSubs)
        {
            const double l = energyToLufs (shortEnergy);
            if (l > kHistMin)
            {
                const int b = binOf (l);
                ++lraCount[(size_t) b];
                lraEnergy[(size_t) b] += shortEnergy;
            }
        }
        computeIntegrated();
        computeLra();
    }

    // Absolute gate is the histogram floor; the relative gate is 10 LU below the absolute-gated mean.
    void computeIntegrated() noexcept
    {
        double sum = 0.0;
        uint64_t count = 0;
        for (int b = 0; b < kBins; ++b)
        {
            sum += histEnergy[(size_t) b];
            count += histCount[(size_t) b];
        }
        if (count == 0) { integrated = -200.0; return; }
        const double relGate = energyToLufs (sum / (double) count) - 10.0;
        const int start = std::clamp ((int) std::ceil ((relGate - kHistMin) * kBinsPerLu - 0.5), 0, kBins);
        sum = 0.0;
        count = 0;
        for (int b = start; b < kBins; ++b)
        {
            sum += histEnergy[(size_t) b];
            count += histCount[(size_t) b];
        }
        integrated = count > 0 ? energyToLufs (sum / (double) count) : -200.0;
    }

    // EBU Tech 3342: short-term values above -70 LUFS, relative gate 20 LU below their power mean,
    // then the 10th and 95th percentiles of what remains.
    void computeLra() noexcept
    {
        double sum = 0.0;
        uint64_t count = 0;
        for (int b = 0; b < kBins; ++b)
        {
            sum += lraEnergy[(size_t) b];
            count += lraCount[(size_t) b];
        }
        if (count == 0) { lra = 0.0; lraLow = lraHigh = -200.0; return; }
        const double relGate = energyToLufs (sum / (double) count) - 20.0;
        const int start = std::clamp ((int) std::ceil ((relGate - kHistMin) * kBinsPerLu - 0.5), 0, kBins);
        uint64_t n = 0;
        for (int b = start; b < kBins; ++b) n += lraCount[(size_t) b];
        if (n == 0) { lra = 0.0; lraLow = lraHigh = -200.0; return; }
        const uint64_t idxLow = (uint64_t) std::llround ((double) (n - 1) * 0.10);
        const uint64_t idxHigh = (uint64_t) std::llround ((double) (n - 1) * 0.95);
        uint64_t cum = 0;
        int bLow = -1, bHigh = -1;
        for (int b = start; b < kBins; ++b)
        {
            cum += lraCount[(size_t) b];
            if (bLow < 0 && cum > idxLow) bLow = b;
            if (bHigh < 0 && cum > idxHigh) { bHigh = b; break; }
        }
        if (bLow < 0 || bHigh < 0) { lra = 0.0; return; }
        lraLow = binCentre (bLow);
        lraHigh = binCentre (bHigh);
        lra = lraHigh - lraLow;
    }

    double fs = 48000.0;
    int subLen = 480, grBinLen = 480;
    TruePeakKernel tpKernel;
    BiquadD kwProto[2], kw[2][2];
    TruePeakChannel tp[2];
    VuBallistics vu;

    // Windows.
    std::array<double, kShortSubs> subRing {};
    int subHead = 0, subCount = 0, hopPhase = 0, ctrlCount = 0;
    uint32_t sinceReset = 0; // sub-blocks since the integration was (re)started
    double acc[2] {};

    // Histograms (0.01 LU bins, -70 to +20 LUFS).
    std::vector<uint32_t> histCount, lraCount;
    std::vector<double> histEnergy, lraEnergy;

    // Current values.
    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0;
    double maxMomentary = -200.0, maxShortTerm = -200.0, lra = 0.0, lraLow = -200.0, lraHigh = -200.0;
    float tpMax = 0.0f, spMax[2] {};
    float correlationNow = 0.0f, vuDbNow = -200.0f;
    uint32_t overs = 0;
    float overThr = 0.0f;
    bool overTrue = false;
    double corrDecay = 0.99, corrXY = 0, corrXX = 0, corrYY = 0, accXY = 0, accXX = 0, accYY = 0, vuAcc = 0;

    // Requests from other threads.
    std::atomic<bool> resetPending { false }, playing { true }, pauseWhenStopped { false }, resetOnPlay { false };
    bool wasPlaying = true;

    // Gain-reduction history.
    std::array<std::atomic<float>, kGrHistoryPoints> grRing {};
    std::atomic<uint32_t> grHead { 0 };
    int grBinCount = 0;
    float grBinMin = 0.0f;

    // Seqlock.
    std::atomic<uint32_t> seq { 0 };
    std::array<std::atomic<std::uint64_t>, kWords> data {};
    uint32_t sequenceCounter = 0;
};
} // namespace nl

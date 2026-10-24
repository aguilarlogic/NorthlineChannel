#pragma once

#include "Common.h"
#include "LoudnessFilters.h"
#include "LoudnessTruePeak.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
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

// One vectorscope sample: a decimated output pair.
struct ScopePair
{
    float l = 0.0f, r = 0.0f;
};

// Single-producer, single-consumer ring of stereo pairs (vectorscope feed). The producer (audio
// thread) never waits or allocates. If the consumer falls more than Capacity pairs behind, the
// oldest pairs are dropped and counted. Each pair is one 64-bit atomic, so a slot is never torn.
// Capacity must be a power of two.
template <size_t Capacity>
class PairRing
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "PairRing capacity must be a power of two");

public:
    // Producer.
    void push (float l, float r) noexcept
    {
        const uint64_t w = written.load (std::memory_order_relaxed);
        slots[(size_t) (w & (Capacity - 1))].store (pack (l, r), std::memory_order_relaxed);
        written.store (w + 1, std::memory_order_release);
    }

    // Consumer: copies up to maxCount unread pairs into out, oldest first. Returns the number copied.
    // Pairs the producer overwrites while the copy runs are discarded as well (best effort under
    // concurrency; exact when producer and consumer do not overlap).
    size_t pop (ScopePair* out, size_t maxCount) noexcept
    {
        const uint64_t w = written.load (std::memory_order_acquire);
        uint64_t first = consumed;
        if (w - first > Capacity)
        {
            dropped += w - Capacity - first;
            first = w - Capacity;
        }
        size_t n = (size_t) std::min<uint64_t> (w - first, (uint64_t) maxCount);
        for (size_t i = 0; i < n; ++i)
            out[i] = unpack (slots[(size_t) ((first + i) & (Capacity - 1))].load (std::memory_order_relaxed));

        // Entries with index + Capacity <= the newest write may have been overwritten during the copy.
        const uint64_t w2 = written.load (std::memory_order_acquire);
        const uint64_t unsafeBelow = w2 >= Capacity ? w2 - Capacity + 1 : 0;
        const size_t lost = unsafeBelow > first ? (size_t) std::min<uint64_t> (unsafeBelow - first, n) : 0;
        if (lost > 0)
        {
            std::copy (out + lost, out + n, out);
            n -= lost;
            dropped += lost;
        }
        consumed = first + n + lost;
        return n;
    }

    // Consumer side: pairs dropped because the consumer was too slow.
    uint64_t droppedCount() const noexcept { return dropped; }

private:
    static uint64_t pack (float l, float r) noexcept
    {
        uint32_t a, b;
        std::memcpy (&a, &l, 4);
        std::memcpy (&b, &r, 4);
        return ((uint64_t) b << 32) | (uint64_t) a;
    }

    static ScopePair unpack (uint64_t v) noexcept
    {
        const uint32_t a = (uint32_t) v, b = (uint32_t) (v >> 32);
        ScopePair p;
        std::memcpy (&p.l, &a, 4);
        std::memcpy (&p.r, &b, 4);
        return p;
    }

    std::atomic<uint64_t> written { 0 };
    std::array<std::atomic<uint64_t>, Capacity> slots {};
    uint64_t consumed = 0, dropped = 0; // consumer side
};

// Fenwick (binary indexed) tree: point update and prefix sum in O(log n), and a descent that finds
// the bin where a running sum passes a value. Used for the gated loudness statistics.
template <typename T>
class FenwickTree
{
public:
    void assign (int n)
    {
        size = n;
        top = 1;
        while (top * 2 <= n) top *= 2;
        t.assign ((size_t) n + 1, T {});
    }

    void clear() noexcept { std::fill (t.begin(), t.end(), T {}); }

    void add (int index, T v) noexcept
    {
        for (int i = index + 1; i <= size; i += i & -i) t[(size_t) i] += v;
    }

    // Sum of bins [0, n).
    T prefix (int n) const noexcept
    {
        T s {};
        for (int i = n; i > 0; i -= i & -i) s += t[(size_t) i];
        return s;
    }

    // Largest n in [0, size] with prefix(n) <= limit. Bin n then holds the item number `limit`
    // (0-based), and equals size when there is no such item.
    int lastWithin (T limit) const noexcept
    {
        int pos = 0;
        for (int step = top; step > 0; step >>= 1)
            if (pos + step <= size && t[(size_t) (pos + step)] <= limit)
            {
                pos += step;
                limit -= t[(size_t) pos];
            }
        return pos;
    }

private:
    int size = 0, top = 1;
    std::vector<T> t;
};

// Blocks of loudness, binned at 0.01 LU from -70 LUFS: the count and the summed energy per bin.
struct GatedHistogram
{
    FenwickTree<uint64_t> count;
    FenwickTree<double> energy;
    uint64_t total = 0;
    double totalEnergy = 0.0;

    void assign (int bins)
    {
        count.assign (bins);
        energy.assign (bins);
        clear();
    }

    void clear() noexcept
    {
        count.clear();
        energy.clear();
        total = 0;
        totalEnergy = 0.0;
    }

    void add (int bin, double blockEnergy) noexcept
    {
        count.add (bin, 1);
        energy.add (bin, blockEnergy);
        ++total;
        totalEnergy += blockEnergy;
    }
};

// Processor-owned loudness and peak metering (BS.1770-4 / EBU R128 / Tech 3341 / Tech 3342),
// fed on the audio thread after the limiter, so it measures exactly what leaves the plugin,
// also while the editor is closed and during offline renders.
//
// Threads: process()/accumulate()/publish()/reset()/prepare() belong to the audio side.
// read(), readGrHistory(), resetIntegrated(), setTransportPlaying() and the option setters are
// safe from any thread (read() is a seqlock reader, the others are atomic requests). scope() is
// the consumer end of the vectorscope ring: one consumer thread (the UI) owns it.
// Nothing allocates after prepare().
class MeterHub
{
public:
    static constexpr int kGrPointsPerSecond = 100; // gain-reduction history resolution
    static constexpr int kGrHistoryPoints = 1000;  // 10 s
    static constexpr int kScopeDecimation = 4;     // vectorscope: one pair per 4 samples
    static constexpr size_t kScopeCapacity = 2048; // pairs (about 170 ms at 48 kHz)
    using ScopeRing = PairRing<kScopeCapacity>;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        subLen = std::max (1, (int) std::lround (0.010 * fs));
        grBinLen = std::max (1, (int) std::lround (fs / kGrPointsPerSecond));
        tpKernel.design (fs);
        KWeighting::design (fs, kwProto[0], kwProto[1]);
        vu.prepare (fs / kControlBlock);
        corrDecay = std::exp (-(double) kControlBlock / (0.3 * fs));
        histogram.assign (kBins);
        lraHistogram.assign (kBins);
        reset();
    }

    // Full reset including filter state. Not for use while process() runs on another thread.
    void reset() noexcept
    {
        kw[0].setup (kwProto[0]);
        kw[1].setup (kwProto[1]);
        acc[0] = acc[1] = 0.0;
        tps.reset();
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
        scopePhase = 0;
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
    bool pauseIntegrationWhenStopped() const noexcept { return pauseWhenStopped.load(); }
    bool resetIntegrationOnPlay() const noexcept { return resetOnPlay.load(); }

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
            if (nc == 2) processChunk<true> (io[0] + i, io[1] + i, len);
            else processChunk<false> (io[0] + i, nullptr, len);
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

    // Vectorscope feed: every kScopeDecimation-th output pair. The UI pops from it.
    ScopeRing& scope() noexcept { return scopeRing; }

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

    static float linToDb (float lin) noexcept { return lin > 1.0e-10f ? fastGainToDb (lin) : -200.0f; }
    // Samples above 1e6 (and NaN and Inf) measure as silence.
    static float sanitize (float s) noexcept { return std::abs (s) <= 1.0e6f ? s : 0.0f; }

    bool gatingOn() const noexcept { return ! (pauseWhenStopped.load (std::memory_order_relaxed) && ! playing.load (std::memory_order_relaxed)); }

    void handleRequests() noexcept
    {
        const bool pl = playing.load (std::memory_order_relaxed);
        if (pl && ! wasPlaying && resetOnPlay.load (std::memory_order_relaxed)) clearIntegratedData();
        wasPlaying = pl;
        if (resetPending.load (std::memory_order_relaxed) && resetPending.exchange (false)) clearIntegratedData();
    }

    void clearIntegratedData() noexcept
    {
        histogram.clear();
        lraHistogram.clear();
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

    // One chunk of at most kControlBlock samples (both channels when Stereo). All per-sample meter
    // work happens in this single pass: sample peaks, K-weighted energy, correlation and VU sums,
    // the true-peak interpolation, the overs count and the vectorscope decimation.
    template <bool Stereo>
    void processChunk (const float* a, const float* b, int len) noexcept
    {
        float pk0 = spMax[0], pk1 = spMax[1];
        Pair2 ee = Pair2::make (acc[0], acc[1]);
        double xy = accXY, xx = accXX, yy = accYY, rect = vuAcc;
        const float thr = overThr;
        const bool trueOvers = thr > 0.0f && overTrue;
        auto ts = tps.getState();

        for (int j = 0; j < len; ++j)
        {
            const float x0 = sanitize (a[j]);
            const float x1 = Stereo ? sanitize (b[j]) : x0;

            pk0 = std::max (pk0, std::abs (x0));
            const Pair2 y = kw[1].process (kw[0].process (Pair2::make ((double) x0, (double) x1)));
            ee = ee + y * y;
            if constexpr (Stereo)
            {
                pk1 = std::max (pk1, std::abs (x1));
                xy += (double) x0 * x1;
                xx += (double) x0 * x0;
                yy += (double) x1 * x1;
                rect += 0.5 * (std::abs ((double) x0) + std::abs ((double) x1));
            }
            else
            {
                xx += (double) x0 * x0;
                rect += std::abs ((double) x0);
            }

            // True peak: the window ends with this pair. The bound is exact, so the interpolation
            // is skipped only when it cannot change the maximum or the over count.
            tps.push (ts, x0, x1);
            const float bound = tps.windowMax (ts);
            float skipLevel = tpMax;
            if (trueOvers) skipLevel = std::min (skipLevel, thr);
            const bool skip = bound * tpKernel.l1 * 1.1f <= skipLevel;
            float peak = 0.0f;
            if (! skip)
            {
                peak = tps.slotPeak (ts, tpKernel, 0);
                if constexpr (Stereo) peak = std::max (peak, tps.slotPeak (ts, tpKernel, 1));
            }
            tpMax = std::max (tpMax, peak);
            if (thr > 0.0f)
            {
                float v = peak;
                if (! overTrue)
                {
                    v = std::abs (x0);
                    if constexpr (Stereo) v = std::max (v, std::abs (x1));
                }
                if (v > thr) ++overs;
            }

            // Vectorscope: every kScopeDecimation-th output pair, counted across blocks.
            if (scopePhase == 0) scopeRing.push (x0, x1);
            scopePhase = scopePhase + 1 == kScopeDecimation ? 0 : scopePhase + 1;
        }

        spMax[0] = pk0;
        spMax[1] = pk1;
        acc[0] = ee.lane (0);
        if constexpr (Stereo) acc[1] = ee.lane (1); // mono: the right lane only carries a copy of the left
        accXY = xy;
        accXX = xx;
        accYY = yy;
        vuAcc = rect;
        tps.setState (ts);
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
        vuDbNow = v > 1.0e-10 ? fastGainToDb ((float) v) : -200.0f;
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
    // First bin at or above a relative gate (LUFS).
    static int gateStart (double relGate) noexcept { return std::clamp ((int) std::ceil ((relGate - kHistMin) * kBinsPerLu - 0.5), 0, kBins); }

    void endHop (double blockEnergy, double shortEnergy) noexcept
    {
        if (! gatingOn()) return;
        if (sinceReset >= (uint32_t) kMomentarySubs)
        {
            const double l = energyToLufs (blockEnergy);
            if (l > kHistMin) histogram.add (binOf (l), blockEnergy);
        }
        if (sinceReset >= (uint32_t) kShortSubs)
        {
            const double l = energyToLufs (shortEnergy);
            if (l > kHistMin) lraHistogram.add (binOf (l), shortEnergy);
        }
        computeIntegrated();
        computeLra();
    }

    // Absolute gate is the histogram floor; the relative gate is 10 LU below the absolute-gated mean.
    void computeIntegrated() noexcept
    {
        if (histogram.total == 0) { integrated = -200.0; return; }
        const double relGate = energyToLufs (histogram.totalEnergy / (double) histogram.total) - 10.0;
        const int start = gateStart (relGate);
        const uint64_t n = histogram.total - histogram.count.prefix (start);
        const double e = histogram.totalEnergy - histogram.energy.prefix (start);
        integrated = n > 0 ? energyToLufs (e / (double) n) : -200.0;
    }

    // EBU Tech 3342: short-term values above -70 LUFS, relative gate 20 LU below their power mean,
    // then the 10th and 95th percentiles of what remains.
    void computeLra() noexcept
    {
        if (lraHistogram.total == 0) { lra = 0.0; lraLow = lraHigh = -200.0; return; }
        const double relGate = energyToLufs (lraHistogram.totalEnergy / (double) lraHistogram.total) - 20.0;
        const int start = gateStart (relGate);
        const uint64_t before = lraHistogram.count.prefix (start);
        const uint64_t n = lraHistogram.total - before;
        if (n == 0) { lra = 0.0; lraLow = lraHigh = -200.0; return; }
        const uint64_t idxLow = (uint64_t) std::llround ((double) (n - 1) * 0.10);
        const uint64_t idxHigh = (uint64_t) std::llround ((double) (n - 1) * 0.95);
        const int bLow = lraHistogram.count.lastWithin (before + idxLow);
        const int bHigh = lraHistogram.count.lastWithin (before + idxHigh);
        if (bLow >= kBins || bHigh >= kBins) { lra = 0.0; return; }
        lraLow = binCentre (bLow);
        lraHigh = binCentre (bHigh);
        lra = lraHigh - lraLow;
    }

    double fs = 48000.0;
    int subLen = 480, grBinLen = 480;
    TruePeakKernel tpKernel;
    BiquadD kwProto[2];
    BiquadPair kw[2]; // K-weighting stages 1 and 2, both channels
    TruePeakStereo tps;
    VuBallistics vu;

    // Windows.
    std::array<double, kShortSubs> subRing {};
    int subHead = 0, subCount = 0, hopPhase = 0, ctrlCount = 0;
    uint32_t sinceReset = 0; // sub-blocks since the integration was (re)started
    double acc[2] {};

    // Gated histograms (0.01 LU bins, -70 to +20 LUFS): integrated loudness and LRA.
    GatedHistogram histogram, lraHistogram;

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

    // Vectorscope feed.
    ScopeRing scopeRing;
    int scopePhase = 0;

    // Seqlock.
    std::atomic<uint32_t> seq { 0 };
    std::array<std::atomic<std::uint64_t>, kWords> data {};
    uint32_t sequenceCounter = 0;
};
} // namespace nl

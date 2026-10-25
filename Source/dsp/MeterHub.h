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

        // Entries older than the newest write minus Capacity were overwritten during the copy.
        const uint64_t w2 = written.load (std::memory_order_acquire);
        const uint64_t unsafeBelow = w2 >= Capacity ? w2 - Capacity : 0;
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
//
// Per chunk (at most one control block, 32 samples, cut at the absolute grid so the result does not
// depend on the host block size):
//   - K-weighting: the two biquads as one recursion over both channels (SSE2 lanes), in a
//     rearranged form with a shorter dependency chain (same filter, rounding differs at 1e-16);
//   - energy, correlation, VU and peak sums (no feedback, so they overlap with the recursion);
//   - true peak: the 4x interpolation is evaluated only where a rigorous bound on the 16-sample window
//     can reach the running maximum or the over threshold. The bound never under-estimates, so the
//     maximum and the over count are the same as evaluating every sample.
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
        for (int p = 0; p < TruePeakKernel::kMaxPhases; ++p)
            for (int j = 0; j < TruePeakKernel::kTaps; ++j)
                for (int k = 0; k < 4; ++k) tpBroad[p][j][k] = tpKernel.h[p][j]; // the kernel, one tap per SIMD vector
        // Energy of each interpolating phase outside the core taps (Cauchy-Schwarz tail bound, see truePeakPass).
        for (int p = 1; p < tpKernel.phases; ++p)
        {
            tpTailE[p] = 0.0f;
            for (int j = 0; j < TruePeakKernel::kTaps; ++j)
                if (j < kCoreLo || j >= kCoreLo + 4) tpTailE[p] += tpKernel.h[p][j] * tpKernel.h[p][j];
        }
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
        for (int s = 0; s < 2; ++s) kw[s].setup (kwProto[s]);
        acc[0] = acc[1] = 0.0;
        std::memset (tpBuf, 0, sizeof (tpBuf));
        tpMax = 0.0f;
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
        const uint64_t head = grHead.load (std::memory_order_acquire); // 64 bits: the ring index must not wrap at 2^32
        const int n = (int) std::min<uint64_t> ((uint64_t) std::min (maxPoints, kGrHistoryPoints), head);
        for (int k = 0; k < n; ++k)
            dst[k] = grRing[(size_t) ((head - (uint64_t) n + (uint64_t) k) % (uint64_t) kGrHistoryPoints)].load (std::memory_order_relaxed);
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
            const uint64_t h = grHead.load (std::memory_order_relaxed);
            grRing[(size_t) (h % (uint64_t) kGrHistoryPoints)].store (grBinMin, std::memory_order_relaxed);
            grHead.store (h + 1, std::memory_order_release);
            grBinCount -= grBinLen;
            grBinMin = 0.0f;
        }
    }

#if NL_TOOL_BUILD
    // Test seam (tool build only, not while audio runs): places the counters near their wrap-around, so the
    // tests can show that the snapshots and the gain-reduction history stay correct across it.
    //   snapshotSequence: the next MeterSnapshot::sequence is snapshotSequence + 1 (wraps at 2^32)
    //   seqlock:          the seqlock word (even when at rest; wraps at 2^32)
    //   grHeadValue:      points in the gain-reduction history (its ring index is taken modulo 1000)
    //   subBlocks:        sub-blocks since integration started (saturates at 2^31 - 1)
    void seedCountersForTest (uint32_t snapshotSequence, uint32_t seqlock, uint64_t grHeadValue, uint32_t subBlocks) noexcept
    {
        sequenceCounter = snapshotSequence;
        seq.store (seqlock & ~1u);
        grHead.store (grHeadValue);
        sinceReset = subBlocks;
    }
#endif

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
    static constexpr int kTpHistory = TruePeakKernel::kTaps - 1; // samples kept for the 16-tap window
    static constexpr int kCoreLo = 6; // the true-peak core taps are window taps kCoreLo .. kCoreLo + 3 (see candidateMask)

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

    // K-weighting stage for both channels (two SSE2 lanes). The filter is BiquadD in direct form,
    // rearranged so that the state recursion has one multiply and one add on its critical path:
    //   y   = b0 x + z1
    //   z1' = c x + z2 - a1 z1        c = b1 - a1 b0
    //   z2' = d x - a2 z1            d = b2 - a2 b0
    struct KStage
    {
        Pair2 b0, a1, a2, c, d, z1, z2;

        void setup (const BiquadD& p) noexcept
        {
            const double cc = p.b1 - p.a1 * p.b0, dd = p.b2 - p.a2 * p.b0;
            b0 = Pair2::make (p.b0, p.b0);
            a1 = Pair2::make (p.a1, p.a1);
            a2 = Pair2::make (p.a2, p.a2);
            c = Pair2::make (cc, cc);
            d = Pair2::make (dd, dd);
            z1 = Pair2::make (0.0, 0.0);
            z2 = Pair2::make (0.0, 0.0);
        }
    };

    // One chunk of at most kControlBlock samples (both channels when Stereo).
    template <bool Stereo>
    void processChunk (const float* a, const float* b, int len) noexcept
    {
        constexpr int H = kTpHistory;
        // Sanitised input with the true-peak history in front: window of output t is tpBuf[c][t .. t+15].
        for (int j = 0; j < len; ++j)
        {
            tpBuf[0][H + j] = sanitize (a[j]);
            tpBuf[1][H + j] = Stereo ? sanitize (b[j]) : tpBuf[0][H + j];
        }
        const float* L = tpBuf[0] + H;
        const float* R = tpBuf[1] + H;

        // Pass 1: K-weighting recursion and energy. Only the recursion is on the critical path, so the
        // statistics run in a second loop that the out-of-order core can overlap with it.
        float pk0 = spMax[0], pk1 = spMax[1];
        double xy = accXY, xx = accXX, yy = accYY, rect = vuAcc;
#if JUCE_USE_SSE_INTRINSICS
        __m128d ee = _mm_set_pd (acc[1], acc[0]);
        KStage& K1 = kw[0];
        KStage& K2 = kw[1];
        const __m128d b01 = K1.b0.v, a01 = K1.a1.v, a02 = K1.a2.v, c01 = K1.c.v, d01 = K1.d.v;
        const __m128d b02 = K2.b0.v, a12 = K2.a1.v, a22 = K2.a2.v, c02 = K2.c.v, d02 = K2.d.v;
        __m128d s1z1 = K1.z1.v, s1z2 = K1.z2.v, s2z1 = K2.z1.v, s2z2 = K2.z2.v;
        for (int j = 0; j < len; ++j)
        {
            const __m128d x = _mm_set_pd ((double) R[j], (double) L[j]);
            const __m128d y1 = _mm_add_pd (_mm_mul_pd (b01, x), s1z1);
            const __m128d n1 = _mm_sub_pd (_mm_add_pd (_mm_mul_pd (c01, x), s1z2), _mm_mul_pd (a01, s1z1));
            s1z2 = _mm_sub_pd (_mm_mul_pd (d01, x), _mm_mul_pd (a02, s1z1));
            s1z1 = n1;
            const __m128d y2 = _mm_add_pd (_mm_mul_pd (b02, y1), s2z1);
            const __m128d n2 = _mm_sub_pd (_mm_add_pd (_mm_mul_pd (c02, y1), s2z2), _mm_mul_pd (a12, s2z1));
            s2z2 = _mm_sub_pd (_mm_mul_pd (d02, y1), _mm_mul_pd (a22, s2z1));
            s2z1 = n2;
            ee = _mm_add_pd (ee, _mm_mul_pd (y2, y2));
        }
        K1.z1.v = s1z1;
        K1.z2.v = s1z2;
        K2.z1.v = s2z1;
        K2.z2.v = s2z2;
        alignas (16) double lanes[2];
        _mm_store_pd (lanes, ee);
        acc[0] = lanes[0];
        if constexpr (Stereo) acc[1] = lanes[1];
#else
        Pair2 ee = Pair2::make (acc[0], acc[1]);
        KStage k1 = kw[0], k2 = kw[1];
        for (int j = 0; j < len; ++j)
        {
            const float f0 = L[j], f1 = R[j];
            const Pair2 x = Pair2::make ((double) f0, (double) f1);
            const Pair2 y1 = k1.b0 * x + k1.z1;
            const Pair2 n1 = k1.c * x + k1.z2 - k1.a1 * k1.z1;
            k1.z2 = k1.d * x - k1.a2 * k1.z1;
            k1.z1 = n1;
            const Pair2 y2 = k2.b0 * y1 + k2.z1;
            const Pair2 n2 = k2.c * y1 + k2.z2 - k2.a1 * k2.z1;
            k2.z2 = k2.d * y1 - k2.a2 * k2.z1;
            k2.z1 = n2;
            ee = ee + y2 * y2;
        }
        kw[0] = k1;
        kw[1] = k2;
        acc[0] = ee.lane (0);
        if constexpr (Stereo) acc[1] = ee.lane (1);
#endif
        // Pass 1b: sample peaks, correlation and VU sums (no feedback).
        for (int j = 0; j < len; ++j)
        {
            const float f0 = L[j], f1 = R[j];
            pk0 = std::max (pk0, std::abs (f0));
            const double x0 = f0, x1 = f1;
            if constexpr (Stereo)
            {
                pk1 = std::max (pk1, std::abs (f1));
                xy += x0 * x1;
                xx += x0 * x0;
                yy += x1 * x1;
                rect += 0.5 * (std::abs (x0) + std::abs (x1));
            }
            else
            {
                xx += x0 * x0;
                rect += std::abs (x0);
            }
        }
        spMax[0] = pk0;
        spMax[1] = pk1;
        accXY = xy;
        accXX = xx;
        accYY = yy;
        vuAcc = rect;

        // Pass 2: over count on samples, true peak where the window bound can matter, scope decimation.
        const float thr = overThr;
        if (thr > 0.0f && ! overTrue)
        {
            for (int j = 0; j < len; ++j)
            {
                float v = std::abs (L[j]);
                if constexpr (Stereo) v = std::max (v, std::abs (R[j]));
                if (v > thr) ++overs;
            }
        }
        truePeakPass<Stereo> (len, thr);

        // Vectorscope: every kScopeDecimation-th output pair, counted across blocks.
        for (int j = (kScopeDecimation - scopePhase) % kScopeDecimation; j < len; j += kScopeDecimation) scopeRing.push (L[j], R[j]);
        scopePhase = (scopePhase + len) % kScopeDecimation;

        // The last kTpHistory samples are the history of the next chunk.
        std::memmove (tpBuf[0], tpBuf[0] + len, sizeof (float) * H);
        std::memmove (tpBuf[1], tpBuf[1] + len, sizeof (float) * H);
    }

    // Peak of the interpolated signal at window start t: the 4x grid of the slot (t + 7), both channels.
    template <bool Stereo>
    float slotPeakAt (int t) const noexcept
    {
        float best = 0.0f;
        for (int c = 0; c < (Stereo ? 2 : 1); ++c)
        {
            const float* w = tpBuf[c] + t;
            float v = std::abs (w[TruePeakKernel::kHalf - 1]);
            for (int p = 1; p < tpKernel.phases; ++p) v = std::max (v, std::abs (truePeakDot (tpKernel.h[p], w)));
            best = std::max (best, v);
        }
        return best;
    }

    // Candidate test for the true peak. Every interpolated value of a window is split into its central
    // kCoreTaps taps (computed) and the remaining taps, which are bounded by Cauchy-Schwarz with the window's
    // energy: |Y_p| <= |core_p| + sqrt(tailE_p * E). The slot sample is bounded by itself. A window that stays
    // under the level (the running maximum, or the over threshold) cannot change either; the margins absorb
    // float rounding. Candidates are interpolated exactly, so the maximum and the over count are exactly those
    // of evaluating every sample. Four consecutive windows are tested per step.
    template <bool Stereo>
    void truePeakPass (int len, float thr) noexcept
    {
        constexpr int H = kTpHistory;
        constexpr int kTail = 4;
        const int N = H + len;
        float e[2][H + kControlBlock + 8], u[H + kControlBlock + 8];
        for (int c = 0; c < (Stereo ? 2 : 1); ++c)
        {
            for (int k = 0; k < N; ++k) e[c][k] = tpBuf[c][k] * tpBuf[c][k];
            // Window sums of length 2, 4, 8, 16 (each step valid where it can reach).
            for (int k = 0; k + 1 < N; ++k) u[k] = e[c][k] + e[c][k + 1];
            for (int k = 0; k + 3 < N; ++k) e[c][k] = u[k] + u[k + 2];
            for (int k = 0; k + 7 < N; ++k) u[k] = e[c][k] + e[c][k + 4];
            for (int k = 0; k + 15 < N; ++k) e[c][k] = u[k] + u[k + 8];
        }

        const bool trueOvers = thr > 0.0f && overTrue;
        const float level = trueOvers ? std::min (tpMax, thr) : tpMax; // the chunk-start level: never above the current one
        for (int t0 = 0; t0 < len; t0 += kTail)
        {
            const unsigned mask = candidateMask<Stereo> (t0, level, e);
            if (mask == 0) continue;
            // The group is interpolated exactly, all four windows at once (windows the bound cleared are exact too,
            // and cannot reach the maximum or the threshold).
            float peak[kTail];
            groupPeaks<Stereo> (t0, peak);
            const int left = std::min (kTail, len - t0);
            for (int i = 0; i < left; ++i)
            {
                tpMax = std::max (tpMax, peak[i]);
                if (trueOvers && peak[i] > thr) ++overs;
            }
        }
    }

    // Exact peaks of the four interpolated windows starting at t0 (both channels), in the arithmetic order of
    // truePeakDot, so the result is bit-identical to slotPeakAt.
    template <bool Stereo>
    void groupPeaks (int t0, float (&out)[4]) const noexcept
    {
#if JUCE_USE_SSE_INTRINSICS
        constexpr int half = kTpHistory / 2;
        const __m128 absMask = _mm_castsi128_ps (_mm_set1_epi32 (0x7fffffff));
        __m128 best = _mm_setzero_ps();
        for (int c = 0; c < (Stereo ? 2 : 1); ++c)
        {
            const float* w = tpBuf[c] + t0;
            // Window vectors X[j] (lane i = window t0 + i, tap j), loaded once and shared by the phases.
            __m128 X[TruePeakKernel::kTaps];
            for (int j = 0; j < TruePeakKernel::kTaps; ++j) X[j] = _mm_loadu_ps (w + j);
            best = _mm_max_ps (best, _mm_and_ps (X[half], absMask)); // the slot samples
            for (int p = 1; p < tpKernel.phases; ++p)
            {
                __m128 v[4];
                for (int l = 0; l < 4; ++l)
                {
                    // Same order as truePeakDot: lane l sums taps l, l+4, l+8, l+12 in turn; then (v0+v1)+(v2+v3).
                    __m128 sum = _mm_mul_ps (_mm_loadu_ps (tpBroad[p][l]), X[l]);
                    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (tpBroad[p][4 + l]), X[4 + l]));
                    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (tpBroad[p][8 + l]), X[8 + l]));
                    sum = _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (tpBroad[p][12 + l]), X[12 + l]));
                    v[l] = sum;
                }
                const __m128 dot = _mm_add_ps (_mm_add_ps (v[0], v[1]), _mm_add_ps (v[2], v[3]));
                best = _mm_max_ps (best, _mm_and_ps (dot, absMask));
            }
        }
        _mm_storeu_ps (out, best);
#else
        for (int i = 0; i < 4; ++i) out[i] = slotPeakAt<Stereo> (t0 + i);
#endif
    }

    static int lowestBit (unsigned m) noexcept
    {
        int i = 0;
        while (((m >> i) & 1u) == 0u) ++i;
        return i;
    }

    // Bit i set when the window starting at t0 + i may reach `level` (conservative, see truePeakPass).
    template <bool Stereo>
    unsigned candidateMask (int t0, float level, const float (&e)[2][kTpHistory + kControlBlock + 8]) const noexcept
    {
        constexpr int half = kTpHistory / 2;
        unsigned bits = 0;
        for (int c = 0; c < (Stereo ? 2 : 1); ++c)
        {
            const float* w = tpBuf[c] + t0;
#if JUCE_USE_SSE_INTRINSICS
            const __m128 lv = _mm_set1_ps (level);
            const __m128 a6 = _mm_loadu_ps (w + half - 1), a7 = _mm_loadu_ps (w + half), a8 = _mm_loadu_ps (w + half + 1), a9 = _mm_loadu_ps (w + half + 2);
            const __m128 absMask = _mm_castsi128_ps (_mm_set1_epi32 (0x7fffffff));
            const __m128 energy = _mm_loadu_ps (e[c] + t0);
            __m128 hit = _mm_cmpgt_ps (_mm_and_ps (a7, absMask), lv); // the slot itself
            for (int p = 1; p < tpKernel.phases; ++p)
            {
                const __m128 core = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_loadu_ps (tpBroad[p][half - 1]), a6), _mm_mul_ps (_mm_loadu_ps (tpBroad[p][half]), a7)),
                                                _mm_add_ps (_mm_mul_ps (_mm_loadu_ps (tpBroad[p][half + 1]), a8), _mm_mul_ps (_mm_loadu_ps (tpBroad[p][half + 2]), a9)));
                const __m128 tail = _mm_sqrt_ps (_mm_mul_ps (_mm_set1_ps (tpTailE[p]), energy));
                const __m128 bound = _mm_add_ps (_mm_and_ps (core, absMask), _mm_mul_ps (tail, _mm_set1_ps (1.002f)));
                hit = _mm_or_ps (hit, _mm_cmpgt_ps (bound, lv));
            }
            bits |= (unsigned) _mm_movemask_ps (hit);
#else
            for (int i = 0; i < 4; ++i)
            {
                const float* x = w + i;
                bool hit = std::abs (x[half]) > level;
                for (int p = 1; p < tpKernel.phases && ! hit; ++p)
                {
                    const float* h = tpKernel.h[p];
                    const float core = h[half - 1] * x[half - 1] + h[half] * x[half] + h[half + 1] * x[half + 1] + h[half + 2] * x[half + 2];
                    hit = std::abs (core) + std::sqrt (tpTailE[p] * e[c][t0 + i]) * 1.002f > level;
                }
                if (hit) bits |= 1u << i;
            }
#endif
        }
        return bits;
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

    // Sum of n values (a multiple of 4), four independent partial sums. Silence sums to exactly zero.
    static double sumOf (const double* p, int n) noexcept
    {
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
        for (int i = 0; i < n; i += 4)
        {
            s0 += p[i];
            s1 += p[i + 1];
            s2 += p[i + 2];
            s3 += p[i + 3];
        }
        return (s0 + s1) + (s2 + s3);
    }

    void endSubBlock() noexcept
    {
        subCount = 0;
        const double e = (acc[0] + acc[1]) / subLen;
        acc[0] = acc[1] = 0.0;
        // The ring is kept twice, so the newest 300 values are contiguous: [subHead+1, subHead+300] in subRing.
        const int h = subHead;
        subRing[(size_t) h] = e;
        subRing[(size_t) (h + kShortSubs)] = e;
        subHead = (h + 1) % kShortSubs;
        if (sinceReset < 0x7fffffffu) ++sinceReset;

        const double eS = sumOf (&subRing[(size_t) (h + 1)], kShortSubs) / kShortSubs;
        const double eM = sumOf (&subRing[(size_t) (h + kShortSubs + 1 - kMomentarySubs)], kMomentarySubs) / kMomentarySubs;
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
    KStage kw[2]; // K-weighting stages 1 and 2, both channels
    float tpBuf[2][kTpHistory + kControlBlock] {}; // true-peak history (first kTpHistory) and the current chunk
    float tpMax = 0.0f, tpTailE[TruePeakKernel::kMaxPhases] {};
    float tpBroad[TruePeakKernel::kMaxPhases][TruePeakKernel::kTaps][4] {}; // kernel taps broadcast for the SIMD interpolation
    VuBallistics vu;

    // Windows.
    std::array<double, 2 * kShortSubs> subRing {}; // each sub-block energy stored twice, see endSubBlock()
    int subHead = 0, subCount = 0, hopPhase = 0, ctrlCount = 0;
    uint32_t sinceReset = 0; // sub-blocks since the integration was (re)started
    double acc[2] {};

    // Gated histograms (0.01 LU bins, -70 to +20 LUFS): integrated loudness and LRA.
    GatedHistogram histogram, lraHistogram;

    // Current values.
    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0;
    double maxMomentary = -200.0, maxShortTerm = -200.0, lra = 0.0, lraLow = -200.0, lraHigh = -200.0;
    float spMax[2] {};
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
    std::atomic<uint64_t> grHead { 0 }; // points pushed since reset
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

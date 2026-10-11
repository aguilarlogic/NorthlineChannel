#pragma once

#include "Filters.h"
#include <array>
#include <atomic>
#include <vector>

namespace nl
{
//==============================================================================
// Wait-free single-producer / single-consumer ring. The audio thread only
// pushes; the UI thread only pops. Capacity is fixed at prepare time.
template <typename T>
class SpscRing
{
public:
    void allocate (size_t minCapacity)
    {
        size_t cap = 1;
        while (cap < minCapacity + 1) cap <<= 1;
        buffer.assign (cap, T {});
        mask = cap - 1;
        readPos.store (0);
        writePos.store (0);
    }

    // Producer. Returns the number of items written (drops what does not fit).
    size_t push (const T* items, size_t count) noexcept
    {
        if (buffer.empty()) return 0;
        const size_t w = writePos.load (std::memory_order_relaxed);
        const size_t r = readPos.load (std::memory_order_acquire);
        const size_t free = mask - ((w - r) & mask);
        count = std::min (count, free);
        for (size_t i = 0; i < count; ++i) buffer[(w + i) & mask] = items[i];
        writePos.store ((w + count) & mask, std::memory_order_release);
        return count;
    }

    bool push (const T& item) noexcept { return push (&item, 1) == 1; }

    // Consumer.
    size_t pop (T* out, size_t maxCount) noexcept
    {
        const size_t r = readPos.load (std::memory_order_relaxed);
        const size_t w = writePos.load (std::memory_order_acquire);
        const size_t avail = (w - r) & mask;
        const size_t count = std::min (avail, maxCount);
        for (size_t i = 0; i < count; ++i) out[i] = buffer[(r + i) & mask];
        readPos.store ((r + count) & mask, std::memory_order_release);
        return count;
    }

private:
    std::vector<T> buffer;
    size_t mask = 0;
    std::atomic<size_t> readPos { 0 }, writePos { 0 };
};

//==============================================================================
// Per-block metering summary produced on the audio thread.
struct MeterFrame
{
    float inPeak[2] {}, outPeak[2] {}, inMs[2] {}, outMs[2] {};
    float gateGr = 0, ds1Gr = 0, ds2Gr = 0, compGr = 0, limGr = 0;
    float moduleUs[6] {}; // PRE, GATE, DS, EQ, COMP, MASTER
    float totalUs = 0, blockUs = 0;
    int numSamples = 0;
};

struct StereoSample { float l, r; };

//==============================================================================
// ITU-R BS.1770-5 / EBU R128 loudness and true-peak meter. Runs off the audio
// thread on samples streamed through an SpscRing.
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        designKWeighting();
        designTruePeak();
        const int hop = (int) std::lround (0.1 * fs); // 100 ms
        hopSamples = hop;
        resetAll();
    }

    void resetAll()
    {
        for (auto& c : kw) { c[0].reset(); c[1].reset(); }
        hopEnergy.fill (0.0);
        hopFill = 0;
        hopAcc[0] = hopAcc[1] = 0.0;
        hopCount = 0;
        hopsSeen = 0;
        histCount.assign (kBins, 0);
        histEnergy.assign (kBins, 0.0);
        truePeakMax = 0.0f;
        for (auto& h : tpHistory) h.fill (0.0f);
        tpPos = 0;
        momentary = shortTerm = integrated = -200.0;
        corrXY = corrXX = corrYY = 0.0;
    }

    void resetIntegrated()
    {
        histCount.assign (kBins, 0);
        histEnergy.assign (kBins, 0.0);
        truePeakMax = 0.0f;
        integrated = -200.0;
    }

    void process (const StereoSample* s, size_t n, int numChannels)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const double x[2] { s[i].l, numChannels > 1 ? s[i].r : 0.0 };
            for (int c = 0; c < numChannels; ++c)
            {
                const double y = kw[c][1].process (kw[c][0].process (x[c]));
                hopAcc[c] += y * y;
            }
            truePeakSample (s[i], numChannels);

            const double cc = 1.0 - 1.0 / (0.3 * fs);
            corrXY = corrXY * cc + x[0] * x[1];
            corrXX = corrXX * cc + x[0] * x[0];
            corrYY = corrYY * cc + x[1] * x[1];

            if (++hopCount >= hopSamples) finishHop (numChannels);
        }
    }

    double getMomentary() const { return momentary; }
    double getShortTerm() const { return shortTerm; }
    double getIntegrated() const { return integrated; }
    float getTruePeakDb() const { return truePeakMax > 0.0f ? 20.0f * std::log10 (truePeakMax) : -200.0f; }
    float getCorrelation() const
    {
        const double d = std::sqrt (corrXX * corrYY);
        return d > 1.0e-12 ? (float) std::clamp (corrXY / d, -1.0, 1.0) : 0.0f;
    }

    // Exposed for tests.
    const BiquadD& kStage (int i) const { return kw[0][i]; }

private:
    static constexpr int kBins = 1000;          // 0.1 LU bins
    static constexpr double kHistMin = -70.0;   // absolute gate
    static constexpr int kTpPhases = 4, kTpHalf = 12, kTpTaps = 2 * kTpHalf + 1;

    void designKWeighting()
    {
        // Stage 1: high shelf (head acoustics); stage 2: RLB high-pass. Formulas
        // reproduce the BS.1770 48 kHz coefficients and generalise to any rate.
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPiD * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            for (auto& c : kw)
            {
                auto& b = c[0];
                b.b0 = (Vh + Vb * K / Q + K * K) / a0;
                b.b1 = 2.0 * (K * K - Vh) / a0;
                b.b2 = (Vh - Vb * K / Q + K * K) / a0;
                b.a1 = 2.0 * (K * K - 1.0) / a0;
                b.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPiD * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            for (auto& c : kw)
            {
                auto& b = c[1];
                b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
                b.a1 = 2.0 * (K * K - 1.0) / a0;
                b.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }
    }

    void designTruePeak()
    {
        // 4x interpolation with a Blackman-Harris windowed sinc (25 taps per phase).
        // Phase 0 reproduces the original sample exactly.
        for (int p = 0; p < kTpPhases; ++p)
        {
            double sum = 0.0;
            for (int j = 0; j < kTpTaps; ++j)
            {
                const double u = (double) (j - kTpHalf) + (double) p / kTpPhases;
                const double sinc = std::abs (u) < 1.0e-12 ? 1.0 : std::sin (kPiD * u) / (kPiD * u);
                const double wpos = (u + (kTpHalf + 1.0)) / (2.0 * (kTpHalf + 1.0)); // 0..1
                const double w = 0.35875 - 0.48829 * std::cos (2 * kPiD * wpos) + 0.14128 * std::cos (4 * kPiD * wpos) - 0.01168 * std::cos (6 * kPiD * wpos);
                tpKernel[(size_t) p][(size_t) j] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (auto& k : tpKernel[(size_t) p]) k = (float) (k / sum);
        }
    }

    void truePeakSample (const StereoSample& s, int numChannels)
    {
        const float in[2] { s.l, s.r };
        for (int c = 0; c < numChannels; ++c)
        {
            auto& h = tpHistory[(size_t) c];
            h[(size_t) tpPos] = in[c];
            h[(size_t) (tpPos + kTpTaps)] = in[c]; // mirrored for contiguous reads
        }
        tpPos = (tpPos + 1) % kTpTaps;

        for (int c = 0; c < numChannels; ++c)
        {
            const float* hist = tpHistory[(size_t) c].data() + tpPos; // oldest first
            for (int p = 0; p < kTpPhases; ++p)
            {
                // Output at time (newest - kTpHalf) + p/4: kernel index j pairs with
                // sample (newest - j) -> hist[kTpTaps - 1 - j].
                const auto& k = tpKernel[(size_t) p];
                float acc = 0.0f;
                for (int j = 0; j < kTpTaps; ++j) acc += k[(size_t) j] * hist[kTpTaps - 1 - j];
                truePeakMax = std::max (truePeakMax, std::abs (acc));
            }
        }
    }

    void finishHop (int numChannels)
    {
        double e = 0.0;
        for (int c = 0; c < numChannels; ++c) e += hopAcc[c];
        hopAcc[0] = hopAcc[1] = 0.0;
        hopEnergy[(size_t) hopFill] = e / hopSamples;
        hopFill = (hopFill + 1) % (int) hopEnergy.size();
        hopCount = 0;
        ++hopsSeen;

        auto meanOf = [this] (int hops)
        {
            double s = 0.0;
            for (int k = 1; k <= hops; ++k)
                s += hopEnergy[(size_t) ((hopFill - k + (int) hopEnergy.size()) % (int) hopEnergy.size())];
            return s / hops;
        };
        auto toLufs = [] (double energy) { return energy > 0.0 ? -0.691 + 10.0 * std::log10 (energy) : -200.0; };

        if (hopsSeen >= 4)
        {
            const double blockEnergy = meanOf (4); // 400 ms gating block, 75 % overlap
            momentary = toLufs (blockEnergy);
            if (momentary > kHistMin)
            {
                const int bin = std::clamp ((int) ((momentary - kHistMin) * 10.0), 0, kBins - 1);
                histCount[(size_t) bin]++;
                histEnergy[(size_t) bin] += blockEnergy;
                computeIntegrated();
            }
        }
        if (hopsSeen >= 30) shortTerm = toLufs (meanOf (30));
    }

    void computeIntegrated()
    {
        double sum = 0.0;
        long long count = 0;
        for (int b = 0; b < kBins; ++b) { sum += histEnergy[(size_t) b]; count += histCount[(size_t) b]; }
        if (count == 0) { integrated = -200.0; return; }
        const double ungated = -0.691 + 10.0 * std::log10 (sum / (double) count);
        const double relGate = ungated - 10.0;
        sum = 0.0;
        count = 0;
        const int startBin = std::clamp ((int) std::ceil ((relGate - kHistMin) * 10.0), 0, kBins);
        for (int b = startBin; b < kBins; ++b) { sum += histEnergy[(size_t) b]; count += histCount[(size_t) b]; }
        integrated = count > 0 ? -0.691 + 10.0 * std::log10 (sum / (double) count) : -200.0;
    }

    double fs = 48000.0;
    BiquadD kw[2][2];
    std::array<double, 32> hopEnergy {};
    int hopFill = 0, hopCount = 0, hopSamples = 4800;
    long long hopsSeen = 0;
    double hopAcc[2] {};
    std::vector<long long> histCount;
    std::vector<double> histEnergy;
    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0;
    std::array<std::array<float, kTpTaps>, kTpPhases> tpKernel {};
    std::array<std::array<float, 2 * kTpTaps>, 2> tpHistory {};
    int tpPos = 0;
    float truePeakMax = 0.0f;
    double corrXY = 0, corrXX = 0, corrYY = 0;
};
} // namespace nl

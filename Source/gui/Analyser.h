#pragma once

#include "../dsp/ChannelStrip.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace nl
{
//==============================================================================
// Spectrum maths. Pure functions and a small FFT helper, used by the analyser threads and by tests.
namespace spectrum
{
constexpr int kFftOrder = 12, kFftSize = 1 << kFftOrder, kHop = kFftSize / 4; // 4096 FFT, 75 % overlap
constexpr int kBins = kFftSize / 2 + 1;
constexpr int kDisplayBins = 256;                                              // log-spaced, 20 Hz to 20 kHz
constexpr double kTiltDbPerOct = 4.5;                                          // display tilt about 1 kHz
constexpr int kHarmonicOrder = 13, kHarmonicFftSize = 1 << kHarmonicOrder;     // 8192 for the harmonic bars
constexpr int kHarmonicHop = kHarmonicFftSize / 4;
constexpr int kHarmonics = 6;                                                  // H2 .. H7

// Display tilt in dB at a frequency: +4.5 dB per octave about 1 kHz.
double tiltDb (double freqHz) noexcept;
// Edges of the display bins: displayEdge (0) = 20 Hz, displayEdge (kDisplayBins) = 20 kHz.
double displayEdge (int edge) noexcept;
// Display bin that holds a frequency (clamped to the range).
int displayBinForFrequency (double freqHz) noexcept;
// Centre frequency of the bin k of an FFT of `fftSize` points at `fs`.
double binFrequency (int bin, double fs, int fftSize) noexcept;

// Hann-windowed FFT of a real signal, returned as level in dBFS per bin (size / 2 + 1 values). A sine of
// amplitude A at a bin centre reads 20 log10 (A) there. Owns its FFT and buffers; not thread safe.
class Analysis
{
public:
    explicit Analysis (int order);
    int size() const noexcept { return n; }
    int numBins() const noexcept { return n / 2 + 1; }
    // x: size() samples (not modified). outDb: numBins() values.
    void analyse (const float* x, float* outDb);

private:
    int n;
    juce::dsp::FFT fft;
    std::vector<float> window, buf;
};

// Maps a dB spectrum onto the display bins: each bin takes the largest value within its edges (or the
// interpolated value at its centre where the FFT is coarser), plus the display tilt.
void toDisplay (const float* binDb, int numBins, int fftSize, double fs, float* display, int numDisplay) noexcept;

// Harmonic levels from a dB spectrum (fftSize points at fs): out[0] is the fundamental in dBFS and
// out[1..count] are harmonics 2 .. count + 1 relative to it (dBc). f0 <= 0 picks the strongest bin
// between 40 Hz and 1 kHz.
void harmonicLevels (const float* binDb, int fftSize, double fs, double f0, float* out, int count) noexcept;

// Lock-free publication of one frame from the analyser thread to the UI (seqlock: the writer never
// waits, the reader retries when a write overlaps its copy).
template <size_t N>
class Seqlock
{
public:
    void publish (const float* src) noexcept
    {
        const auto s = seq.load (std::memory_order_relaxed);
        seq.store (s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (size_t i = 0; i < N; ++i) data[i].store (src[i], std::memory_order_relaxed);
        seq.store (s + 2, std::memory_order_release);
    }

    // False when nothing has been published yet.
    bool read (float* dst) const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if (s1 == 0) return false;
            if (s1 & 1u) continue;
            for (size_t i = 0; i < N; ++i) dst[i] = data[i].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s1) return true;
        }
        return false;
    }

    uint32_t version() const noexcept { return seq.load (std::memory_order_acquire); }

private:
    std::atomic<uint32_t> seq { 0 };
    std::array<std::atomic<float>, N> data {};
};
} // namespace spectrum

//==============================================================================
// Pre / post spectrum of the EQ slot, on a background thread. Consumes the EQ's input and output
// taps (mono), runs a 4096-point FFT every 1024 samples (75 % overlap), and publishes a display
// spectrum and a peak-hold curve for each side. Freeze stops the updates and keeps the last frame.
class EqAnalyser : public juce::Thread
{
public:
    explicit EqAnalyser (ChannelStrip& strip);
    ~EqAnalyser() override;

    // Starts consuming the taps (the strip streams while any analyser is attached).
    void attach();
    void detach();
    bool isAttached() const noexcept { return attached; }

    void setFrozen (bool shouldFreeze) noexcept { frozen.store (shouldFreeze, std::memory_order_relaxed); }
    bool isFrozen() const noexcept { return frozen.load (std::memory_order_relaxed); }

    // Latest frames, spectrum::kDisplayBins dB values each. The hold curve decays at 20 dB per second.
    bool readPre (float* dst) const noexcept { return channels[0].frame.read (dst); }
    bool readPost (float* dst) const noexcept { return channels[1].frame.read (dst); }
    uint32_t frameCount() const noexcept { return channels[1].frame.version(); }

    void run() override;

private:
    struct Channel
    {
        ChannelStrip::AnalyserTap tap;
        std::array<float, spectrum::kFftSize> hist {};
        int writePos = 0, filled = 0, sinceHop = 0;
        std::array<float, spectrum::kDisplayBins> live {}, hold {};
        spectrum::Seqlock<2 * spectrum::kDisplayBins> frame;
    };

    void consume (Channel& ch, const float* samples, size_t n);
    void analyseFrame (Channel& ch);

    ChannelStrip& strip;
    spectrum::Analysis analysis { spectrum::kFftOrder };
    std::array<Channel, 2> channels;
    std::vector<float> dbBuf, linBuf, popBuf;
    std::atomic<bool> frozen { false };
    bool attached = false;
};

//==============================================================================
// Harmonic levels of the PRE output, on a background thread (8192-point FFT every 2048 samples).
// Publishes the fundamental (dBFS) and H2 .. H7 (dBc) for the harmonic bars.
class HarmonicAnalyser : public juce::Thread
{
public:
    explicit HarmonicAnalyser (ChannelStrip& strip);
    ~HarmonicAnalyser() override;

    void attach();
    void detach();
    bool isAttached() const noexcept { return attached; }

    // out: 1 + spectrum::kHarmonics values (fundamental dBFS, then dBc for H2 .. H7).
    bool read (float* out) const noexcept { return levels.read (out); }

    void run() override;

private:
    ChannelStrip& strip;
    spectrum::Analysis analysis { spectrum::kHarmonicOrder };
    std::array<float, spectrum::kHarmonicFftSize> hist {};
    int writePos = 0, filled = 0, sinceHop = 0;
    std::vector<float> dbBuf, linBuf, popBuf;
    spectrum::Seqlock<1 + spectrum::kHarmonics> levels;
    bool attached = false;
};
} // namespace nl

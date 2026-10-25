#include "Analyser.h"

namespace nl
{
namespace spectrum
{
double tiltDb (double freqHz) noexcept
{
    return kTiltDbPerOct * std::log2 (std::max (freqHz, 1.0e-3) / 1000.0);
}

double displayEdge (int edge) noexcept
{
    return 20.0 * std::pow (1000.0, (double) edge / (double) kDisplayBins);
}

int displayBinForFrequency (double freqHz) noexcept
{
    const double x = std::log (std::max (freqHz, 20.0) / 20.0) / std::log (1000.0) * kDisplayBins;
    return juce::jlimit (0, kDisplayBins - 1, (int) std::floor (x));
}

double binFrequency (int bin, double fs, int fftSize) noexcept
{
    return (double) bin * fs / (double) fftSize;
}

Analysis::Analysis (int order)
    : n (1 << order), fft (order), window ((size_t) 1 << order), buf ((size_t) 2 << order, 0.0f)
{
    // Periodic Hann: its coefficients sum to n / 2, which fixes the amplitude scale in analyse().
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);
}

void Analysis::analyse (const float* x, float* outDb)
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = x[i] * window[(size_t) i];
    // Magnitudes of bins 0 .. n / 2 in the first n / 2 + 1 values. The forward transform is unscaled, so a
    // sine of amplitude A at a bin centre gives |X| = A n / 4, and 4 / n maps it back to A.
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    const float scale = 4.0f / (float) n;
    for (int k = 0; k <= n / 2; ++k)
        outDb[k] = juce::Decibels::gainToDecibels (buf[(size_t) k] * scale, -120.0f);
}

void toDisplay (const float* db, int numBins, int fftSize, double fs, float* out, int numDisplay) noexcept
{
    const double binHz = fs / (double) fftSize;
    for (int i = 0; i < numDisplay; ++i)
    {
        const double lo = 20.0 * std::pow (1000.0, (double) i / numDisplay);
        const double hi = 20.0 * std::pow (1000.0, (double) (i + 1) / numDisplay);
        const double fc = std::sqrt (lo * hi);

        // Interpolated value at the centre (the FFT may be coarser than the display at low frequencies)...
        const double x = fc / binHz;
        const int k0 = juce::jlimit (0, numBins - 1, (int) std::floor (x));
        const int k1 = juce::jmin (numBins - 1, k0 + 1);
        const float frac = (float) juce::jlimit (0.0, 1.0, x - (double) k0);
        float v = db[k0] + (db[k1] - db[k0]) * frac;

        // ...and the largest FFT bin inside the display bin, so narrow peaks are not lost.
        const int first = juce::jmax (0, (int) std::ceil (lo / binHz));
        const int last = juce::jmin (numBins - 1, (int) std::floor (hi / binHz));
        for (int k = first; k <= last; ++k) v = std::max (v, db[k]);

        out[i] = v + (float) tiltDb (fc);
    }
}

void harmonicLevels (const float* db, int fftSize, double fs, double f0, float* out, int count) noexcept
{
    const int numBins = fftSize / 2 + 1;
    const double binHz = fs / (double) fftSize;
    auto peakNear = [&] (double f)
    {
        const int k = (int) std::lround (f / binHz);
        float v = -120.0f;
        for (int j = juce::jmax (0, k - 2); j <= juce::jmin (numBins - 1, k + 2); ++j) v = std::max (v, db[j]);
        return v;
    };

    double fund = f0;
    if (fund <= 0.0)
    {
        int best = (int) std::ceil (40.0 / binHz);
        for (int k = best; k <= (int) std::floor (1000.0 / binHz) && k < numBins; ++k)
            if (db[k] > db[best]) best = k;
        fund = best * binHz;
    }

    out[0] = peakNear (fund);
    for (int h = 2; h <= count + 1; ++h)
        out[h - 1] = h * fund < 0.5 * fs ? peakNear (h * fund) - out[0] : -120.0f;
}
} // namespace spectrum

//==============================================================================
EqAnalyser::EqAnalyser (ChannelStrip& s) : juce::Thread ("EQ analyser"), strip (s)
{
    channels[0].tap = ChannelStrip::AnalyserTap::eqIn;
    channels[1].tap = ChannelStrip::AnalyserTap::eqOut;
    dbBuf.assign ((size_t) spectrum::kBins, -120.0f);
    linBuf.assign ((size_t) spectrum::kFftSize, 0.0f);
    popBuf.assign ((size_t) spectrum::kFftSize, 0.0f);
}

EqAnalyser::~EqAnalyser() { detach(); }

void EqAnalyser::attach()
{
    if (attached) return;
    attached = true;
    strip.analyserStart();
    startThread();
}

void EqAnalyser::detach()
{
    if (! attached) return;
    // Wait for the thread to exit: its loop reads members of this object, which die right after this call.
    stopThread (-1);
    strip.analyserStop();
    attached = false;
}

void EqAnalyser::run()
{
    for (auto& ch : channels)
    {
        ch.writePos = ch.filled = ch.sinceHop = 0;
        ch.live.fill (-120.0f);
        ch.hold.fill (-120.0f);
        // Drop whatever the taps hold from before this attach.
        while (strip.popAnalyser (ch.tap, popBuf.data(), popBuf.size()) == popBuf.size()) {}
    }

    while (! threadShouldExit())
    {
        size_t total = 0;
        for (auto& ch : channels)
            for (;;)
            {
                const size_t got = strip.popAnalyser (ch.tap, popBuf.data(), popBuf.size());
                if (got == 0) break;
                consume (ch, popBuf.data(), got);
                total += got;
                if (got < popBuf.size()) break;
            }
        if (total == 0) wait (10);
    }
}

void EqAnalyser::consume (Channel& ch, const float* s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        ch.hist[(size_t) ch.writePos] = s[i];
        ch.writePos = (ch.writePos + 1) & (spectrum::kFftSize - 1);
        if (ch.filled < spectrum::kFftSize) ++ch.filled;
        if (++ch.sinceHop >= spectrum::kHop && ch.filled >= spectrum::kFftSize)
        {
            ch.sinceHop = 0;
            analyseFrame (ch);
        }
    }
}

void EqAnalyser::analyseFrame (Channel& ch)
{
    if (frozen.load (std::memory_order_relaxed)) return;

    // The oldest sample sits at writePos.
    for (int i = 0; i < spectrum::kFftSize; ++i)
        linBuf[(size_t) i] = ch.hist[(size_t) ((ch.writePos + i) & (spectrum::kFftSize - 1))];
    analysis.analyse (linBuf.data(), dbBuf.data());

    const double fs = strip.getSampleRate() > 0.0 ? strip.getSampleRate() : 48000.0;
    spectrum::toDisplay (dbBuf.data(), spectrum::kBins, spectrum::kFftSize, fs, ch.live.data(), spectrum::kDisplayBins);

    // Peak hold: jumps up with the signal, falls at 20 dB per second.
    const float decay = (float) (20.0 * spectrum::kHop / fs);
    for (int i = 0; i < spectrum::kDisplayBins; ++i)
        ch.hold[(size_t) i] = std::max (ch.live[(size_t) i], ch.hold[(size_t) i] - decay);

    std::array<float, 2 * spectrum::kDisplayBins> packed;
    std::copy (ch.live.begin(), ch.live.end(), packed.begin());
    std::copy (ch.hold.begin(), ch.hold.end(), packed.begin() + spectrum::kDisplayBins);
    ch.frame.publish (packed.data());
}

//==============================================================================
HarmonicAnalyser::HarmonicAnalyser (ChannelStrip& s) : juce::Thread ("Harmonic analyser"), strip (s)
{
    dbBuf.assign ((size_t) (spectrum::kHarmonicFftSize / 2 + 1), -120.0f);
    linBuf.assign ((size_t) spectrum::kHarmonicFftSize, 0.0f);
    popBuf.assign ((size_t) spectrum::kHarmonicFftSize, 0.0f);
}

HarmonicAnalyser::~HarmonicAnalyser() { detach(); }

void HarmonicAnalyser::attach()
{
    if (attached) return;
    attached = true;
    strip.analyserStart();
    startThread();
}

void HarmonicAnalyser::detach()
{
    if (! attached) return;
    // Wait for the thread to exit: its loop reads members of this object, which die right after this call.
    stopThread (-1);
    strip.analyserStop();
    attached = false;
}

void HarmonicAnalyser::run()
{
    writePos = filled = sinceHop = 0;
    while (strip.popAnalyser (ChannelStrip::AnalyserTap::preOut, popBuf.data(), popBuf.size()) == popBuf.size()) {}

    std::array<float, 1 + spectrum::kHarmonics> lv {};
    while (! threadShouldExit())
    {
        size_t total = 0;
        for (;;)
        {
            const size_t got = strip.popAnalyser (ChannelStrip::AnalyserTap::preOut, popBuf.data(), popBuf.size());
            if (got == 0) break;
            total += got;
            for (size_t i = 0; i < got; ++i)
            {
                hist[(size_t) writePos] = popBuf[i];
                writePos = (writePos + 1) & (spectrum::kHarmonicFftSize - 1);
                if (filled < spectrum::kHarmonicFftSize) ++filled;
                if (++sinceHop >= spectrum::kHarmonicHop && filled >= spectrum::kHarmonicFftSize)
                {
                    sinceHop = 0;
                    for (int k = 0; k < spectrum::kHarmonicFftSize; ++k)
                        linBuf[(size_t) k] = hist[(size_t) ((writePos + k) & (spectrum::kHarmonicFftSize - 1))];
                    analysis.analyse (linBuf.data(), dbBuf.data());
                    const double fs = strip.getSampleRate() > 0.0 ? strip.getSampleRate() : 48000.0;
                    spectrum::harmonicLevels (dbBuf.data(), spectrum::kHarmonicFftSize, fs, 0.0, lv.data() + 0, spectrum::kHarmonics);
                    levels.publish (lv.data());
                }
            }
            if (got < popBuf.size()) break;
        }
        if (total == 0) wait (10);
    }
}
} // namespace nl

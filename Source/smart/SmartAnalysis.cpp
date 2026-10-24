#include "SmartAnalysis.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace nl::smart
{
namespace
{
using cd = std::complex<double>;

constexpr float kFloorDb = -120.0f;
constexpr double kTwoPi = 2.0 * juce::MathConstants<double>::pi;

// Pitch and gate thresholds (see the roadmap table in 9.1).
constexpr float kYinThreshold = 0.15f;      // absolute YIN threshold on the cumulative mean difference
constexpr float kStableRatio = 0.03f;       // a voiced frame is stable when a neighbour is within 3 %
constexpr float kGateSeparationDb = 15.0f;  // the gate is switched on only above this separation
constexpr float kSibilantGrDb = 4.5f;       // target gain reduction on the sibilant events (3 to 6 dB)
constexpr float kDeEssSlope = 0.8f;         // the de-esser gain computer above its knee: 5:1
constexpr float kDetectorPeakDb = 5.0f;     // de-esser detector (band peak envelope) above the band RMS; calibrated on bursts
constexpr float kEqMoveMaxDb = 3.0f;        // EQ moves are limited to +/- 3 dB
constexpr float kEqMinDeviationDb = 1.5f;   // deviations below this are left alone
constexpr float kCompGrTargetDb = 3.5f;     // median gain reduction on the active segments (3 to 4 dB)

// Parameter indices of the four EQ bands: each band block has the same stride.
constexpr int eqBandOn (int band) { return (int) P::eqLowOn + band * P::kEqBandStride; }
constexpr int eqBandGain (int band) { return (int) P::eqLowGain + band * P::kEqBandStride; }
constexpr float kEqBandCentre[4] { 100.0f, 500.0f, 2500.0f, 10000.0f };

// Iterative radix-2 complex FFT (size must be a power of two).
class Fft
{
public:
    explicit Fft (int size)
        : n (size), rev ((size_t) size), tw ((size_t) std::max (1, size / 2))
    {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if ((i >> b) & 1) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0, -kTwoPi * i / n);
    }

    int size() const noexcept { return n; }

    void run (cd* a, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (a[i], a[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cd w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const cd u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
        if (inverse)
            for (int i = 0; i < n; ++i) a[i] /= (double) n;
    }

private:
    int n;
    std::vector<int> rev;
    std::vector<cd> tw;
};

int nextPow2 (int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

std::vector<double> hann (int n)
{
    std::vector<double> w ((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5 - 0.5 * std::cos (kTwoPi * i / n);
    return w;
}

double sumSquares (const std::vector<double>& w)
{
    double s = 0.0;
    for (double v : w) s += v * v;
    return s;
}

// Hann-windowed one-sided power spectrum of x[0..len). out[k] = |X[k]|^2, k = 0 .. size/2.
void powerSpectrum (const float* x, int len, const Fft& fft, const std::vector<double>& win, std::vector<cd>& buf, std::vector<double>& out)
{
    const int n = fft.size();
    buf.assign ((size_t) n, cd (0.0, 0.0));
    for (int i = 0; i < std::min (len, n); ++i) buf[(size_t) i] = cd ((double) x[i] * win[(size_t) i], 0.0);
    fft.run (buf.data(), false);
    out.assign ((size_t) n / 2 + 1, 0.0);
    for (int k = 0; k <= n / 2; ++k) out[(size_t) k] = std::norm (buf[(size_t) k]);
}

// Histogram of dB values with a fixed bin width. Percentiles read the bin centres.
struct Hist
{
    double lo, step;
    std::vector<double> c;
    double total = 0.0;

    Hist (double lowDb, double binDb, int bins)
        : lo (lowDb), step (binDb), c ((size_t) bins, 0.0) {}

    int bin (double db) const
    {
        const int b = (int) std::floor ((db - lo) / step);
        return std::clamp (b, 0, (int) c.size() - 1);
    }
    double centre (int b) const { return lo + (b + 0.5) * step; }
    void add (double db)
    {
        c[(size_t) bin (db)] += 1.0;
        total += 1.0;
    }
    double percentile (double p) const
    {
        if (total <= 0.0) return kFloorDb;
        const double target = p * total;
        double acc = 0.0;
        for (size_t i = 0; i < c.size(); ++i)
        {
            acc += c[i];
            if (acc >= target) return centre ((int) i);
        }
        return centre ((int) c.size() - 1);
    }
};

// Median of a copy of v (deterministic: the vector is sorted, the middle taken).
double median (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

// Short-term RMS level in dB of consecutive windows of `win` samples, hop `hop`.
std::vector<float> windowDb (const float* x, int n, int win, int hop)
{
    std::vector<float> out;
    for (int s = 0; s + win <= n; s += hop)
    {
        double ms = 0.0;
        for (int j = 0; j < win; ++j) ms += (double) x[s + j] * x[s + j];
        ms /= (double) win;
        out.push_back ((float) (10.0 * std::log10 (std::max (ms, 1.0e-12))));
    }
    return out;
}

float rmsDbOf (const float* x, int n)
{
    double ms = 0.0;
    for (int j = 0; j < n; ++j) ms += (double) x[j] * x[j];
    return (float) (10.0 * std::log10 (std::max (ms / std::max (1, n), 1.0e-12)));
}

float roundTo (float v, float step) { return std::round (v / step) * step; }

// Level and crest factor (peak minus RMS, in dB) of the 100 ms windows above the activity gate.
struct Segments
{
    std::vector<float> levelDb;  // active RMS levels
    std::vector<float> crestDb;  // active crest factors
};

} // namespace

//==============================================================================
const char* sourceName (Source s) noexcept
{
    static const char* names[] { "Vocal", "Kick", "Snare", "Toms", "Bass", "Guitar", "Keys", "Bus", "Dialogue" };
    return names[juce::jlimit (0, kNumSources - 1, (int) s)];
}

Source sourceFromIndex (int index) noexcept
{
    return (Source) juce::jlimit (0, kNumSources - 1, index);
}

ToneTarget toneTarget (Source s) noexcept
{
    // Starting targets: long-term level in dB at 100 Hz, 500 Hz, 2.5 kHz and 10 kHz relative to 1 kHz.
    switch (s)
    {
        case Source::Vocal:    return { 2.0f, 0.5f, 1.5f, -1.0f };
        case Source::Kick:     return { 3.0f, -2.0f, -3.0f, -8.0f };
        case Source::Snare:    return { 0.0f, 0.0f, 2.0f, -2.0f };
        case Source::Toms:     return { 2.0f, 0.0f, 1.0f, -3.0f };
        case Source::Bass:     return { 3.0f, 0.0f, -1.0f, -8.0f };
        case Source::Guitar:   return { 0.0f, 1.0f, 1.0f, -2.0f };
        case Source::Keys:     return { 1.0f, 0.0f, 0.5f, -1.0f };
        case Source::Bus:      return { 1.0f, 0.0f, 0.5f, -1.5f };
        case Source::Dialogue: return { 1.0f, 0.5f, 1.0f, -2.0f };
        default:               return { 0.0f, 0.0f, 0.0f, 0.0f };
    }
}

int compTypeDefault (Source s) noexcept
{
    // Choice index: 0 VCA, 1 FET, 2 OPT, 3 SOFT.
    switch (s)
    {
        case Source::Vocal:    return 1;
        case Source::Dialogue: return 2;
        case Source::Bass:     return 2;
        case Source::Guitar:   return 2;
        case Source::Keys:     return 2;
        case Source::Bus:      return 3;
        default:               return 0; // Kick, Snare, Toms
    }
}

std::array<float, P::count> defaultValues()
{
    std::array<float, P::count> v {};
    for (int i = 0; i < P::count; ++i) v[(size_t) i] = paramDef (i).defV;
    return v;
}

//==============================================================================
float yinPitch (const float* x, int n, double sampleRate, float* aperiodicity)
{
    if (aperiodicity != nullptr) *aperiodicity = 1.0f;
    const int W = n / 2;
    const int tauMax = std::min (W, (int) std::floor (sampleRate / 40.0));
    const int tauMin = std::max (2, (int) std::floor (sampleRate / 1000.0));
    if (tauMax < tauMin + 3) return 0.0f;

    // Cross-correlation c[tau] = sum_j x[j] x[j + tau] over j < W, through one FFT pair.
    const int M = nextPow2 (W + tauMax);
    Fft fft (M);
    std::vector<cd> a ((size_t) M, cd (0.0, 0.0)), b ((size_t) M, cd (0.0, 0.0));
    for (int j = 0; j < W; ++j) a[(size_t) j] = cd (x[j], 0.0);
    for (int j = 0; j < W + tauMax; ++j) b[(size_t) j] = cd (x[j], 0.0);
    fft.run (a.data(), false);
    fft.run (b.data(), false);
    for (int k = 0; k < M; ++k) b[(size_t) k] = std::conj (a[(size_t) k]) * b[(size_t) k];
    fft.run (b.data(), true);

    // Cumulative energy of the frame.
    std::vector<double> cs ((size_t) n + 1, 0.0);
    for (int j = 0; j < n; ++j) cs[(size_t) j + 1] = cs[(size_t) j] + (double) x[j] * x[j];
    const double e1 = cs[(size_t) W];

    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const double e2 = cs[(size_t) (tau + W)] - cs[(size_t) tau];
        d[(size_t) tau] = std::max (0.0, e1 + e2 - 2.0 * b[(size_t) tau].real());
    }

    // Cumulative mean normalised difference.
    std::vector<double> dp ((size_t) tauMax + 1, 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[(size_t) tau];
        dp[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
    }

    int best = tauMin;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (dp[(size_t) tau] < dp[(size_t) best]) best = tau;
    if (aperiodicity != nullptr) *aperiodicity = (float) dp[(size_t) best];

    // First dip under the threshold, walked down to its local minimum and refined by a parabola.
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (dp[(size_t) tau] >= kYinThreshold) continue;
        int t = tau;
        while (t + 1 <= tauMax && dp[(size_t) t + 1] < dp[(size_t) t]) ++t;
        double refined = t;
        if (t > 1 && t < tauMax)
        {
            const double y0 = dp[(size_t) t - 1], y1 = dp[(size_t) t], y2 = dp[(size_t) t + 1];
            const double den = y0 - 2.0 * y1 + y2;
            if (std::abs (den) > 1.0e-12) refined = t + 0.5 * (y0 - y2) / den;
        }
        if (aperiodicity != nullptr) *aperiodicity = (float) dp[(size_t) t];
        return refined > 0.0 ? (float) (sampleRate / refined) : 0.0f;
    }
    return 0.0f;
}

//==============================================================================
Result analyze (const float* x, int n, double fs, Source source, const std::array<float, P::count>& current)
{
    Result r;
    r.source = source;
    auto& m = r.m;
    const auto cur = [&] (int p) { return current[(size_t) p]; };
    const auto add = [&] (int param, float to, const juce::String& reason)
    {
        const float from = cur (param);
        if (std::abs (to - from) < 1.0e-4f) return;
        r.changes.push_back ({ param, from, to, reason });
    };

    const int win100 = std::max (1, (int) std::lround (0.1 * fs));
    const int win10 = std::max (1, (int) std::lround (0.01 * fs));
    if (n < 10 * win100 || fs <= 0.0)
    {
        r.summary = "Capture too short for Smart Start.";
        return r;
    }

    //----------------------------------------------------------------------------
    // Level: 100 ms RMS histogram. The 90th percentile is the program level, the 10th the noise floor.
    const auto L100 = windowDb (x, n, win100, win100);
    Hist h100 (-120.0, 0.1, 1260);
    for (float v : L100) h100.add (v);
    m.noiseDb = (float) h100.percentile (0.10);
    m.programDb = (float) h100.percentile (0.90);
    if (m.programDb < -80.0f)
    {
        r.summary = "No signal in the capture. Play the source while Listen runs.";
        return r;
    }
    r.valid = true;

    const float opLevel = cur (P::opLevel);
    const float trim = std::clamp (opLevel - m.programDb, -24.0f, 24.0f);
    add (P::inGain, roundTo (trim, 0.1f),
         juce::String::formatted ("Input %+.1f dB: program 90th percentile %.1f dB to Operating Level %.0f dB",
                                  roundTo (trim, 0.1f), m.programDb, opLevel));

    //----------------------------------------------------------------------------
    // Gate: 10 ms RMS histogram, its valley between the noise and program modes.
    const auto L10 = windowDb (x, n, win10, win10);
    Hist h10 (-120.0, 0.5, 252);
    for (float v : L10) h10.add (v);
    const double noise10 = h10.percentile (0.10);
    const double program10 = h10.percentile (0.97);
    const double separation = program10 - noise10;
    m.gateSeparationDb = (float) separation;

    // Smooth the histogram (5-bin box, then [1 2 1]) so that counting noise does not make false valleys.
    std::vector<double> H = h10.c;
    {
        std::vector<double> box (H.size(), 0.0);
        for (int i = 0; i < (int) H.size(); ++i)
        {
            double s = 0.0;
            int cnt = 0;
            for (int k = i - 2; k <= i + 2; ++k)
                if (k >= 0 && k < (int) H.size()) { s += H[(size_t) k]; ++cnt; }
            box[(size_t) i] = s / cnt;
        }
        for (int i = 0; i < (int) H.size(); ++i)
        {
            const double l = box[(size_t) std::max (0, i - 1)], c = box[(size_t) i], rr = box[(size_t) std::min ((int) H.size() - 1, i + 1)];
            H[(size_t) i] = 0.25 * l + 0.5 * c + 0.25 * rr;
        }
    }
    const int iNoisePeak = [&]
    {
        int best = h10.bin (noise10 - 3.0);
        for (int i = h10.bin (noise10 - 3.0); i <= h10.bin (noise10 + 3.0); ++i)
            if (H[(size_t) i] > H[(size_t) best]) best = i;
        return best;
    }();
    const int iLast = h10.bin (program10 - 6.0);
    int iValley = -1;
    for (int i = iNoisePeak + 1; i < iLast; ++i)
        if (H[(size_t) i] <= H[(size_t) i - 1] && H[(size_t) i] < H[(size_t) i + 1]) { iValley = i; break; }
    if (iValley < 0)
    {
        // No dip after the noise peak: the knee where the noise tail falls to a tenth of its height.
        for (int i = iNoisePeak + 1; i <= iLast; ++i)
            if (H[(size_t) i] <= 0.1 * H[(size_t) iNoisePeak]) { iValley = i; break; }
    }
    if (iValley < 0) iValley = h10.bin (noise10 + 6.0);
    const float valleyDb = std::clamp ((float) h10.centre (iValley), (float) noise10 + 1.0f, (float) program10 - 6.0f);
    m.gateValleyDb = valleyDb;

    if (separation > kGateSeparationDb)
    {
        // Hold from the note length: the median run above the valley, half of it, 5 to 500 ms.
        std::vector<double> runsMs;
        int run = 0;
        for (size_t i = 0; i <= L10.size(); ++i)
        {
            if (i < L10.size() && L10[i] >= valleyDb) { ++run; continue; }
            if (run >= 5) runsMs.push_back (run * 10.0);
            run = 0;
        }
        add (P::gateOn, 1.0f, juce::String::formatted ("Gate on: noise %.0f dB, program %.0f dB, %.0f dB apart", noise10, program10, separation));
        add (P::gateThresh, roundTo (valleyDb, 0.1f), juce::String::formatted ("Gate threshold %.1f dB at the valley between noise and program", valleyDb));
        if (! runsMs.empty())
        {
            const float hold = std::clamp (roundTo ((float) (0.5 * median (runsMs)), 1.0f), 5.0f, 500.0f);
            add (P::gateHold, hold, juce::String::formatted ("Gate hold %.0f ms: half the median note length", hold));
        }
    }
    else if (cur (P::gateOn) >= 0.5f)
    {
        add (P::gateOn, 0.0f, juce::String::formatted ("Gate off: noise and program only %.0f dB apart (needs %.0f)", separation, kGateSeparationDb));
    }

    //----------------------------------------------------------------------------
    // Low end: YIN on a 12 kHz decimated copy (box average), voiced and stable frames only.
    const int D = std::max (1, (int) std::floor (fs / 12000.0));
    const double fsP = fs / D;
    const int nP = n / D;
    std::vector<float> xp ((size_t) nP, 0.0f);
    for (int i = 0; i < nP; ++i)
    {
        double s = 0.0;
        for (int j = 0; j < D; ++j) s += x[i * D + j];
        xp[(size_t) i] = (float) (s / D);
    }
    const int frameLenOk = std::min (2 * (int) std::floor (fsP / 40.0), nP); // two lags of the lowest pitch
    const int hopP = std::max (1, (int) std::lround (0.02 * fsP));
    std::vector<double> stableF0;
    {
        std::vector<float> f0s;
        for (int s = 0; s + frameLenOk <= nP; s += hopP)
        {
            const float level = rmsDbOf (&xp[(size_t) s], frameLenOk);
            float ap = 1.0f;
            const float f0 = level >= m.noiseDb + 10.0f ? yinPitch (&xp[(size_t) s], frameLenOk, fsP, &ap) : 0.0f;
            f0s.push_back (f0);
        }
        for (size_t i = 0; i < f0s.size(); ++i)
        {
            if (f0s[i] <= 0.0f) continue;
            bool stable = false;
            for (int k = -1; k <= 1; k += 2)
            {
                const long long j = (long long) i + k;
                if (j < 0 || j >= (long long) f0s.size() || f0s[(size_t) j] <= 0.0f) continue;
                if (std::abs (f0s[(size_t) j] - f0s[i]) <= kStableRatio * f0s[i]) stable = true;
            }
            if (stable) stableF0.push_back (f0s[i]);
        }
    }
    if (stableF0.size() >= 5)
    {
        std::sort (stableF0.begin(), stableF0.end());
        const double f0Low = stableF0[(size_t) std::floor (0.05 * (stableF0.size() - 1))];
        m.f0Hz = (float) f0Low;
        const float hp = std::clamp (roundTo ((float) (0.7 * f0Low), 0.1f), 20.0f, 150.0f);
        add (P::hpOn, 1.0f, juce::String::formatted ("High-pass on: lowest stable pitch %.1f Hz", f0Low));
        add (P::hpFreq, hp, juce::String::formatted ("High-pass %.1f Hz: 0.7 x %.1f Hz", hp, f0Low));
    }

    //----------------------------------------------------------------------------
    // Activity: windows well above the noise floor.
    int activeCount = 0;
    for (float v : L10) activeCount += v >= m.noiseDb + 12.0f ? 1 : 0;
    m.activeFraction = L10.empty() ? 0.0f : (float) activeCount / (float) L10.size();

    //----------------------------------------------------------------------------
    // Sibilance: 1024-point frames every 10 ms, 4 to 10 kHz against the full band.
    const bool sibilanceSource = source == Source::Vocal || source == Source::Dialogue || source == Source::Guitar
                                 || source == Source::Keys || source == Source::Bus;
    if (sibilanceSource && n >= 1024)
    {
        const int N = 1024;
        const Fft fft (N);
        const auto win = hann (N);
        const double sw2 = sumSquares (win);
        const int kLo = (int) std::ceil (4000.0 * N / fs), kHi = std::min (N / 2, (int) std::floor (10000.0 * N / fs));
        const int kFullLo = std::max (1, (int) std::ceil (20.0 * N / fs)), kFullHi = N / 2 - 1;
        std::vector<cd> buf;
        std::vector<double> ps;
        struct Frame { int index; double bandDb, ratioDb; };
        std::vector<Frame> frames;
        const int frameHop = win10;
        for (int f = 0; f * frameHop + N <= n; ++f)
        {
            if (f >= (int) L10.size()) break;
            if (L10[(size_t) f] < m.noiseDb + 12.0f) continue;
            powerSpectrum (x + f * frameHop, N, fft, win, buf, ps);
            double eb = 0.0, ef = 0.0;
            for (int k = kLo; k <= kHi; ++k) eb += ps[(size_t) k];
            for (int k = kFullLo; k <= kFullHi; ++k) ef += ps[(size_t) k];
            const double scale = 2.0 / (N * sw2);
            const double bandDb = 10.0 * std::log10 (std::max (eb * scale, 1.0e-12));
            const double fullDb = 10.0 * std::log10 (std::max (ef * scale, 1.0e-12));
            frames.push_back ({ f, bandDb, bandDb - fullDb });
        }
        if (frames.size() >= 20)
        {
            // The top 5 % of events by band level are the sibilant ones (ties broken by time).
            std::vector<Frame> sorted = frames;
            std::stable_sort (sorted.begin(), sorted.end(), [] (const Frame& a, const Frame& b) { return a.bandDb > b.bandDb; });
            const size_t topCount = std::max<size_t> (3, (size_t) std::ceil (0.05 * sorted.size()));
            sorted.resize (std::min (topCount, sorted.size()));
            std::vector<double> ratios, bands;
            for (const auto& fr : sorted) { ratios.push_back (fr.ratioDb); bands.push_back (fr.bandDb); }
            const double ratioMed = median (ratios), bandMed = median (bands);

            // Peak frequency of the sibilant band, from the summed spectra of those frames.
            std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
            for (const auto& fr : sorted)
            {
                powerSpectrum (x + fr.index * frameHop, N, fft, win, buf, ps);
                for (size_t k = 0; k < acc.size(); ++k) acc[k] += ps[k];
            }
            int kPeak = kLo;
            double peakVal = -1.0;
            for (int k = kLo + 1; k < kHi - 1; ++k)
            {
                const double v = acc[(size_t) k - 1] + acc[(size_t) k] + acc[(size_t) k + 1];
                if (v > peakVal) { peakVal = v; kPeak = k; }
            }
            const float peakHz = roundTo ((float) (kPeak * fs / N), 10.0f);
            m.sibilantPeakHz = peakHz;
            m.sibilantRatioDb = (float) ratioMed;

            // De-esser: gain reduction = 0.8 x (detector - threshold) above the knee.
            const bool relative = cur (P::dsRelative) >= 0.5f;
            const double detector = (relative ? ratioMed : bandMed) + kDetectorPeakDb;
            const float over = kSibilantGrDb / kDeEssSlope;
            const float thresh = std::clamp (roundTo ((float) (detector - over), 0.1f), -48.0f, 0.0f);
            add (P::dsOn, 1.0f, "De-Ess on: sibilance found in the top 5 % of voiced events");
            add (P::ds1On, 1.0f, "De-Ess band 1 on");
            add (P::ds1Type, 1.0f, "De-Ess band 1 as a bell");
            add (P::ds1Freq, peakHz, juce::String::formatted ("De-Ess band 1 at %.1f kHz: sibilant peak", peakHz / 1000.0f));
            add (P::ds1Thresh, thresh, juce::String::formatted ("De-Ess threshold %.1f dB: about %.1f dB GR on the sibilant events", thresh, kSibilantGrDb));
        }
    }

    //----------------------------------------------------------------------------
    // Tone: long-term spectrum of the active program (4096 points) in 1/3 octave bands, against the target.
    if (n >= 4096)
    {
        const int size = 4096;
        const int hopT = size / 2;
        const Fft fft (size);
        const auto win = hann (size);
        const double sw2 = sumSquares (win);
        std::vector<cd> buf;
        std::vector<double> ps, avg ((size_t) size / 2 + 1, 0.0);
        int used = 0;
        for (int s = 0; s + size <= n; s += hopT)
        {
            if (rmsDbOf (x + s, size) < m.noiseDb + 12.0f) continue;
            powerSpectrum (x + s, size, fft, win, buf, ps);
            for (size_t k = 0; k < avg.size(); ++k) avg[k] += ps[k];
            ++used;
        }
        if (used >= 3)
        {
            const auto bandDb = [&] (double fc)
            {
                const double lo = fc / std::pow (2.0, 1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
                double s = 0.0;
                int cnt = 0;
                for (int k = 1; k < (int) avg.size(); ++k)
                {
                    const double f = k * fs / size;
                    if (f < lo || f > hi) continue;
                    s += avg[(size_t) k];
                    ++cnt;
                }
                return cnt > 0 ? 10.0 * std::log10 (std::max (s / cnt, 1.0e-20)) : -200.0;
            };
            const double ref = bandDb (1000.0);
            const ToneTarget target = toneTarget (source);
            const float tgt[4] { target.low, target.mid, target.tone, target.high };
            float dev[4];
            for (int b = 0; b < 4; ++b) dev[b] = (float) (bandDb (kEqBandCentre[b]) - ref) - tgt[b];

            int order[4] { 0, 1, 2, 3 };
            std::stable_sort (order, order + 4, [&] (int a, int b) { return std::abs (dev[a]) > std::abs (dev[b]); });
            bool anyMove = false;
            for (int i = 0; i < 2; ++i)
            {
                const int b = order[i];
                if (std::abs (dev[b]) < kEqMinDeviationDb) break;
                const float gain = std::clamp (roundTo (-dev[b], 0.1f), -kEqMoveMaxDb, kEqMoveMaxDb);
                if (std::abs (gain) < 0.5f) continue;
                if (! anyMove) add (P::eqOn, 1.0f, "EQ on");
                anyMove = true;
                static const char* bandNames[4] { "Low", "Mid", "Tone", "High" };
                add (eqBandOn (b), 1.0f, juce::String (bandNames[b]) + " band on");
                add (eqBandGain (b), gain,
                     juce::String (bandNames[b]) + juce::String::formatted (" %+.1f dB: %.1f dB against the ", gain, dev[b])
                         + juce::String (sourceName (source)) + juce::String::formatted (" target at %.0f Hz", (double) kEqBandCentre[b]));
            }
        }
    }

    //----------------------------------------------------------------------------
    // Dynamics: 100 ms windows above the noise floor; the threshold gives about 3.5 dB median GR.
    {
        Segments seg;
        for (int s = 0, i = 0; s + win100 <= n; s += win100, ++i)
        {
            if (L100[(size_t) i] < m.noiseDb + 15.0f) continue;
            float peak = 0.0f;
            for (int j = 0; j < win100; ++j) peak = std::max (peak, std::abs (x[s + j]));
            seg.levelDb.push_back (L100[(size_t) i]);
            seg.crestDb.push_back ((float) juce::Decibels::gainToDecibels (peak, -120.0f) - L100[(size_t) i]);
        }
        if (seg.levelDb.size() >= 3)
        {
            std::vector<double> lv (seg.levelDb.begin(), seg.levelDb.end()), cf (seg.crestDb.begin(), seg.crestDb.end());
            const double levelMed = median (lv);
            const double crest = median (cf);
            m.crestDb = (float) crest;
            const float ratio = crest >= 16.0 ? 4.0f : (crest <= 10.0 ? 2.0f : 3.0f);
            const float thresh = std::clamp (roundTo ((float) (levelMed - kCompGrTargetDb / (1.0f - 1.0f / ratio)), 0.1f), -50.0f, 0.0f);
            m.compThreshDb = thresh;
            add (P::compOn, 1.0f, "Compressor on");
            static const char* compNames[] { "VCA", "FET", "OPT", "SOFT" };
            add (P::compType, (float) compTypeDefault (source),
                 juce::String ("Comp type ") + juce::String (compNames[compTypeDefault (source)]) + " for " + juce::String (sourceName (source)));
            add (P::compRatio, ratio, juce::String::formatted ("Ratio %.0f:1 for a crest factor of %.1f dB", ratio, crest));
            add (P::compThresh, thresh, juce::String::formatted ("Compressor threshold %.1f dB: about %.1f dB median GR", thresh, kCompGrTargetDb));
        }
    }

    //----------------------------------------------------------------------------
    r.summary = juce::String (sourceName (source)) + juce::String::formatted (": %d change", (int) r.changes.size())
                + (r.changes.size() == 1 ? juce::String() : juce::String ("s"))
                + juce::String::formatted (". Noise %.0f dB, program %.0f dB.", m.noiseDb, m.programDb);
    return r;
}
} // namespace nl::smart

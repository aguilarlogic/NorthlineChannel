#pragma once

#include "../Params.h"
#include <array>
#include <vector>

namespace nl::smart
{
// What the captured signal is. The source picks the target tone curve, the default compressor
// type and whether the de-esser is considered.
enum class Source : int { Vocal = 0, Kick, Snare, Toms, Bass, Guitar, Keys, Bus, Dialogue, count };
constexpr int kNumSources = (int) Source::count;
const char* sourceName (Source s) noexcept;
Source sourceFromIndex (int index) noexcept;

// Capture length. Smart Start listens for this long (roadmap 9.1: 8 to 15 s).
constexpr double kCaptureSeconds = 12.0;

// One parameter change with the line the summary shows for it.
struct Change
{
    int param = 0;      // P::Index
    float from = 0.0f;  // plain value before the analysis
    float to = 0.0f;    // plain value proposed by the analysis
    juce::String reason;
};

// Numbers the analysis measured, for the summary and for the tests.
struct Measurements
{
    float noiseDb = -120.0f;        // 10th percentile of short-term level
    float programDb = -120.0f;      // 90th percentile of short-term level (the Operating Level reference)
    float crestDb = 0.0f;           // median crest factor of the active segments
    float f0Hz = 0.0f;              // lowest stable pitch, 0 when none was found
    float gateValleyDb = -120.0f;   // gate threshold candidate
    float gateSeparationDb = 0.0f;  // program minus noise level in the gate histogram
    float sibilantPeakHz = 0.0f;    // centre of the sibilant band, 0 when not applicable
    float sibilantRatioDb = 0.0f;   // median band-to-full level of the sibilant events
    float compThreshDb = 0.0f;      // compressor threshold candidate
    float activeFraction = 0.0f;    // share of the capture above the activity gate
};

struct Result
{
    bool valid = false;             // false when the capture held no usable signal
    Source source = Source::Vocal;
    std::vector<Change> changes;    // in application order, no-ops removed
    Measurements m;
    juce::String summary;           // one or two lines for the panel
};

// Pure analysis of a mono capture (the strip input, L and R averaged). Deterministic: the same
// samples, sample rate, source and current parameter values give the same result. `current` holds
// the plain value of every parameter, so that unchanged values are not reported.
// Runs on a background thread; it allocates, which the audio thread never does.
Result analyze (const float* x, int n, double sampleRate, Source source, const std::array<float, P::count>& current);

// Plain values of every parameter at their defaults (for tests and the Init state).
std::array<float, P::count> defaultValues();

// Target tone curves: long-term level in dB at four points, relative to 1 kHz. Exposed for tests.
struct ToneTarget { float low, mid, tone, high; };
ToneTarget toneTarget (Source s) noexcept;

// Compressor type default per source (index into the Comp Type choice list).
int compTypeDefault (Source s) noexcept;

// YIN pitch estimate of one frame of `n` samples at the start of x. Returns the frequency in Hz,
// or 0 when the frame is aperiodic. aperiodicity receives the minimum of the cumulative mean
// normalised difference (0 = perfectly periodic).
float yinPitch (const float* x, int n, double sampleRate, float* aperiodicity = nullptr);
} // namespace nl::smart

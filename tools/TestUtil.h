#pragma once

#include "ToolCommon.h"
#include <vector>

// Shared helpers for the test suites and measurement suites.
//
// Adding tests: create tools/tests/Test<Area>.cpp (picked up by CMake automatically) and write
//
//     NL_TEST (myArea_whatItChecks)
//     {
//         auto p = tool::makeProc();
//         tool::check (condition, "description", "detail");
//     }
//
// `NorthlineTool test [filter]` runs every test whose name contains `filter`.

namespace tool
{
using namespace nl;

inline int failures = 0, checks = 0;

inline void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("  [%s] %s%s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.isEmpty() ? "" : ("  (" + detail + ")").toRawUTF8());
}

//==============================================================================
// Test registry.
struct TestCase
{
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& testRegistry()
{
    static std::vector<TestCase> r;
    return r;
}

struct TestRegistrar
{
    TestRegistrar (const char* name, void (*fn)()) { testRegistry().push_back ({ name, fn }); }
};

#define NL_TEST(name)                                                  \
    static void name();                                                \
    static const tool::TestRegistrar name##_registrar { #name, name }; \
    static void name()

//==============================================================================
inline std::unique_ptr<NorthlineProcessor> makeProc (double fs = 48000.0, int block = 256, bool sidechain = false)
{
    auto p = std::make_unique<NorthlineProcessor>();
    auto layout = p->getBusesLayout();
    if (sidechain && layout.inputBuses.size() > 1) layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    p->setBusesLayout (layout);
    p->setRateAndBufferSizeDetails (fs, block);
    p->prepareToPlay (fs, block);
    return p;
}

// Renders `in` through the processor in blocks; returns the output.
inline juce::AudioBuffer<float> render (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block, const juce::AudioBuffer<float>* sc = nullptr)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    const int totalCh = p.getTotalNumInputChannels();
    juce::AudioBuffer<float> work (juce::jmax (2, totalCh), block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        work.setSize (work.getNumChannels(), len, false, false, true);
        work.clear();
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, len);
        if (sc != nullptr && totalCh >= 4)
            for (int c = 0; c < 2; ++c) work.copyFrom (2 + c, 0, *sc, juce::jmin (c, sc->getNumChannels() - 1), pos, len);
        p.processBlock (work, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, work, c, 0, len);
    }
    return out;
}

// Parameter automation event for renderWithAutomation (plain value, applied before the
// block that contains `sample`; use sample positions that are multiples of 2048 when
// comparing block sizes).
struct AutomationEvent
{
    int sample, param;
    float value;
};

// Like render(), but applies automation events at block boundaries. Events must be sorted.
inline juce::AudioBuffer<float> renderWithAutomation (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                                      const std::vector<AutomationEvent>& events, const juce::AudioBuffer<float>* sc = nullptr)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    const int totalCh = p.getTotalNumInputChannels();
    juce::AudioBuffer<float> work (juce::jmax (2, totalCh), block);
    juce::MidiBuffer midi;
    size_t next = 0;
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        while (next < events.size() && events[next].sample < pos + len)
        {
            setParam (p, events[next].param, events[next].value);
            ++next;
        }
        work.setSize (work.getNumChannels(), len, false, false, true);
        work.clear();
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, len);
        if (sc != nullptr && totalCh >= 4)
            for (int c = 0; c < 2; ++c) work.copyFrom (2 + c, 0, *sc, juce::jmin (c, sc->getNumChannels() - 1), pos, len);
        p.processBlock (work, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, work, c, 0, len);
    }
    return out;
}

// Sample-exact variant of renderWithAutomation: host blocks are shortened so that every event
// is applied at exactly its sample position, whatever the block size (like a host that splits
// blocks at automation points). Use this to compare block sizes that do not divide the event
// spacing: renderWithAutomation applies an event up to block-1 samples early, which moves it
// across control ticks and makes bit-identical output impossible for any engine.
inline juce::AudioBuffer<float> renderWithAutomationExact (NorthlineProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                                           const std::vector<AutomationEvent>& events, const juce::AudioBuffer<float>* sc = nullptr)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    const int totalCh = p.getTotalNumInputChannels();
    juce::AudioBuffer<float> work (juce::jmax (2, totalCh), block);
    juce::MidiBuffer midi;
    size_t next = 0;
    int pos = 0;
    while (pos < n)
    {
        while (next < events.size() && events[next].sample <= pos)
        {
            setParam (p, events[next].param, events[next].value);
            ++next;
        }
        int len = juce::jmin (block, n - pos);
        if (next < events.size()) len = juce::jmin (len, events[next].sample - pos);
        work.setSize (work.getNumChannels(), len, false, false, true);
        work.clear();
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, len);
        if (sc != nullptr && totalCh >= 4)
            for (int c = 0; c < 2; ++c) work.copyFrom (2 + c, 0, *sc, juce::jmin (c, sc->getNumChannels() - 1), pos, len);
        p.processBlock (work, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, work, c, 0, len);
        pos += len;
    }
    return out;
}

inline juce::AudioBuffer<float> sine (double fs, double freq, float amp, int n, double phase = 0.0)
{
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * kPiD * freq * i / fs + phase);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

// Deterministic pink noise (Voss-McCartney style, -3 dB/oct), scaled to the given RMS in dBFS.
inline juce::AudioBuffer<float> pinkNoise (int n, float rmsDbfs, int seed = 1)
{
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (seed);
    double rows[16] {}, sum = 0.0, acc = 0.0;
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const int k = i == 0 ? 0 : juce::jmin (15, (int) juce::findHighestSetBit ((uint32_t) (i & -i)));
        sum -= rows[k];
        rows[k] = rng.nextDouble() * 2.0 - 1.0;
        sum += rows[k];
        v[(size_t) i] = (float) (sum + (rng.nextDouble() * 2.0 - 1.0));
        acc += (double) v[(size_t) i] * v[(size_t) i];
    }
    const double g = juce::Decibels::decibelsToGain ((double) rmsDbfs) / std::sqrt (acc / juce::jmax (1, n));
    for (int i = 0; i < n; ++i)
    {
        b.setSample (0, i, (float) (v[(size_t) i] * g));
        b.setSample (1, i, (float) (v[(size_t) i] * g));
    }
    return b;
}

inline float peakDb (const juce::AudioBuffer<float>& b, int from, int ch = 0)
{
    float m = 0.0f;
    for (int i = from; i < b.getNumSamples(); ++i) m = std::max (m, std::abs (b.getSample (ch, i)));
    return juce::Decibels::gainToDecibels (m, -200.0f);
}

inline float rmsDb (const juce::AudioBuffer<float>& b, int from, int ch = 0)
{
    double s = 0.0;
    for (int i = from; i < b.getNumSamples(); ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return (float) (10.0 * std::log10 (std::max (1.0e-30, s / (b.getNumSamples() - from))));
}

// RMS over [from, to).
inline float rmsDbRange (const juce::AudioBuffer<float>& b, int from, int to, int ch = 0)
{
    double s = 0.0;
    to = juce::jmin (to, b.getNumSamples());
    for (int i = from; i < to; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return (float) (10.0 * std::log10 (std::max (1.0e-30, s / juce::jmax (1, to - from))));
}

// Max absolute sample difference between two buffers over all channels, in dB.
inline float maxDiffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
{
    float m = 0.0f;
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    for (int c = 0; c < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++c)
        for (int i = from; i < n; ++i) m = std::max (m, std::abs (a.getSample (c, i) - b.getSample (c, i)));
    return juce::Decibels::gainToDecibels (m, -300.0f);
}

// Gain at one frequency from a steady-state sine.
inline float gainAt (NorthlineProcessor& p, double freq, float ampDb = -20.0f)
{
    const double fs = p.getSampleRate();
    const int n = (int) (fs * 1.0);
    p.getStrip().reset();
    auto in = sine (fs, freq, juce::Decibels::decibelsToGain (ampDb), n);
    auto out = render (p, in, 256);
    return rmsDb (out, n / 2) - rmsDb (in, n / 2);
}

inline void allModulesOff (NorthlineProcessor& p)
{
    for (int i : { (int) P::preOn, (int) P::gateOn, (int) P::dsOn, (int) P::eqOn, (int) P::compOn, (int) P::limOn }) setParam (p, i, 0);
}
} // namespace tool

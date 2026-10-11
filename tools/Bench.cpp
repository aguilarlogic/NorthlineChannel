#include "ToolCommon.h"
#include <algorithm>

using namespace nl;

namespace tool
{
namespace
{
struct Result
{
    double meanUs = 0, p99Us = 0, maxUs = 0, rtPercent = 0;
};

// Benchmarks any AudioProcessor the same way: stereo noise, fixed block size,
// wall-clock time per processBlock call. rtPercent = processing time / audio time.
Result benchProcessor (juce::AudioProcessor& p, double fs, int block, double seconds)
{
    const int numCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);

    // One second of band-limited, roughly pink noise (cheap 1-pole tilt), looped.
    const int srcLen = (int) fs;
    juce::AudioBuffer<float> src (2, srcLen);
    juce::Random rng (42);
    for (int c = 0; c < 2; ++c)
    {
        float lp = 0.0f;
        for (int i = 0; i < srcLen; ++i)
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            lp = 0.97f * lp + 0.03f * w;
            src.setSample (c, i, 0.25f * (0.6f * w + 3.0f * lp));
        }
    }

    juce::AudioBuffer<float> buf (numCh, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * fs / block);
    const int warm = (int) (0.5 * fs / block);
    std::vector<double> times;
    times.reserve ((size_t) blocks);
    const double tickUs = 1.0e6 / (double) juce::Time::getHighResolutionTicksPerSecond();
    int pos = 0;
    for (int b = 0; b < blocks + warm; ++b)
    {
        buf.clear();
        for (int c = 0; c < 2; ++c)
        {
            int copied = 0, rd = pos;
            while (copied < block)
            {
                const int len = juce::jmin (block - copied, srcLen - rd);
                buf.copyFrom (c, copied, src, c, rd, len);
                copied += len;
                rd = (rd + len) % srcLen;
            }
        }
        pos = (pos + block) % srcLen;
        const auto t0 = juce::Time::getHighResolutionTicks();
        p.processBlock (buf, midi);
        const auto t1 = juce::Time::getHighResolutionTicks();
        if (b >= warm) times.push_back ((double) (t1 - t0) * tickUs);
    }
    p.releaseResources();

    Result r;
    double sum = 0.0;
    for (double t : times) sum += t;
    r.meanUs = sum / (double) times.size();
    std::sort (times.begin(), times.end());
    r.p99Us = times[(size_t) (0.99 * (double) (times.size() - 1))];
    r.maxUs = times.back();
    r.rtPercent = 100.0 * sum / (1.0e6 * (double) times.size() * block / fs);
    return r;
}

void printRow (const char* config, double fs, int block, const Result& r)
{
    const double budgetUs = 1.0e6 * block / fs;
    std::printf ("  %-34s %6.0f Hz %5d   %8.2f us  %8.2f us  %7.3f %%  %7.1f\n",
                 config, fs, block, r.meanUs, r.p99Us, r.rtPercent, r.rtPercent > 0 ? 100.0 / r.rtPercent : 0.0);
    juce::ignoreUnused (budgetUs);
}

void printHeader()
{
    std::printf ("  %-34s %9s %5s   %11s  %11s  %9s  %7s\n", "configuration", "rate", "block", "mean/block", "p99/block", "RT CPU", "inst/core");
}
} // namespace

int runBench (const juce::StringArray& args)
{
    const bool quick = args.contains ("--quick");
    const double seconds = quick ? 3.0 : 10.0;

    if (args[0] == "host-bench")
    {
        const juce::File file (args[1]);
        const auto filter = args.size() > 2 && ! args[2].startsWith ("--") ? args[2] : juce::String();
        juce::AudioPluginFormatManager fm;
        juce::addDefaultFormatsToManager (fm);
        juce::OwnedArray<juce::PluginDescription> types;
        for (auto* f : fm.getFormats())
            if (f->getName() == "VST3") f->findAllTypesForFile (types, file.getFullPathName());
        if (types.isEmpty()) { std::printf ("No VST3 plug-ins found in %s\n", file.getFullPathName().toRawUTF8()); return 1; }

        const juce::PluginDescription* chosen = nullptr;
        for (auto* d : types)
        {
            std::printf ("  found: %s (%s)\n", d->name.toRawUTF8(), d->manufacturerName.toRawUTF8());
            if (chosen == nullptr && (filter.isEmpty() || d->name.containsIgnoreCase (filter))) chosen = d;
        }
        if (chosen == nullptr) { std::printf ("No plug-in matched '%s'\n", filter.toRawUTF8()); return 1; }

        std::printf ("\nBenchmarking %s (default state, stereo, %.0f s per run)\n", chosen->name.toRawUTF8(), seconds);
        printHeader();
        for (double fs : { 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 512 })
            {
                if (fs == 96000.0 && block != 64) continue;
                juce::String err;
                auto inst = fm.createPluginInstance (*chosen, fs, block, err);
                if (inst == nullptr) { std::printf ("  could not instantiate: %s\n", err.toRawUTF8()); return 1; }
                auto layout = inst->getBusesLayout();
                if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
                if (! layout.inputBuses.isEmpty()) layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
                inst->setBusesLayout (layout);
                const auto r = benchProcessor (*inst, fs, block, seconds);
                printRow (chosen->name.substring (0, 34).toRawUTF8(), fs, block, r);
                if (block == 64 && fs == 48000.0) std::printf ("    reported latency: %d samples\n", inst->getLatencySamples());
            }
        return 0;
    }

    std::printf ("Northline Channel CPU benchmark (in-process, stereo, %.0f s per run)\n", seconds);
    std::printf ("RT CPU = processing time / audio duration on one core. inst/core = how many instances one core could run.\n\n");
    printHeader();

    struct Config { const char* name; std::function<void (NorthlineProcessor&)> setup; };
    const std::vector<Config> configs {
        { "default (transparent)", [] (NorthlineProcessor&) {} },
        { "preset: Vocal - Upfront", [] (NorthlineProcessor& p) { p.loadFactoryPreset (1); } },
        { "everything on (Eco, zero latency)", [] (NorthlineProcessor& p) { configureEverything (p, false, false); } },
        { "everything on (HQ sat + lookahead)", [] (NorthlineProcessor& p) { configureEverything (p, true, true); } },
    };

    for (auto& cfg : configs)
    {
        for (double fs : { 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 512 })
            {
                if (fs == 96000.0 && block != 64) continue;
                NorthlineProcessor p;
                cfg.setup (p);
                const auto r = benchProcessor (p, fs, block, seconds);
                printRow (cfg.name, fs, block, r);

                if (fs == 48000.0 && block == 64 && cfg.name != configs.front().name)
                {
                    // Per-module breakdown from the plug-in's own CPU readout.
                    std::vector<MeterFrame> frames (256);
                    double sums[6] {};
                    int count = 0;
                    for (;;)
                    {
                        const size_t got = p.getStrip().meterFrames().pop (frames.data(), frames.size());
                        for (size_t i = 0; i < got; ++i) { for (int m = 0; m < 6; ++m) sums[m] += frames[i].moduleUs[m]; ++count; }
                        if (got < frames.size()) break;
                    }
                    if (count > 0)
                        std::printf ("    per-module (us/block, last frames): PRE %.2f  GATE %.2f  DS %.2f  EQ %.2f  COMP %.2f  MASTER %.2f\n",
                                     sums[0] / count, sums[1] / count, sums[2] / count, sums[3] / count, sums[4] / count, sums[5] / count);
                }
            }
    }
    return 0;
}
} // namespace tool

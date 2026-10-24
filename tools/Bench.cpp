#include "ToolCommon.h"
#include "BenchTimer.h"
#include <algorithm>

using namespace nl;

namespace tool
{
namespace
{
// One configuration at one rate and block size, summarised over the repeats.
struct Summary
{
    double cpuUsMed = 0, cpuUsMin = 0, cpuPctMin = 0, cpuPctMed = 0, wallPctMed = 0, wallP99Us = 0;
};

// One timed run of a processor: stereo program material, fixed block size, prepared at the rate.
// With uiOpen, the harness reads the strip's meters and scopes at 30 Hz, as the editor does, so the
// plug-in does its metering work; those reads are harness work and are removed like it. collect, if
// given, receives the meter frames that were read.
TimedRun runOnce (juce::AudioProcessor& p, double fs, int block, double seconds, bool uiOpen, std::vector<MeterFrame>* collect)
{
    auto* np = dynamic_cast<NorthlineProcessor*> (&p);
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
    const int warm = (int) (0.25 * fs / block);
    const int uiPeriod = std::max (1, (int) (fs / 30.0 / block));
    std::vector<MeterFrame> frameBuf (256);
    std::vector<ScopePoint> scopeBuf (256);
    int pos = 0;

    // The harness refills buf with the next block of program material (and reads the UI state when the
    // editor is open). It is timed separately and removed from the plug-in's time.
    const auto harness = [&] (int index)
    {
        if (uiOpen && np != nullptr && index % uiPeriod == 0)
        {
            auto& strip = np->getStrip();
            for (;;)
            {
                const size_t got = strip.meterFrames().pop (frameBuf.data(), frameBuf.size());
                if (collect != nullptr) collect->insert (collect->end(), frameBuf.begin(), frameBuf.begin() + (std::ptrdiff_t) got);
                if (got < frameBuf.size()) break;
            }
            for (int m = 0; m < numModules; ++m)
                while (strip.scope (m).pop (scopeBuf.data(), scopeBuf.size()) == scopeBuf.size()) {}
            while (strip.deEsserBand2Scope().pop (scopeBuf.data(), scopeBuf.size()) == scopeBuf.size()) {}
        }
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
    };
    juce::MidiBuffer* midiPtr = &midi;
    const auto body = [&] (int) { p.processBlock (buf, *midiPtr); };

    const auto r = timeBlocks (warm, blocks, (double) block / fs, body, harness);
    p.releaseResources();
    return r;
}

Summary summarise (const std::vector<TimedRun>& runs, double fs, int block)
{
    const double audioUs = 1.0e6 * block / fs;
    std::vector<double> cpu, pct, wallPct, p99;
    for (const auto& r : runs)
    {
        cpu.push_back (r.cpuUsPerBlock);
        pct.push_back (100.0 * r.cpuUsPerBlock / audioUs);
        wallPct.push_back (100.0 * r.wallUsPerBlock / audioUs);
        p99.push_back (r.wallP99Us);
    }
    Summary s;
    s.cpuUsMed = medianOf (cpu);
    s.cpuUsMin = minOf (cpu);
    s.cpuPctMin = minOf (pct);
    s.cpuPctMed = medianOf (pct);
    s.wallPctMed = medianOf (wallPct);
    s.wallP99Us = medianOf (p99);
    return s;
}

// Runs `repeats` timed runs of an already-built processor.
Summary benchProcessor (juce::AudioProcessor& p, double fs, int block, double seconds, int repeats,
                       bool uiOpen = false, std::vector<MeterFrame>* collect = nullptr)
{
    std::vector<TimedRun> runs;
    for (int k = 0; k < repeats; ++k) runs.push_back (runOnce (p, fs, block, seconds, uiOpen, collect));
    return summarise (runs, fs, block);
}

void printRow (const char* config, double fs, int block, const Summary& s)
{
    std::printf ("  %-34s %6.0f Hz %5d   %8.2f us  %8.2f us  %7.3f %%  %7.3f %%  %7.3f %%  %8.2f us  %7.1f\n",
                 config, fs, block, s.cpuUsMed, s.cpuUsMin, s.cpuPctMin, s.cpuPctMed, s.wallPctMed, s.wallP99Us,
                 s.cpuPctMed > 0 ? 100.0 / s.cpuPctMed : 0.0);
}

void printHeader()
{
    std::printf ("  %-34s %9s %5s   %11s  %11s  %9s  %9s  %9s  %11s  %7s\n",
                 "configuration", "rate", "block", "CPU us med", "CPU us min", "CPU min", "CPU med", "wall med", "wall p99", "inst/core");
}

void printModules (const std::vector<MeterFrame>& frames)
{
    if (frames.empty()) return;
    double sums[6] {};
    for (const auto& f : frames)
        for (int m = 0; m < 6; ++m) sums[m] += f.moduleUs[m];
    const double k = (double) frames.size();
    std::printf ("    per-module (us/block, wall inside the plug-in): PRE %.2f  GATE %.2f  DS %.2f  EQ %.2f  COMP %.2f  MASTER %.2f\n",
                 sums[0] / k, sums[1] / k, sums[2] / k, sums[3] / k, sums[4] / k, sums[5] / k);
}
} // namespace

int runBench (const juce::StringArray& args)
{
    const bool quick = args.contains ("--quick");
    const double seconds = quick ? 1.0 : 2.0;
    const int repeats = quick ? 3 : 5;

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

        std::printf ("\nBenchmarking %s (default state, stereo, %d runs of %.1f s)\n", chosen->name.toRawUTF8(), repeats, seconds);
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
                const auto s = benchProcessor (*inst, fs, block, seconds, repeats);
                printRow (chosen->name.substring (0, 34).toRawUTF8(), fs, block, s);
                if (block == 64 && fs == 48000.0) std::printf ("    reported latency: %d samples\n", inst->getLatencySamples());
            }
        return 0;
    }

    std::printf ("Northline Channel CPU benchmark (in-process, stereo, %d runs of %.1f s per row)\n", repeats, seconds);
    if (CpuClock::isThreadTime)
        std::printf ("CPU = thread cycle time (QueryThreadCycleTime) at %.3f GHz, so other processes do not count. wall = wall clock.\n",
                     CpuClock::calibrate() * 1.0e-9);
    else
        std::printf ("CPU = wall clock on this platform (no thread CPU clock in the tool).\n");
    std::printf ("CPU %% = CPU time / audio time on one core. min and median are over the runs. inst/core = instances one core could run (median).\n\n");

    struct Config { const char* name; std::function<void (NorthlineProcessor&)> setup; };
    const std::vector<Config> configs {
        { "default (transparent)", [] (NorthlineProcessor&) {} },
        { "preset: Vocal - Upfront", [] (NorthlineProcessor& p) { p.loadFactoryPreset (1); } },
        { "everything on (Eco, zero latency)", [] (NorthlineProcessor& p) { configureEverything (p, false, false); } },
        { "everything on (HQ sat + lookahead)", [] (NorthlineProcessor& p) { configureEverything (p, true, true); } },
    };

    std::printf ("Editor closed (nothing reads the meters or scopes):\n");
    printHeader();
    for (auto& cfg : configs)
        for (double fs : { 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 512 })
            {
                if (fs == 96000.0 && block != 64) continue;
                NorthlineProcessor p;
                cfg.setup (p);
                printRow (cfg.name, fs, block, benchProcessor (p, fs, block, seconds, repeats));
                if (block == 64 && fs == 48000.0) std::printf ("    reported latency: %d samples\n", p.getLatencySamples());
            }

    std::printf ("\nEditor open (meters and scopes read at 30 Hz), 48 kHz, 64 samples:\n");
    printHeader();
    for (auto& cfg : configs)
    {
        NorthlineProcessor p;
        cfg.setup (p);
        std::vector<MeterFrame> frames;
        printRow (cfg.name, 48000.0, 64, benchProcessor (p, 48000.0, 64, seconds, repeats, true, &frames));
        printModules (frames);
    }
    return 0;
}
} // namespace tool

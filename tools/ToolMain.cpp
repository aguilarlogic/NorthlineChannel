#include "ToolCommon.h"
#include "gui/PluginEditor.h"
#include <cstdlib>
#include <new>
#include <thread>

namespace tool
{
std::atomic<bool> countAllocations { false };
std::atomic<long long> allocationCount { 0 };
std::thread::id countingThread;
} // namespace tool

// Count heap allocations made by the counting thread while the flag is set, so tests can prove
// the audio path never allocates (other threads, e.g. JUCE's timer, are ignored).
void* operator new (std::size_t size)
{
    if (tool::countAllocations.load (std::memory_order_relaxed) && std::this_thread::get_id() == tool::countingThread)
        tool::allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace tool
{
int runValidate (const juce::StringArray& args); // Validate.cpp

// Renders the editor off-screen after feeding it ~2 s of program material, so the
// GUI can be checked (construction, layout, meters) without a DAW.
static int snapshot (const juce::File& file, int preset, const juce::String& overlay)
{
    nl::NorthlineProcessor p;
    const double fs = 48000.0;
    const int block = 256;
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
    if (preset >= 0) p.loadFactoryPreset (preset);
    else configureEverything (p, true, false);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    if (auto* ne = dynamic_cast<nl::NorthlineEditor*> (editor.get()))
    {
        if (overlay == "browser") ne->openPresetBrowser();
        else if (overlay == "smart") ne->openSmartStart();
        else if (overlay == "hub") ne->openHub();
        else if (overlay == "settings") ne->openSettings();
    }
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (11);
    double phase = 0.0;
    int n = 0;
    for (int step = 0; step < 70; ++step)
    {
        for (int b = 0; b < 6; ++b, n += block)
        {
            // Voice-like test signal: 180 Hz harmonic tone with a syllable envelope plus hiss bursts.
            for (int i = 0; i < block; ++i)
            {
                const double t = (n + i) / fs;
                const double env = 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 3.0 * t);
                double v = 0.0;
                for (int h = 1; h <= 8; ++h) v += std::sin (phase * h) / h;
                phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / fs;
                const double hiss = (std::fmod (t, 0.6) > 0.5) ? (rng.nextFloat() * 2.0f - 1.0f) * 0.25 : 0.0;
                const float x = (float) (0.22 * env * v + hiss);
                buf.setSample (0, i, x);
                buf.setSample (1, i, x * 0.92f);
            }
            p.processBlock (buf, midi);
        }
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep (34);
    }

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (img, out);
    std::printf ("%s %s (%d x %d)\n", ok ?"wrote" : "FAILED to write", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    p.releaseResources();
    return ok ? 0 : 1;
}
} // namespace tool

int main (int argc, char* argv[])
{
    tool::countingThread = std::this_thread::get_id();
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));

    const auto cmd = args.isEmpty() ? juce::String ("test") : args[0];
    if (cmd == "test") return tool::runTests (args);
    if (cmd == "measure") return tool::runMeasure (args);
    if (cmd == "bench" || cmd == "host-bench") return tool::runBench (args);
    if (cmd == "validate") return tool::runValidate (args);
    if (cmd == "snapshot" && args.size() > 1)
        return tool::snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), args.size() > 2 ? args[2].getIntValue() : 1, args[3]);

    std::printf ("NorthlineTool\n"
                 "  test [filter]                run DSP / realtime-safety / state tests (names containing filter)\n"
                 "  measure <suite|all|list> [--out dir] [--quick]\n"
                 "                               run measurement suites and write Markdown reports\n"
                 "  bench [--quick]              CPU benchmark of Northline Channel (in-process)\n"
                 "  host-bench <file.vst3> [name-filter] [--quick]\n"
                 "                               load any VST3 (e.g. a competitor strip) and benchmark it the same way\n"
                 "  validate [file.vst3] [--quick] [--seed N]\n"
                 "                               pluginval-style checks (editor, rates x blocks x layouts, fuzz, silence,\n"
                 "                               state, latency, bypass, threads, audio-thread allocations) on the\n"
                 "                               in-process processor, or on the given VST3 through JUCE hosting\n"
                 "  snapshot <out.png> [preset] [browser|smart|hub|settings]\n"
                 "                               render the editor to a PNG (factory preset index, -1 = everything on),\n"
                 "                               optionally with an overlay open\n");
    return 1;
}

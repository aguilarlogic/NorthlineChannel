#include "ToolCommon.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

// `NorthlineTool validate [file.vst3] [--quick] [--seed N]`: an in-process validator in the spirit of
// pluginval at strictness 10 (roadmap 11.2, 14).
//
// Without a path it drives NorthlineProcessor directly, so the allocation counter sees all of the
// plug-in code. With a path it loads that VST3 through JUCE's plugin hosting (the same route
// `host-bench` uses); the allocation counter then only sees the host side, because the plug-in DLL
// has its own heap, and the thread storm can only use the standard plug-in API (no presets, undo,
// A/B or Smart Start).
//
// A check listed in kKnownFails reports KNOWN instead of FAIL while its bug is open (the reproducer
// lives in tools/tests/TestValidate.cpp). An XPASS means the fix has landed and the entry can go.

using namespace nl;

namespace tool::validate
{
namespace
{
constexpr std::size_t kParams = (std::size_t) P::count;

//==============================================================================
// Bugs in files this tool does not own. Each one is reproduced by an NL_KNOWN_FAIL test.
struct KnownFail
{
    const char* check;
    const char* reason;
};

constexpr KnownFail kKnownFails[] = {
    { "bypass.delayed",
      "PluginProcessor.cpp: NorthlineProcessor does not override processBlockBypassed, so the JUCE default "
      "passes the input through undelayed. A host bypass (the VST3 client calls it when the host bypasses a "
      "plug-in without a bypass parameter) then misaligns by the reported latency. Fix: override "
      "processBlockBypassed to delay the input by getLatencySamples() through a delay line sized in prepareToPlay." },
    { "compare.click_free",
      "ChannelStrip.cpp: switching Compare from one module (PRE, GATE, DE-ESS or EQ) straight to module COMP gives a "
      "single-sample step of about 0.05 at the block where the switch lands (steady 0.002); other transitions are "
      "smooth. Cause not isolated: the transition needs two module fades to cross, so look at the module loop in "
      "ChannelStrip::process (moduleFade, the silent-module path) and at the COMP path. Reproducer: "
      "NL_KNOWN_FAIL validate_KNOWN_FAIL_compare_click in tools/tests/TestValidate.cpp." },
};

//==============================================================================
struct Row
{
    juce::String name;
    int checks = 0, pass = 0, fail = 0, known = 0, skipped = 0;
    double ms = 0.0;
    long long allocs = 0;
};

struct Ctx
{
    bool quick = false;
    juce::uint32 seed = 1;
    bool hosted = false;
    juce::AudioPluginFormatManager fm;
    juce::PluginDescription desc;
    std::thread::id mainThread;
    juce::String pluginName;

    juce::String section;
    Row cur;
    long long allocStart = 0;
    double startMs = 0.0;

    int failures = 0, known = 0;
    std::vector<Row> rows;
};

// Audio-thread allocations counted across the whole run (the counting operator new is in ToolMain.cpp).
std::atomic<long long> gAudioAllocs { 0 };

template <typename... Args>
juce::String fmt (const char* format, Args... args)
{
    char buf[1024];
    std::snprintf (buf, sizeof buf, format, args...);
    return juce::String (buf);
}

//==============================================================================
// Runs one processBlock (or processBlockBypassed) call with the allocation counter armed.
void audioCall (juce::AudioProcessor& ap, juce::AudioBuffer<float>& buf, bool bypassed = false)
{
    thread_local juce::MidiBuffer midi;
    const long long before = allocationCount.load (std::memory_order_relaxed);
    countAllocations.store (true, std::memory_order_relaxed);
    if (bypassed) ap.processBlockBypassed (buf, midi);
    else          ap.processBlock (buf, midi);
    countAllocations.store (false, std::memory_order_relaxed);
    gAudioAllocs.fetch_add (allocationCount.load (std::memory_order_relaxed) - before, std::memory_order_relaxed);
}

//==============================================================================
// `lines` are reproducers for the failing cases (seed, rate, block, layout, ...). All are printed (the
// latency and bypass sweeps have at most 256 combinations).
void record (Ctx& c, const juce::String& name, bool ok, const juce::String& detail, const std::vector<juce::String>& lines = {})
{
    const KnownFail* kf = nullptr;
    for (const auto& k : kKnownFails)
        if (name == k.check) kf = &k;

    const char* tag = "PASS";
    if (kf == nullptr)
    {
        if (ok) ++c.cur.pass;
        else    { tag = "FAIL"; ++c.cur.fail; ++c.failures; }
    }
    else if (ok)
    {
        tag = "XPASS";
        ++c.cur.pass;
    }
    else
    {
        tag = "KNOWN";
        ++c.cur.known;
        ++c.known;
    }
    ++c.cur.checks;
    std::printf ("  [%-5s] %-26s %s\n", tag, name.toRawUTF8(), detail.toRawUTF8());
    if (kf != nullptr && ! ok) std::printf ("           known: %s\n", kf->reason);
    if (kf != nullptr && ok) std::printf ("           XPASS: remove \"%s\" from kKnownFails in tools/Validate.cpp\n", kf->check);
    if (! ok)
        for (int i = 0; i < (int) lines.size(); ++i)
            std::printf ("           repro: %s\n", lines[(std::size_t) i].toRawUTF8());
}

// A check that cannot run here (named in the detail). It is reported, not counted as a pass or a failure.
void skip (Ctx& c, const char* name, const juce::String& reason)
{
    ++c.cur.skipped;
    std::printf ("  [SKIP ] %-26s %s\n", name, reason.toRawUTF8());
}

void beginSection (Ctx& c, const char* name)
{
    c.section = name;
    c.cur = Row();
    c.cur.name = name;
    c.allocStart = gAudioAllocs.load();
    c.startMs = juce::Time::getMillisecondCounterHiRes();
    std::printf ("\n== %s\n", name);
}

void endSection (Ctx& c)
{
    c.cur.allocs = gAudioAllocs.load() - c.allocStart;
    c.cur.ms = juce::Time::getMillisecondCounterHiRes() - c.startMs;
    record (c, c.section + ".allocs", c.cur.allocs == 0,
            fmt ("%lld audio-thread allocation(s)%s", c.cur.allocs,
                 c.hosted ? " (host side only; plug-in DLL not visible)" : ""));
    c.rows.push_back (c.cur);
}

//==============================================================================
// A system under test: the in-process processor, or a VST3 instance hosted through JUCE.
struct Dut
{
    std::unique_ptr<juce::AudioProcessor> owned;
    juce::AudioProcessor* ap = nullptr;
    NorthlineProcessor* nl = nullptr; // in-process only
    std::array<juce::AudioProcessorParameter*, kParams> prm {};
};

// Maps P indices to parameter objects. Hosted parameters are matched by display name.
bool mapParams (Dut& d)
{
    if (d.nl != nullptr)
    {
        for (int i = 0; i < (int) P::count; ++i) d.prm[(std::size_t) i] = d.nl->param (i);
        return true;
    }
    bool ok = true;
    for (int i = 0; i < (int) P::count; ++i)
    {
        d.prm[(std::size_t) i] = nullptr;
        const juce::String want (paramDef (i).name);
        for (auto* p : d.ap->getParameters())
            if (p->getName (256) == want) { d.prm[(std::size_t) i] = p; break; }
        ok = ok && d.prm[(std::size_t) i] != nullptr;
    }
    return ok;
}

Dut makeDut (Ctx& c, juce::String* err = nullptr)
{
    Dut d;
    if (c.hosted)
    {
        juce::String e;
        auto inst = c.fm.createPluginInstance (c.desc, 48000.0, 512, e);
        if (inst == nullptr) { if (err != nullptr) *err = e; return d; }
        d.ap = inst.get();
        d.owned = std::move (inst);
    }
    else
    {
        auto p = std::make_unique<NorthlineProcessor>();
        d.nl = p.get();
        d.ap = p.get();
        d.owned = std::move (p);
    }
    mapParams (d);
    return d;
}

void setNorm (Dut& d, int i, float v)
{
    if (d.prm[(std::size_t) i] != nullptr)
        d.prm[(std::size_t) i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

// Discrete parameters (Bool, Choice) take only their steps, as a host sends them. Float parameters keep the value.
float quantise (int i, float v)
{
    const auto& def = paramDef (i);
    if (def.kind == ParamDef::Float) return v;
    const int steps = def.kind == ParamDef::Bool ? 2 : (int) def.choices.size();
    if (steps < 2) return 0.0f;
    return (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) (steps - 1)) / (float) (steps - 1);
}

// Selects index k of a Bool (2 steps) or Choice parameter.
void setIndex (Dut& d, int i, int k)
{
    const auto& def = paramDef (i);
    const int steps = def.kind == ParamDef::Bool ? 2 : (int) def.choices.size();
    setNorm (d, i, steps > 1 ? (float) k / (float) (steps - 1) : 0.0f);
}

// Owns an editor the way a host window does: editorBeingDeleted() is called before the editor is deleted,
// otherwise the processor keeps a dangling active-editor pointer (JUCE's contract for editor wrappers).
struct EditorHandle
{
    EditorHandle (juce::AudioProcessor& p) : ap (&p), ed (p.createEditorAndMakeActive()) {}
    ~EditorHandle() { reset(); }
    EditorHandle (const EditorHandle&) = delete;
    EditorHandle& operator= (const EditorHandle&) = delete;

    void reset()
    {
        if (ed != nullptr)
        {
            ap->editorBeingDeleted (ed);
            delete ed;
            ed = nullptr;
        }
    }

    juce::AudioProcessorEditor* get() const { return ed; }
    juce::AudioProcessorEditor* operator->() const { return ed; }

private:
    juce::AudioProcessor* ap;
    juce::AudioProcessorEditor* ed;
};

// Dispatches pending native messages on this thread. JUCE posts its async callbacks to the message queue, and a
// VST3 plug-in's copy of JUCE posts its latency and parameter notifications to the queue of the thread that created it.
void dispatchNativeMessages()
{
#if JUCE_WINDOWS
    MSG msg;
    while (::PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE) != 0)
    {
        ::TranslateMessage (&msg);
        ::DispatchMessageW (&msg);
    }
#endif
}

// Lets the message-thread side run for `ms`: the processor's housekeeping and async undo work, the JUCE timers that
// are due (editor and processor timers), and the native message queue. Runs on the message thread, like a host's
// idle loop.
void pump (Dut& d, int ms)
{
    if (d.nl != nullptr)
    {
        d.nl->servicePendingWork();
        d.nl->flushPendingUndo();
    }
    juce::Timer::callPendingTimersSynchronously();
    dispatchNativeMessages();
    juce::Thread::sleep (ms);
}

//==============================================================================
struct Layout
{
    const char* name;
    int mainIn, mainOut, scCh;
};

constexpr Layout kLayouts[] = {
    { "stereo", 2, 2, 2 },
    { "mono", 1, 1, 1 },
    { "mono-in/stereo-out", 1, 2, 1 },
};

bool applyLayout (juce::AudioProcessor& ap, const Layout& l, bool sidechain)
{
    auto bl = ap.getBusesLayout();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    bl.inputBuses.getReference (0) = l.mainIn == 1 ? mono : stereo;
    bl.outputBuses.getReference (0) = l.mainOut == 1 ? mono : stereo;
    if (bl.inputBuses.size() > 1)
        bl.inputBuses.getReference (1) = sidechain ? (l.scCh == 1 ? mono : stereo) : juce::AudioChannelSet::disabled();
    return ap.setBusesLayout (bl);
}

float sineAt (double fs, double freq, long long n, double amp, double phase = 0.0)
{
    return (float) (amp * std::sin (juce::MathConstants<double>::twoPi * freq * (double) n / fs + phase));
}

// Main input: identical sine on every main channel. Sidechain: a second, lower sine.
void fillGrid (juce::AudioBuffer<float>& b, const Layout& l, bool sidechain, double fs, long long n0)
{
    const int nIn = l.mainIn + (sidechain ? l.scCh : 0);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const long long n = n0 + i;
        const float main = sineAt (fs, 997.0, n, 0.25);
        for (int c = 0; c < l.mainIn; ++c) b.setSample (c, i, main);
        if (sidechain)
            for (int c = 0; c < l.scCh; ++c) b.setSample (l.mainIn + c, i, sineAt (fs, 151.0, n, 0.25, 0.5 * c));
    }
    for (int c = nIn; c < b.getNumChannels(); ++c) b.clear (c, 0, b.getNumSamples());
}

// Stereo render in blocks of `block` samples. Returns the 2-channel output.
juce::AudioBuffer<float> renderStereo (juce::AudioProcessor& ap, const juce::AudioBuffer<float>& in, int block, bool bypassed = false)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (2, n);
    juce::AudioBuffer<float> work (2, block);
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = std::min (block, n - pos);
        work.setSize (2, len, false, false, true);
        for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, in, ch, pos, len);
        audioCall (ap, work, bypassed);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, work, ch, 0, len);
    }
    return out;
}

//==============================================================================
// Audio thread that streams random-length blocks through the instance until stopped, the way a host
// does while the message thread works. The allocation counter follows this thread only.
class AudioStreamer
{
public:
    AudioStreamer (juce::AudioProcessor& p, int maxBlock, juce::uint32 seed, std::thread::id mainId)
        : ap (p), maxBlock (maxBlock), seed (seed), mainId (mainId) {}

    ~AudioStreamer() { stop(); }

    void start()
    {
        quit.store (false);
        th = std::thread ([this] { run(); });
    }

    void stop()
    {
        quit.store (true);
        if (th.joinable()) th.join();
        tool::countingThread = mainId;
    }

    std::atomic<long long> blocks { 0 };
    std::atomic<long long> nonFinite { 0 };

private:
    void run()
    {
        tool::countingThread = std::this_thread::get_id();
        juce::Random rng ((juce::int64) seed);
        const int nCh = std::max (2, std::max (ap.getTotalNumInputChannels(), ap.getTotalNumOutputChannels()));
        juce::AudioBuffer<float> buf (nCh, maxBlock);
        while (! quit.load (std::memory_order_acquire))
        {
            const int len = 1 + rng.nextInt (maxBlock);
            buf.setSize (nCh, len, false, false, true);
            for (int ch = 0; ch < nCh; ++ch)
                for (int i = 0; i < len; ++i) buf.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);
            audioCall (ap, buf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < len; ++i)
                    if (! std::isfinite (buf.getSample (ch, i))) nonFinite.fetch_add (1);
            blocks.fetch_add (1);
            std::this_thread::yield();
        }
    }

    juce::AudioProcessor& ap;
    int maxBlock;
    juce::uint32 seed;
    std::thread::id mainId;
    std::atomic<bool> quit { false };
    std::thread th;
};

// Lets the audio thread stream until it has run `minBlocks` blocks (or one second passes). A fixed sleep is
// not enough: a freshly started audio thread can take several milliseconds to get its first blocks.
void streamUntil (AudioStreamer& audio, Dut& d, long long minBlocks)
{
    const long long b0 = audio.blocks.load();
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (audio.blocks.load() - b0 < minBlocks && juce::Time::getMillisecondCounterHiRes() - t0 < 1000.0)
        pump (d, 1);
}

// Counts parameter-change notifications that carry a latency change.
struct LatencyWatch final : public juce::AudioProcessorListener
{
    std::atomic<int> events { 0 };
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.latencyChanged) events.fetch_add (1);
    }
};

//==============================================================================
// 1. Editor: open, close and re-open, with and without audio, destroy during audio, resize.
void sectionEditor (Ctx& c)
{
    beginSection (c, "editor");
    auto d = makeDut (c);
    if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false))
    {
        record (c, "editor.setup", false, "could not create or configure the instance");
        endSection (c);
        return;
    }
    d.ap->prepareToPlay (48000.0, 256);

    bool ok = true;
    for (int k = 0; k < 10; ++k)
    {
        EditorHandle ed (*d.ap);
        ok = ok && ed.get() != nullptr;
        if (ed.get() != nullptr) pump (d, 20);
    }
    record (c, "editor.open_close_x10", ok, "10 open, close and re-open cycles, no audio");

    AudioStreamer audio (*d.ap, 512, c.seed + 1u, c.mainThread);
    audio.start();
    ok = true;
    for (int k = 0; k < 10; ++k)
    {
        EditorHandle ed (*d.ap);
        ok = ok && ed.get() != nullptr;
        if (ed.get() != nullptr) pump (d, 20);
    }
    audio.stop();
    record (c, "editor.open_close_x10_audio", ok && audio.nonFinite.load() == 0,
            fmt ("10 cycles with audio running, %lld blocks, %lld non-finite samples", audio.blocks.load(), audio.nonFinite.load()));

    audio.start();
    ok = true;
    for (int k = 0; k < 20; ++k)
    {
        EditorHandle ed (*d.ap);
        ok = ok && ed.get() != nullptr;
        ed.reset(); // destroyed straight away, while the audio thread streams
    }
    audio.stop();
    record (c, "editor.destroy_during_audio", ok && audio.nonFinite.load() == 0,
            fmt ("20 editors destroyed while audio runs, %lld non-finite samples", audio.nonFinite.load()));

    EditorHandle ed (*d.ap);
    int minW = 400, minH = 300, maxW = 2000, maxH = 1200;
    if (ed.get() != nullptr)
        if (auto* cons = ed->getConstrainer())
        {
            // A hosted plug-view reports no limits (0 and a huge maximum): keep the fallback then.
            const int w0 = cons->getMinimumWidth(), h0 = cons->getMinimumHeight();
            const int w1 = cons->getMaximumWidth(), h1 = cons->getMaximumHeight();
            if (w0 >= 100 && h0 >= 100 && w1 <= 10000 && h1 <= 10000)
            {
                minW = w0; minH = h0;
                maxW = w1; maxH = h1;
            }
        }
    bool sized = ed.get() != nullptr;
    if (ed.get() != nullptr)
    {
        juce::Random rng ((juce::int64) c.seed * 31 + 7);
        std::vector<std::pair<int, int>> sizes { { minW, minH }, { maxW, maxH } };
        for (int k = 0; k < 8; ++k)
            sizes.push_back ({ minW + rng.nextInt (juce::jmax (1, maxW - minW + 1)), minH + rng.nextInt (juce::jmax (1, maxH - minH + 1)) });
        audio.start();
        for (auto [w, h] : sizes)
        {
            ed->setSize (w, h);
            pump (d, 5);
            const bool exact = ed->getWidth() == w && ed->getHeight() == h;
            // In-process the editor takes the size as given; a hosted plug-view may clamp it.
            sized = sized && (d.nl != nullptr ? exact : (ed->getWidth() > 0 && ed->getHeight() > 0));
        }
        audio.stop();
    }
    ed.reset();
    record (c, "editor.resize", sized,
            fmt ("min %dx%d, max %dx%d and 8 random sizes, with audio running", minW, minH, maxW, maxH));
    endSection (c);
}

//==============================================================================
// 2. Sample rates x block sizes x layouts x sidechain.
void sectionGrid (Ctx& c)
{
    beginSection (c, "grid");
    const std::vector<double> rates = c.quick ? std::vector<double> { 22050, 48000, 192000 }
                                              : std::vector<double> { 22050, 44100, 48000, 88200, 96000, 176400, 192000 };
    const std::vector<int> blocks = c.quick ? std::vector<int> { 1, 7, 256, 4096 }
                                            : std::vector<int> { 1, 7, 32, 64, 256, 512, 1024, 4096 };

    int configs = 0;
    std::vector<juce::String> unsupportedLines, nonFiniteLines, silentLines, asymLines;

    for (double fs : rates)
        for (int block : blocks)
            for (const auto& lay : kLayouts)
                for (int sc = 0; sc < 2; ++sc)
                {
                    const juce::String repro = fmt ("seed=%u rate=%.0f block=%d layout=%s sidechain=%s", (unsigned) c.seed,
                                                    fs, block, lay.name, sc != 0 ? "on" : "off");
                    auto d = makeDut (c);
                    if (d.ap == nullptr || ! applyLayout (*d.ap, lay, sc != 0))
                    {
                        unsupportedLines.push_back (repro);
                        continue;
                    }
                    d.ap->prepareToPlay (fs, block);
                    ++configs;

                    const int nCh = juce::jmax (1, juce::jmax (d.ap->getTotalNumInputChannels(), d.ap->getTotalNumOutputChannels()));
                    const long long total = std::max<long long> (8192, 4LL * block);
                    const long long half = total / 2;
                    juce::AudioBuffer<float> buf (nCh, block);
                    bool finite = true, symmetric = true;
                    double sumSq[2] { 0.0, 0.0 };

                    for (long long pos = 0; pos < total; pos += block)
                    {
                        const int len = (int) std::min<long long> (block, total - pos);
                        buf.setSize (nCh, len, false, false, true);
                        fillGrid (buf, lay, sc != 0, fs, pos);
                        audioCall (*d.ap, buf);
                        for (int ch = 0; ch < lay.mainOut; ++ch)
                            for (int i = 0; i < len; ++i)
                            {
                                const float v = buf.getSample (ch, i);
                                if (! std::isfinite (v)) finite = false;
                                if (pos + i >= half) sumSq[ch] += (double) v * v;
                            }
                        if (lay.mainOut == 2)
                            for (int i = 0; i < len; ++i)
                                if (buf.getSample (0, i) != buf.getSample (1, i)) symmetric = false;
                    }

                    bool quiet = false;
                    for (int ch = 0; ch < lay.mainOut; ++ch)
                        if (std::sqrt (sumSq[ch] / (double) (total - half)) < 0.01) quiet = true; // below -40 dBFS RMS

                    if (! finite) nonFiniteLines.push_back (repro);
                    if (quiet) silentLines.push_back (repro);
                    if (! symmetric && lay.mainOut == 2) asymLines.push_back (repro);
                }

    record (c, "grid.supported", unsupportedLines.empty(),
            fmt ("%d configurations prepared, %d refused", configs, (int) unsupportedLines.size()), unsupportedLines);
    record (c, "grid.finite", nonFiniteLines.empty(),
            fmt ("%d configurations, %d with NaN/Inf", configs, (int) nonFiniteLines.size()), nonFiniteLines);
    record (c, "grid.nonsilent", silentLines.empty(),
            fmt ("%d configurations, %d with output below -40 dBFS", configs, (int) silentLines.size()), silentLines);
    record (c, "grid.symmetric", asymLines.empty(),
            fmt ("identical L/R input gives identical L/R output: %d mismatching", (int) asymLines.size()), asymLines);
    endSection (c);
}

// Stereo render with a seeded schedule of 1..4096 sample blocks on an instance prepared for 4096.
juce::AudioBuffer<float> renderScheduled (Ctx& c, double fs, juce::uint32 scheduleSeed, long long n, bool& finite)
{
    juce::AudioBuffer<float> out (2, (int) n);
    finite = true;
    auto d = makeDut (c);
    if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false)) { finite = false; return out; }
    d.ap->prepareToPlay (fs, 4096);

    juce::Random rng ((juce::int64) scheduleSeed);
    juce::AudioBuffer<float> work (2, 4096);
    long long pos = 0;
    while (pos < n)
    {
        const int len = (int) std::min<long long> (1 + rng.nextInt (4096), n - pos);
        work.setSize (2, len, false, false, true);
        fillGrid (work, kLayouts[0], false, fs, pos);
        audioCall (*d.ap, work);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                const float v = work.getSample (ch, i);
                if (! std::isfinite (v)) finite = false;
                out.setSample (ch, (int) (pos + i), v);
            }
        pos += len;
    }
    return out;
}

// 2b. Variable block sizes inside one prepare: finite, not silent, and repeatable with the same schedule.
void sectionVariable (Ctx& c)
{
    beginSection (c, "variable");
    const std::vector<double> rates = c.quick ? std::vector<double> { 48000, 192000 }
                                              : std::vector<double> { 22050, 48000, 96000, 192000 };
    int nonFinite = 0, silent = 0, unrepeatable = 0;
    juce::String first;
    for (double fs : rates)
    {
        const long long n = (long long) (fs * 0.5);
        const auto seed = c.seed * 977u + (juce::uint32) fs;
        bool finA = true, finB = true;
        auto a = renderScheduled (c, fs, seed, n, finA);
        auto b = renderScheduled (c, fs, seed, n, finB);
        if (! finA || ! finB) ++nonFinite;

        double sumSq = 0.0;
        for (int i = (int) (n / 2); i < n; ++i) sumSq += (double) a.getSample (0, i) * a.getSample (0, i);
        if (std::sqrt (sumSq / (double) (n - n / 2)) < 0.01) ++silent;

        bool same = true;
        for (int ch = 0; ch < 2 && same; ++ch)
            for (int i = 0; i < n && same; ++i) same = a.getSample (ch, i) == b.getSample (ch, i);
        if (! same) { if (++unrepeatable == 1) first = fmt ("seed=%u rate=%.0f block=1..4096 random", (unsigned) c.seed, fs); }
    }
    record (c, "variable.finite", nonFinite == 0, fmt ("%d rates, %d with NaN/Inf", (int) rates.size(), nonFinite));
    record (c, "variable.nonsilent", silent == 0, fmt ("%d rates, %d below -40 dBFS", (int) rates.size(), silent));
    record (c, "variable.repeatable", unrepeatable == 0,
            fmt ("same schedule, two fresh instances: %d differing%s%s", unrepeatable, unrepeatable ? "; first: " : "", first.toRawUTF8()));
    endSection (c);
}

//==============================================================================
// 3. Parameter fuzz: every parameter set to a random value every block, with ramps on a subset.
void sectionFuzz (Ctx& c)
{
    beginSection (c, "fuzz");
    const std::vector<double> rates = c.quick ? std::vector<double> { 48000 } : std::vector<double> { 48000, 96000 };
    const double seconds = c.quick ? 0.5 : 2.0;
    constexpr int kBlock = 256;

    bool finite = true;
    float worstPeak = 0.0f;
    juce::String repro;

    for (double fs : rates)
    {
        auto d = makeDut (c);
        if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false)) { finite = false; continue; }
        d.ap->prepareToPlay (fs, kBlock);

        juce::Random rng ((juce::int64) c.seed * 7919 + (juce::int64) fs);
        std::array<bool, kParams> ramp {};
        std::array<float, kParams> from {}, to {};
        for (std::size_t i = 0; i < kParams; ++i)
        {
            ramp[i] = rng.nextFloat() < 0.25f;
            from[i] = rng.nextFloat();
            to[i] = rng.nextFloat();
        }

        const long long blocks = (long long) (seconds * fs / kBlock);
        juce::AudioBuffer<float> buf (2, kBlock);
        for (long long b = 0; b < blocks; ++b)
        {
            const float t = (float) b / (float) blocks;
            for (int i = 0; i < (int) kParams; ++i)
                setNorm (d, i, quantise (i, ramp[(std::size_t) i] ? from[(std::size_t) i] + (to[(std::size_t) i] - from[(std::size_t) i]) * t
                                                                  : rng.nextFloat()));

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i) buf.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.5f);
            if (b % 37 == 0) buf.setSample (0, 0, 1.0f); // occasional full-scale impulse

            audioCall (*d.ap, buf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) { if (finite) repro = fmt ("seed=%u rate=%.0f block=%d, block %lld", (unsigned) c.seed, fs, kBlock, b); finite = false; }
                    else worstPeak = std::max (worstPeak, std::abs (v));
                }
        }
    }
    record (c, "fuzz.finite", finite, finite ? fmt ("%d rates, every parameter randomised per block", (int) rates.size())
                                            : fmt ("NaN or Inf found; first: %s", repro.toRawUTF8()));
    record (c, "fuzz.bounded", worstPeak < 1000.0f, fmt ("peak %.1f dBFS (bound +60 dBFS)", juce::Decibels::gainToDecibels (worstPeak, -200.0f)));
    endSection (c);
}

//==============================================================================
// 3b. Silence in gives silence out after the tail, with every module on and the limiter off.
void sectionSilence (Ctx& c)
{
    beginSection (c, "silence");
    const std::vector<double> rates = c.quick ? std::vector<double> { 48000 } : std::vector<double> { 44100, 48000, 192000 };
    const int on[] { P::preOn, P::satOn, P::hpOn, P::lpOn, P::weightOn, P::gateOn, P::dsOn, P::ds1On, P::ds2On,
                     P::eqOn, P::eqLowOn, P::eqMidOn, P::eqToneOn, P::eqHighOn, P::compOn };

    float worstDb = -300.0f;
    juce::String worst;
    bool ok = true;
    for (double fs : rates)
        for (int quality = 0; quality < 2; ++quality)
        {
            auto d = makeDut (c);
            if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false)) { ok = false; continue; }
            for (int i : on) setIndex (d, i, 1);
            setIndex (d, P::limOn, 0);
            setIndex (d, P::quality, quality);
            d.ap->prepareToPlay (fs, 256);

            juce::Random rng ((juce::int64) c.seed + (juce::int64) fs + quality);
            const long long noiseN = (long long) fs, tailN = 2LL * (long long) fs;
            juce::AudioBuffer<float> buf (2, 256);
            float tailPeak = 0.0f;
            for (long long pos = 0; pos < noiseN + tailN; pos += 256)
            {
                const int len = (int) std::min<long long> (256, noiseN + tailN - pos);
                buf.setSize (2, len, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < len; ++i)
                        buf.setSample (ch, i, pos + i < noiseN ? (rng.nextFloat() * 2.0f - 1.0f) * 0.1f : 0.0f);
                audioCall (*d.ap, buf);
                if (pos + len > noiseN + (long long) fs) // the last second only
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < len; ++i)
                            if (pos + i >= noiseN + (long long) fs) tailPeak = std::max (tailPeak, std::abs (buf.getSample (ch, i)));
            }
            const float db = juce::Decibels::gainToDecibels (tailPeak, -300.0f);
            if (worst.isEmpty() || db > worstDb) { worstDb = db; worst = fmt ("rate=%.0f quality=%s", fs, quality ? "HQ" : "Eco"); }
            if (db > -120.0f) ok = false;
        }
    record (c, "silence.tail", ok, fmt ("worst tail %.1f dBFS (bound -120 dBFS) at %s", worstDb, worst.toRawUTF8()));
    endSection (c);
}

//==============================================================================
// 4. State: random parameters, save, load into a fresh instance, identical parameters and output.
void sectionState (Ctx& c)
{
    beginSection (c, "state");
    const int trials = c.quick ? 2 : 6;
    constexpr double fs = 48000.0;
    int paramsOff = 0;
    double worstDiff = 0.0;
    juce::String worstName;
    std::vector<juce::String> paramLines, outLines;

    for (int t = 0; t < trials; ++t)
    {
        const juce::String repro = fmt ("seed=%u trial=%d rate=48000 block=256 layout=stereo", (unsigned) c.seed, t);
        juce::Random rng ((juce::int64) c.seed * 131 + (juce::int64) t);

        auto a = makeDut (c);
        if (a.ap == nullptr || ! applyLayout (*a.ap, kLayouts[0], false)) { paramLines.push_back (repro); continue; }
        a.ap->prepareToPlay (fs, 256);
        for (int i = 0; i < (int) P::count; ++i)
            if (! isTransient (i)) setNorm (a, i, quantise (i, rng.nextFloat())); // transient params are reset by a state load, by design
        juce::MemoryBlock state;
        a.ap->getStateInformation (state);

        juce::AudioBuffer<float> in (2, 16384);
        juce::Random noise (c.seed + 5u);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (ch, i, (noise.nextFloat() * 2.0f - 1.0f) * 0.25f);
        const auto outA = renderStereo (*a.ap, in, 256);

        auto b = makeDut (c);
        if (b.ap == nullptr || ! applyLayout (*b.ap, kLayouts[0], false)) { paramLines.push_back (repro); continue; }
        b.ap->prepareToPlay (fs, 256);
        b.ap->setStateInformation (state.getData(), (int) state.getSize());
        // The state stores plain values as text, so a normalised value can differ in the last bits. In-process the
        // tolerance is 1e-6; a hosted VST3 reads values back through the host's own conversion, so it is 1e-4. The
        // outputs must match bit for bit either way, and the largest difference is reported.
        const double tol = c.hosted ? 1.0e-4 : 1.0e-6;
        for (int i = 0; i < (int) P::count; ++i)
        {
            if (isTransient (i)) continue;
            const double diff = std::abs ((double) a.prm[(std::size_t) i]->getValue() - (double) b.prm[(std::size_t) i]->getValue());
            if (diff > worstDiff) { worstDiff = diff; worstName = fmt ("%s (trial %d)", paramDef (i).name, t); }
            if (diff > tol)
            {
                ++paramsOff;
                paramLines.push_back (fmt ("%s, trial %d: source %.9f, loaded %.9f, %s", paramDef (i).name, t,
                                           (double) a.prm[(std::size_t) i]->getValue(), (double) b.prm[(std::size_t) i]->getValue(), repro.toRawUTF8()));
            }
        }
        const auto outB = renderStereo (*b.ap, in, 256);

        bool same = true;
        for (int ch = 0; ch < 2 && same; ++ch)
            for (int i = 0; i < in.getNumSamples() && same; ++i) same = outA.getSample (ch, i) == outB.getSample (ch, i);
        if (! same) outLines.push_back (repro);
    }
    record (c, "state.params", paramLines.empty() && paramsOff == 0,
            fmt ("%d trials, %d parameters off by more than %.0e after load; largest difference %.2e on %s",
                 trials, paramsOff, c.hosted ? 1.0e-4 : 1.0e-6, worstDiff, worstName.toRawUTF8()), paramLines);
    record (c, "state.bitexact", outLines.empty(),
            fmt ("%d trials, %d with output differing from the source instance", trials, (int) outLines.size()), outLines);
    endSection (c);
}

//==============================================================================
// 5 and 6 share the latency sweep: latency sources on, IIR stages off, so an impulse peak is an exact delay.
struct Combo
{
    int quality, gate, ds, comp, lim;
};

std::vector<Combo> makeCombos (bool quick)
{
    std::vector<Combo> v;
    const int steps = (int) std::size (kLookaheadMs);
    for (int q = 0; q < 2; ++q)
        for (int m = 0; m < 2; ++m)
        {
            if (quick)
                for (int k = 0; k < steps; ++k) v.push_back ({ q, k, k, k, m });
            else
                for (int g = 0; g < steps; ++g)
                    for (int d = 0; d < steps; ++d)
                        for (int cc = 0; cc < steps; ++cc) v.push_back ({ q, g, d, cc, m });
        }
    return v;
}

juce::String describe (const Combo& k)
{
    return fmt ("quality=%s gateLA=%d dsLA=%d compLA=%d limMode=%s", k.quality ? "HQ" : "Eco", k.gate, k.ds, k.comp, k.lim ? "lookahead" : "normal");
}

void applyCombo (Dut& d, const Combo& k)
{
    setIndex (d, P::hpOn, 0);
    setIndex (d, P::lpOn, 0);
    setIndex (d, P::weightOn, 0);
    setIndex (d, P::eqOn, 0);
    setIndex (d, P::preOn, 1);
    setIndex (d, P::satOn, 1);
    setIndex (d, P::gateOn, 1);
    setIndex (d, P::dsOn, 1);
    setIndex (d, P::compOn, 1);
    setIndex (d, P::limOn, 1);
    setIndex (d, P::quality, k.quality);
    setIndex (d, P::gateLookahead, k.gate);
    setIndex (d, P::dsLookahead, k.ds);
    setIndex (d, P::compLookahead, k.comp);
    setIndex (d, P::limMode, k.lim);
    setNorm (d, P::gateThresh, 0.0f);  // gate open for the impulse
    setNorm (d, P::compThresh, 1.0f);  // compressor idle for the impulse
    setNorm (d, P::ds1Thresh, 1.0f);   // de-esser idle for the impulse, so its gain does not move the peak
    setNorm (d, P::ds2Thresh, 1.0f);
}

// Prepares, runs silence through the instance so pending parameter changes apply, and reads the latency.
int settleLatency (Dut& d, double fs, int block)
{
    d.ap->prepareToPlay (fs, block);
    juce::AudioBuffer<float> z (2, block);
    z.clear();
    audioCall (*d.ap, z);
    audioCall (*d.ap, z);
    pump (d, 20);
    d.ap->prepareToPlay (fs, block);
    audioCall (*d.ap, z);
    pump (d, 20);
    return d.ap->getLatencySamples();
}

struct Measure
{
    int reported = 0, measured = 0;
    float peak = 0.0f;
    bool audible = false;
};

Measure measureLatency (Dut& d, double fs, int block)
{
    Measure m;
    m.reported = settleLatency (d, fs, block);
    d.ap->prepareToPlay (fs, block);
    constexpr int n = 8192, at = 1024;
    juce::AudioBuffer<float> in (2, n);
    in.clear();
    in.setSample (0, at, 0.5f);
    in.setSample (1, at, 0.5f);
    const auto out = renderStereo (*d.ap, in, block);
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (std::abs (out.getSample (0, i)) > m.peak)
        {
            m.peak = std::abs (out.getSample (0, i));
            idx = i;
        }
    m.audible = m.peak > 1.0e-4f;
    m.measured = idx - at;
    return m;
}

// 5. Reported latency equals the impulse delay for every quality and lookahead combination, and a
//    latency change notifies the host.
void sectionLatency (Ctx& c)
{
    beginSection (c, "latency");
    constexpr double fs = 48000.0;
    constexpr int block = 256;
    const auto combos = makeCombos (c.quick);

    std::vector<juce::String> badLines;
    int mismatches = 0, silentCombos = 0;
    for (const auto& k : combos)
    {
        const juce::String repro = fmt ("seed=%u rate=%.0f block=%d layout=stereo %s", (unsigned) c.seed, fs, block, describe (k).toRawUTF8());
        auto d = makeDut (c);
        if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false)) { ++silentCombos; badLines.push_back (repro + " (instance)"); continue; }
        applyCombo (d, k);
        const auto m = measureLatency (d, fs, block);
        if (! m.audible)
        {
            ++silentCombos;
            badLines.push_back (repro + " (no impulse output)");
        }
        else if (m.reported != m.measured)
        {
            ++mismatches;
            badLines.push_back (fmt ("reported %d, measured %d, peak %.3f: %s", m.reported, m.measured, (double) m.peak, repro.toRawUTF8()));
        }
    }
    record (c, "latency.match", mismatches == 0 && silentCombos == 0,
            fmt ("%d combinations at %.0f Hz, block %d: %d mismatched, %d silent", (int) combos.size(), fs, block, mismatches, silentCombos),
            badLines);

    // Notification: a change from zero latency to the worst combination must reach the listener.
    {
        auto d = makeDut (c);
        bool notified = false;
        juce::String detail = "could not create the instance";
        if (d.ap != nullptr && applyLayout (*d.ap, kLayouts[0], false))
        {
            Combo off { 0, 0, 0, 0, 0 };
            applyCombo (d, off);
            setIndex (d, P::preOn, 0);
            setIndex (d, P::satOn, 0);
            setIndex (d, P::gateOn, 0);
            setIndex (d, P::dsOn, 0);
            setIndex (d, P::compOn, 0);
            setIndex (d, P::limOn, 0);
            const int before = settleLatency (d, fs, block);

            LatencyWatch watch;
            d.ap->addListener (&watch);
            applyCombo (d, { 1, 3, 3, 3, 1 });
            juce::AudioBuffer<float> z (2, block);
            z.clear();
            audioCall (*d.ap, z);
            // A hosted plug-in reports its change through its own message loop: wait for the notification (up to 1 s).
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            while (watch.events.load() == 0 && juce::Time::getMillisecondCounterHiRes() - t0 < 1000.0) pump (d, 10);
            const int after = d.ap->getLatencySamples();
            notified = watch.events.load() > 0 && after != before;
            detail = fmt ("latency %d -> %d samples, %d change notification(s)", before, after, watch.events.load());
            d.ap->removeListener (&watch);
        }
        record (c, "latency.notify", notified, detail);
    }
    endSection (c);
}

//==============================================================================
// 6. Bypass: processBlockBypassed must give the input delayed by the reported latency.
void sectionBypass (Ctx& c)
{
    beginSection (c, "bypass");
    constexpr double fs = 48000.0;
    constexpr int block = 256;
    const auto combos = makeCombos (c.quick);

    std::vector<juce::String> badLines;
    int withLatency = 0;
    for (const auto& k : combos)
    {
        auto d = makeDut (c);
        if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false)) continue;
        applyCombo (d, k);
        const int latency = settleLatency (d, fs, block);
        if (latency > 0) ++withLatency;

        constexpr int n = 4096;
        juce::AudioBuffer<float> in (2, n);
        juce::Random rng ((juce::int64) c.seed * 17 + latency);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) in.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        d.ap->prepareToPlay (fs, block);
        const auto out = renderStereo (*d.ap, in, block, true);

        bool ok = true;
        for (int ch = 0; ch < 2 && ok; ++ch)
            for (int i = 0; i < n && ok; ++i)
            {
                const float expected = i >= latency ? in.getSample (ch, i - latency) : 0.0f;
                ok = out.getSample (ch, i) == expected;
            }
        if (! ok)
            badLines.push_back (fmt ("reported latency %d: seed=%u rate=%.0f block=%d layout=stereo %s", latency, (unsigned) c.seed, fs, block,
                                     describe (k).toRawUTF8()));
    }
    record (c, "bypass.delayed", badLines.empty(),
            fmt ("%d combinations (%d with latency): %d not delayed by the reported latency", (int) combos.size(), withLatency,
                 (int) badLines.size()), badLines);
    endSection (c);
}

//==============================================================================
// 7. Thread safety: the message thread changes parameters, loads presets, A/B, undo and Smart Start
//    while the audio thread streams. Then state is loaded while the stream is stopped.
void sectionThreads (Ctx& c)
{
    beginSection (c, "threads");
    constexpr double fs = 48000.0;
    constexpr int maxBlock = 1024;
    auto d = makeDut (c);
    if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false))
    {
        record (c, "threads.storm", false, "could not create the instance");
        endSection (c);
        return;
    }
    d.ap->prepareToPlay (fs, maxBlock);
    juce::MemoryBlock saved;
    d.ap->getStateInformation (saved);

    AudioStreamer audio (*d.ap, maxBlock, c.seed + 7u, c.mainThread);
    juce::Random rng ((juce::int64) c.seed * 4421 + 3);
    const double seconds = c.quick ? 5.0 : 30.0;
    long long ops = 0;
    audio.start();
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < seconds * 1000.0)
    {
        const int op = rng.nextInt (100);
        if (op < 40)
        {
            const int i = rng.nextInt ((int) P::count);
            setNorm (d, i, quantise (i, rng.nextFloat()));
        }
        else if (d.nl != nullptr)
        {
            if (op < 46)      d.nl->loadFactoryPreset (rng.nextInt ((int) factoryPresets().size()));
            else if (op < 50) d.nl->selectSlot (rng.nextInt (2));
            else if (op < 52) d.nl->copyActiveToOther();
            else if (op < 56) { if (d.nl->canUndo()) d.nl->undo(); }
            else if (op < 60) { if (d.nl->canRedo()) d.nl->redo(); }
            else if (op < 66) d.nl->smartListen ((smart::Source) rng.nextInt (smart::kNumSources));
            else if (op < 70) d.nl->smartCancel();
            else if (op < 74) d.nl->pushUndoSnapshot ("Validate");
            else if (op < 75) d.nl->resetToDefaults();
            else if (op < 80) d.ap->setStateInformation (saved.getData(), (int) saved.getSize());
            else if (op < 90) d.nl->setCompare (rng.nextInt (7) - 1); // Compare: off, whole strip, or one module
        }
        else if (op < 80) d.ap->setStateInformation (saved.getData(), (int) saved.getSize());

        pump (d, rng.nextInt (3));
        ++ops;
    }
    audio.stop();
    const long long stormBlocks = audio.blocks.load(), stormBad = audio.nonFinite.load();
    record (c, "threads.storm", stormBad == 0 && stormBlocks > 0,
            fmt ("%.0f s, %lld message-thread operations, %lld audio blocks, %lld non-finite samples", seconds, ops, stormBlocks, stormBad));

    // State loads while the stream is stopped, then streaming resumes on the loaded state.
    const long long blocks0 = audio.blocks.load(), bad0 = audio.nonFinite.load();
    for (int k = 0; k < 10; ++k)
    {
        audio.start();
        streamUntil (audio, d, 20);
        audio.stop();
        d.ap->setStateInformation (saved.getData(), (int) saved.getSize());
        d.ap->prepareToPlay (fs, maxBlock);
        audio.start();
        streamUntil (audio, d, 20);
        audio.stop();
    }
    const long long loadBlocks = audio.blocks.load() - blocks0, loadBad = audio.nonFinite.load() - bad0;
    record (c, "threads.state_load_stopped", loadBad == 0 && loadBlocks >= 400,
            fmt ("10 loads with the stream stopped, %lld audio blocks, %lld non-finite samples", loadBlocks, loadBad));
    endSection (c);
}

//==============================================================================
//==============================================================================
// 8. Compare (roadmap 9.3): switching Compare must not click, and must not move the latency. Every ordered
//    pair of targets (-1 off, 0 whole strip, 1 + moduleId) is switched on a block boundary while a steady
//    100 Hz sine runs through every module. In-process only: Compare is NorthlineProcessor API.
void sectionCompare (Ctx& c)
{
    beginSection (c, "compare");
    if (c.hosted)
    {
        skip (c, "compare.click_free", "Compare is NorthlineProcessor API, not reachable through a hosted VST3");
        skip (c, "compare.latency", "as above");
        endSection (c);
        return;
    }
    constexpr double fs = 48000.0;
    constexpr int block = 256;
    constexpr long long warm = 64 * block, after = 16 * block; // the switch falls on the block that starts at `warm`
    constexpr int targets[] { -1, 0, 1, 2, 3, 4, 5 };
    const int on[] { P::preOn, P::satOn, P::hpOn, P::lpOn, P::weightOn, P::gateOn, P::dsOn, P::ds1On, P::ds2On,
                     P::eqOn, P::eqLowOn, P::eqMidOn, P::eqToneOn, P::eqHighOn, P::compOn };

    auto targetName = [] (int t) -> juce::String {
        if (t < 0) return "off";
        if (t == 0) return "whole strip";
        return juce::String ("module ") + juce::String (moduleName (t - 1));
    };

    struct Trace
    {
        std::vector<float> y;
        int latMin = 1 << 30, latMax = -1;
        bool finite = true;
    };
    // Starts in Compare target `from`; switches to `to` at sample `warm` (no switch when to == from).
    auto trace = [&] (int from, int to) {
        Trace t;
        auto d = makeDut (c);
        if (d.ap == nullptr || ! applyLayout (*d.ap, kLayouts[0], false)) { t.finite = false; return t; }
        for (int i : on) setIndex (d, i, 1);
        setIndex (d, P::limOn, 0);
        setIndex (d, P::quality, 0);
        d.ap->prepareToPlay (fs, block);
        d.nl->setCompare (from);
        juce::AudioBuffer<float> buf (2, block);
        for (long long pos = 0; pos < warm + after; pos += block)
        {
            if (pos == warm && to != from) d.nl->setCompare (to);
            buf.setSize (2, block, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i) buf.setSample (ch, i, sineAt (fs, 100.0, pos + i, 0.25));
            audioCall (*d.ap, buf);
            const int lat = d.ap->getLatencySamples();
            t.latMin = std::min (t.latMin, lat);
            t.latMax = std::max (t.latMax, lat);
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v)) t.finite = false;
                t.y.push_back (v);
            }
        }
        return t;
    };
    // Largest sample-to-sample step of y over [from, to).
    auto maxStep = [] (const std::vector<float>& y, long long from, long long to) {
        double m = 0.0;
        for (long long n = std::max<long long> (from, 1); n < to; ++n)
            m = std::max (m, (double) std::abs (y[(std::size_t) n] - y[(std::size_t) (n - 1)]));
        return m;
    };

    std::vector<juce::String> badLines;
    int transitions = 0, latMin = 1 << 30, latMax = -1;
    bool finite = true;
    double worst = 0.0;
    for (int a : targets)
        for (int b : targets)
        {
            if (a == b) continue;
            ++transitions;
            const auto sw = trace (a, b);
            const auto steadyB = trace (b, b);
            finite = finite && sw.finite && steadyB.finite;
            latMin = std::min ({ latMin, sw.latMin, steadyB.latMin });
            latMax = std::max ({ latMax, sw.latMax, steadyB.latMax });

            // Limit: 1.5 times the steady step on either side of the switch, plus a small floor.
            const double limit = 1.5 * std::max (maxStep (sw.y, warm - 4096, warm), maxStep (steadyB.y, warm - 4096, warm)) + 1.0e-3;
            const double atSwitch = maxStep (sw.y, warm, warm + 1);
            const double afterMax = maxStep (sw.y, warm + 1, warm + after);
            worst = std::max (worst, std::max (atSwitch, afterMax) / limit);
            if (std::max (atSwitch, afterMax) > limit)
                badLines.push_back (fmt ("from %s to %s: step %.5f at the switch, %.5f after (limit %.5f), seed=%u rate=48000 block=256",
                                         targetName (a).toRawUTF8(), targetName (b).toRawUTF8(), atSwitch, afterMax, limit, (unsigned) c.seed));
        }

    record (c, "compare.click_free", badLines.empty() && finite,
            fmt ("%d transitions between %d Compare targets, %d with a step above 1.5x steady (worst ratio %.2f)",
                 transitions, (int) std::size (targets), (int) badLines.size(), worst), badLines);
    record (c, "compare.latency", latMin == latMax && latMin >= 0,
            fmt ("latency %d..%d samples across all transitions", latMin, latMax));
    record (c, "compare.finite", finite, "no NaN or Inf across the transitions");
    endSection (c);
}

//==============================================================================
// 9. Long run: counters near 2^31 and 2^32 must wrap without a discontinuity, NaN or state jump.
//    Not run here. The strip keeps no absolute sample count: ControlClock::phase is always below
//    kControlBlock, so there is nothing to seed. The uint32 counters that can wrap (ChannelStrip::ringGeneration,
//    ChannelStrip::paramUpdates, MeterHub::sequenceCounter) and LevelMatch's hopsSeen need a test seam. See the report.
void sectionLongRun (Ctx& c)
{
    beginSection (c, "longrun");
    skip (c, "longrun.wrap_2e31", "needs a test seam: the strip has no absolute sample or clock counter (ControlClock::phase is below kControlBlock)");
    skip (c, "longrun.wrap_2e32", "needs a test seam for ChannelStrip::ringGeneration, paramUpdates and MeterHub::sequenceCounter");
    endSection (c);
}

void printSummary (Ctx& c)
{
    std::printf ("\n%-12s %7s %6s %6s %6s %6s %9s %10s\n", "section", "checks", "pass", "fail", "known", "skip", "time s", "allocs");
    long long allocs = 0;
    int skipped = 0;
    for (const auto& r : c.rows)
    {
        std::printf ("%-12s %7d %6d %6d %6d %6d %9.1f %10lld\n", r.name.toRawUTF8(), r.checks, r.pass, r.fail, r.known, r.skipped,
                     r.ms / 1000.0, r.allocs);
        allocs += r.allocs;
        skipped += r.skipped;
    }
    std::printf ("%-12s %7s %6s %6s %6s %6d %9s %10lld\n", "total", "", "", "", "", skipped, "", allocs);
}

int runAll (Ctx& c)
{
    c.mainThread = std::this_thread::get_id();
    tool::countingThread = c.mainThread;
    if (c.hosted)
    {
        // Parameter writes go through P indices; a hosted parameter that does not map would make them no-ops.
        beginSection (c, "setup");
        Dut probe = makeDut (c);
        int missing = 0;
        juce::String firstMissing;
        for (int i = 0; i < (int) P::count; ++i)
            if (probe.prm[(std::size_t) i] == nullptr && missing++ == 0) firstMissing = paramDef (i).name;
        record (c, "vst3.params", probe.ap != nullptr && missing == 0,
                fmt ("%d of %d parameters matched by name%s%s", (int) P::count - missing, (int) P::count,
                     missing ? "; first unmatched: " : "", firstMissing.toRawUTF8()));
        endSection (c);
    }
    sectionEditor (c);
    sectionGrid (c);
    sectionVariable (c);
    sectionFuzz (c);
    sectionSilence (c);
    sectionState (c);
    sectionLatency (c);
    sectionBypass (c);
    sectionThreads (c);
    sectionCompare (c);
    sectionLongRun (c);
    printSummary (c);
    std::printf ("\n%d failure(s), %d known; %s\n", c.failures, c.known, c.failures == 0 ? "PASS" : "FAIL");
    return c.failures;
}

} // namespace

//==============================================================================
// Used by `validate` and by the validate_quick test. `failed` and `known` receive the counts.
int runSuite (bool quick, juce::uint32 seed, int& failed, int& known)
{
    Ctx c;
    c.quick = quick;
    c.seed = seed;
    std::printf ("NorthlineTool validate: in-process, %s, seed %u\n", quick ? "quick" : "full", (unsigned) seed);
    const int f = runAll (c);
    failed = f;
    known = c.known;
    return f;
}

// Command line: validate [file.vst3] [--quick] [--seed N]
int runCli (const juce::StringArray& args)
{
    Ctx c;
    c.quick = args.contains ("--quick");
    juce::String path;
    for (int i = 1; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a == "--seed" && i + 1 < args.size()) { c.seed = (juce::uint32) args[++i].getLargeIntValue(); continue; }
        if (a.startsWith ("--seed=")) { c.seed = (juce::uint32) a.fromFirstOccurrenceOf ("=", false, false).getLargeIntValue(); continue; }
        if (! a.startsWith ("--") && path.isEmpty()) path = a;
    }

    if (path.isNotEmpty())
    {
        const juce::File file (path);
        juce::addDefaultFormatsToManager (c.fm);
        juce::OwnedArray<juce::PluginDescription> types;
        for (auto* f : c.fm.getFormats())
            if (f->getName() == "VST3") f->findAllTypesForFile (types, file.getFullPathName());
        if (types.isEmpty())
        {
            std::printf ("No VST3 plug-ins found in %s\n", file.getFullPathName().toRawUTF8());
            return 1;
        }
        c.hosted = true;
        c.desc = *types[0];
        c.pluginName = c.desc.name;
    }

    std::printf ("NorthlineTool validate: %s, %s, seed %u\n",
                 c.hosted ? c.pluginName.toRawUTF8() : "in-process processor", c.quick ? "quick" : "full", (unsigned) c.seed);
    return runAll (c) == 0 ? 0 : 1;
}

} // namespace tool::validate

namespace tool
{
int runValidate (const juce::StringArray& args)
{
    return validate::runCli (args);
}
} // namespace tool

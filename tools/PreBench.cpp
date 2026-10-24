#include "PreHarness.h"
#include "BenchTimer.h"
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

// PRE performance and regression tools (not sound measurements; thresholds live in tools/tests/TestPre.cpp):
//   pre-bench time            CPU per 64-sample block of the PRE module alone, in the bench's configurations
//   pre-bench stages          CPU per stage of the saturation path (per 64-sample block, stereo)
//   pre-bench dump <file>     renders the reference cases (PRE module, channel 0) into a binary file
//   pre-bench null <file>     renders the same cases again and prints the null depth against the file
// A speed-up that must not change the sound is checked with dump (old build) and null (new build): the
// worst null depth over all cases is the number to report.

using namespace tool::pre;

namespace tool
{
namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

std::vector<float> signalOf (int id, size_t n)
{
    if (id == 0) return pinkVec (n, -18.0, 7);
    return sineVec (kFs, binExact (5000.0, kFs, 4096), 0.5, n);
}

struct Case
{
    std::string name;
    PreParams p;
    int signal = 0;
    std::vector<Event> events;
    size_t length = 24000;
};

// Reference cases: every type, both qualities, several drives, tones and mixes, the filters and Weight,
// the level and gain-staging options, and parameter changes in the middle of a stream.
std::vector<Case> buildCases()
{
    std::vector<Case> cs;
    auto add = [&] (std::string name, const PreParams& p, int sig, std::vector<Event> ev = {}, size_t len = 24000)
    {
        cs.push_back (Case { std::move (name), p, sig, std::move (ev), len });
    };
    const float drives[] { 0.0f, 35.0f, 100.0f };
    const float tones[] { 0.0f, -60.0f, 60.0f };
    for (int t = 0; t < shaper::numTypes; ++t)
        for (int hq = 0; hq < 2; ++hq)
        {
            for (float d : drives)
                for (float tn : tones)
                    for (int sig = 0; sig < 2; ++sig)
                    {
                        PreParams p = satParams (t, d, hq == 1, true);
                        p.satTone = tn;
                        add ("sat t" + std::to_string (t) + " hq" + std::to_string (hq) + " d" + std::to_string ((int) d)
                                 + " tone" + std::to_string ((int) tn) + " sig" + std::to_string (sig),
                             p, sig);
                    }
            PreParams m = satParams (t, 35.0f, hq == 1, true);
            m.satMix = 40.0f;
            add ("mix40 t" + std::to_string (t) + " hq" + std::to_string (hq), m, 0);
            PreParams g = satParams (t, 35.0f, hq == 1, false);
            add ("nogain t" + std::to_string (t) + " hq" + std::to_string (hq), g, 0);
            PreParams o = satParams (t, 50.0f, hq == 1, true);
            o.opLevel = -12.0f;
            add ("op-12 t" + std::to_string (t) + " hq" + std::to_string (hq), o, 1);
            PreParams f = satParams (t, 35.0f, hq == 1, true);
            f.hpOn = true; f.hpFreq = 60.0f; f.hpSlope = 3;
            f.lpOn = true; f.lpFreq = 16000.0f; f.lpSlope = 2; f.lpRes = 0.9f;
            f.weightOn = true; f.weightAmt = 1; f.weightFreq = 90.0f;
            add ("full t" + std::to_string (t) + " hq" + std::to_string (hq), f, 0);
        }
    {
        PreParams f = satParams (1, 40.0f, true, true);
        f.hpOn = true; f.hpFreq = 80.0f; f.hpSlope = 1; f.lpOn = true; f.lpFreq = 14000.0f; f.lpSlope = 4;
        f.weightOn = true; f.weightAmt = 2; f.weightTrack = true;
        f.filtTarget = 1; add ("ch0 only, hq", f, 0);
        f.filtTarget = 2; add ("ch1 only, hq", f, 0);
        f.hq = false; f.filtTarget = 0; add ("everything eco", f, 0);
        f.hq = true; f.satOn = false; add ("filters only, hq", f, 0);
        f.hq = false; add ("filters only, eco", f, 0);
    }
    {
        // parameter changes during the stream (applied at control ticks)
        PreParams a = satParams (1, 35.0f, false, true);
        PreParams a2 = a; a2.satType = 2;
        PreParams a3 = a2; a3.drive = 100.0f;
        PreParams a4 = a3; a4.satMix = 30.0f;
        PreParams a5 = a4; a5.hq = true;
        PreParams a6 = a5; a6.satTone = 60.0f;
        PreParams a7 = a6; a7.satOn = false;
        PreParams a8 = a7; a8.satOn = true; a8.satType = 5; a8.hq = false;
        add ("automated eco to hq", a, 1, { { 6000, a2 }, { 12000, a3 }, { 15000, a4 }, { 18000, a5 }, { 21000, a6 }, { 24000, a7 }, { 30000, a8 } }, 43200);
        PreParams b = satParams (3, 60.0f, true, true);
        PreParams b2 = b; b2.hq = false; b2.satType = 6;
        PreParams b3 = b2; b3.satMix = 100.0f; b3.satOn = false; b3.hpOn = true; b3.hpFreq = 120.0f;
        PreParams b4 = b3; b4.satOn = true;
        add ("automated hq to eco", b, 0, { { 9000, b2 }, { 14000, b3 }, { 20000, b4 } }, 43200);
    }
    return cs;
}

std::vector<float> renderCase (const Case& c)
{
    const std::vector<float> x = signalOf (c.signal, c.length);
    if (c.events.empty()) return tool::pre::render (c.p, kFs, x, kBlock);
    return tool::pre::renderAutomated (c.p, kFs, x, kBlock, c.events);
}

// Times PRE alone on a 1 s stereo stream, 64-sample blocks, after a 0.25 s warm-up. Median over `repeats`.
double timePre (const PreParams& p, int repeats)
{
    const auto src = signalOf (0, (size_t) kFs);
    std::vector<double> us;
    for (int k = 0; k < repeats; ++k)
    {
        auto m = std::make_unique<PreModule>();
        m->prepare (kFs, 2048);
        m->setParams (p);
        m->reset();
        std::vector<float> l (kBlock), r (kBlock);
        size_t pos = 0;
        const int warm = (int) (0.25 * kFs / kBlock), blocks = (int) (1.0 * kFs / kBlock);
        auto harness = [&] (int)
        {
            for (int i = 0; i < kBlock; ++i) l[(size_t) i] = r[(size_t) i] = src[(pos + (size_t) i) % src.size()];
            pos = (pos + kBlock) % src.size();
        };
        auto body = [&] (int)
        {
            float* ch[2] { l.data(), r.data() };
            m->process (ch, 2, kBlock);
        };
        us.push_back (timeBlocks (warm, blocks, kBlock / kFs, body, harness).cpuUsPerBlock);
    }
    return medianOf (us);
}

PreParams everythingPre (bool hq)
{
    PreParams p = satParams (1, 40.0f, hq, true);
    p.hpOn = true; p.hpFreq = 60.0f; p.hpSlope = 3;
    p.lpOn = true; p.lpFreq = 16000.0f;
    p.weightOn = true;
    return p;
}

int runTime()
{
    std::printf ("PRE module alone, us per 64-sample block (thread CPU, median of 5 runs of 1 s stereo):\n");
    std::printf ("  %-36s %8.2f us\n", "filters + Weight only (no saturation)", timePre ([] { PreParams p; p.hpOn = true; p.hpFreq = 60.0f; p.hpSlope = 3; p.lpOn = true; p.lpFreq = 16000.0f; p.weightOn = true; return p; }(), 5));
    std::printf ("  %-36s %8.2f us\n", "everything on, Eco (sat Even 40 %)", timePre (everythingPre (false), 5));
    std::printf ("  %-36s %8.2f us\n", "everything on, HQ (sat Even 40 %)", timePre (everythingPre (true), 5));
    for (int t = 0; t < shaper::numTypes; ++t)
    {
        const auto name = std::string ("sat only type ") + std::to_string (t) + " Eco / HQ";
        std::printf ("  %-36s %8.2f us %8.2f us\n", name.c_str(), timePre (satParams (t, 40.0f, false, true), 3), timePre (satParams (t, 40.0f, true, true), 3));
    }
    return 0;
}

// Per-stage CPU of the saturation path. Each stage runs on 32-sample control segments (as SatSection does)
// and is reported per 64-sample block, i.e. twice the per-segment time, for stereo.
int runStages()
{
    using namespace nl::sat;
    const auto src = signalOf (0, (size_t) kFs);
    const size_t segs = src.size() / 32;
    auto seg = [&] (int i) { return src.data() + ((size_t) i % segs) * 32; };
    const int warm = 200, blocks = 4000;
    auto harness = [] (int) {};
    auto report = [&] (const char* name, auto&& body)
    {
        const auto r = timeBlocks (warm, blocks, 32 / kFs, body, harness);
        std::printf ("  %-64s %8.3f us\n", name, 2.0 * r.cpuUsPerBlock);
        return 2.0 * r.cpuUsPerBlock;
    };

    const DroopFir dHq = makeDroopFirZeroPhase (kHqOrder, 4.0 * kFs, kFs, kHqDroopDelay);
    const DroopFir dEco = makeDroopFir (kEcoOrder, kFs, kFs);
    Oversampler4 os, osTwin;
    os.prepare (32, 2);
    osTwin.prepare (32, 2);
    float up[2][128], o4[2][128], lo[2][32], lin4[128], dr[128];
    Ramp ramp;
    for (auto& ch : up) for (int k = 0; k < 128; ++k) ch[k] = 0.3f * std::sin (0.11f * (float) k);
    float droopHist[2][64] {};
    int droopPos[2] { 0, 0 };

    std::printf ("Saturation path, HQ (4x), stereo, us per 64-sample block:\n");
    report ("up 4x (oversampler, both channels)", [&] (int i) { for (int c = 0; c < 2; ++c) os.up (c, seg (i), up[c], 32); });
    report ("droop FIR, 23 taps at 4x (both channels)", [&] (int i) { for (int c = 0; c < 2; ++c) firRing (dHq.h.data(), dHq.taps, droopHist[c], droopPos[c], up[c], dr, 128); (void) i; });
    {
        shaper::Adaa<shaper::BiasedSig> a;
        a.order = 2;
        a.reset();
        report ("ADAA-2 BiasedSig only, 4x (both channels)", [&] (int i) {
            for (int c = 0; c < 2; ++c) for (int k = 0; k < 128; ++k) a.process (up[c][k] + 0.001 * (double) i);
        });
    }
    {
        shaper::Adaa<shaper::Sig> a;
        a.order = 2;
        a.reset();
        report ("ADAA-2 Sig only, 4x (both channels)", [&] (int i) {
            for (int c = 0; c < 2; ++c) for (int k = 0; k < 128; ++k) a.process (up[c][k] + 0.001 * (double) i);
        });
    }
    report ("primitives: 256 x asinh", [&] (int i) { double s = 0; for (int k = 0; k < 256; ++k) s += std::asinh (0.3 + 0.001 * k + 1e-6 * i); volatile double v = s; (void) v; });
    report ("primitives: 256 x log", [&] (int i) { double s = 0; for (int k = 0; k < 256; ++k) s += std::log (1.3 + 0.001 * k + 1e-6 * i); volatile double v = s; (void) v; });
    report ("primitives: 256 x sqrt", [&] (int i) { double s = 0; for (int k = 0; k < 256; ++k) s += std::sqrt (1.3 + 0.001 * k + 1e-6 * i); volatile double v = s; (void) v; });

    static const char* names[] { "Odd", "Even", "Heavy", "Crush", "Tape", "Tube", "Transformer" };
    for (int t = 0; t < shaper::numTypes; ++t)
    {
        Coefs cf;
        cf.configure (t, 4.0 * kFs, kHqOrder, dHq);
        State st[2];
        st[0].reset (cf);
        st[1].reset (cf);
        report ((std::string ("shaper HQ, type ") + names[t] + " (droop+pre+ADAA+post, both ch)").c_str(), [&] (int i) {
            for (int c = 0; c < 2; ++c) process (st[c], cf, up[c], o4[c], 128, ramp);
            (void) i;
        });
    }
    {
        auto down = [&] (int i) { for (int c = 0; c < 2; ++c) os.down (c, o4[c], lo[c], 32); (void) i; };
        report ("down 4x to 1x (both channels)", down);
    }
    {
        // linear twin of the HQ path: droop FIR, ADAA average, decimator
        std::vector<float> hist (64, 0.0f);
        int pos = 0;
        float d1 = 0.0f, d2 = 0.0f;
        report ("linear twin (droop + average + decimator, both ch)", [&] (int i) {
            for (int c = 0; c < 2; ++c)
            {
                firRing (dHq.h.data(), dHq.taps, hist.data(), pos, up[c], dr, 128);
                for (int k = 0; k < 128; ++k) { lin4[k] = (dr[k] + d1 + d2) * (1.0f / 3.0f); d2 = d1; d1 = dr[k]; }
                osTwin.down (c, lin4, lo[c], 32);
            }
            (void) i;
        });
    }
    std::printf ("Saturation path, Eco (1x), stereo, us per 64-sample block:\n");
    {
        const DroopFir d1x = makeDroopFir (kEcoOrder, kFs, kFs);
        for (int t : { 0, 1, 2 })
        {
            Coefs cf;
            cf.configure (t, kFs, kEcoOrder, d1x);
            State st[2];
            st[0].reset (cf);
            st[1].reset (cf);
            float y[32];
            report ((std::string ("shaper Eco, type ") + names[t] + " (droop+pre+ADAA+post, both ch)").c_str(), [&] (int i) {
                for (int c = 0; c < 2; ++c) process (st[c], cf, seg (i), y, 32, ramp);
            });
        }
    }
    std::printf ("Whole module, us per 64-sample block:\n");
    std::printf ("  %-64s %8.3f us\n", "everything on, HQ", timePre (everythingPre (true), 3));
    std::printf ("  %-64s %8.3f us\n", "everything on, Eco", timePre (everythingPre (false), 3));
    return 0;
}

int runDump (const juce::String& path)
{
    const auto cases = buildCases();
    std::ofstream out (path.toStdString(), std::ios::binary);
    if (! out) { std::printf ("cannot write %s\n", path.toRawUTF8()); return 1; }
    const int32_t n = (int32_t) cases.size();
    out.write ((const char*) &n, 4);
    for (const auto& c : cases)
    {
        const auto y = renderCase (c);
        const int32_t nl = (int32_t) c.name.size(), ny = (int32_t) y.size();
        out.write ((const char*) &nl, 4);
        out.write (c.name.data(), nl);
        out.write ((const char*) &ny, 4);
        out.write ((const char*) y.data(), (std::streamsize) (y.size() * sizeof (float)));
    }
    std::printf ("wrote %d cases to %s\n", n, path.toRawUTF8());
    return 0;
}

int runNull (const juce::String& path)
{
    std::ifstream in (path.toStdString(), std::ios::binary);
    if (! in) { std::printf ("cannot read %s\n", path.toRawUTF8()); return 1; }
    const auto cases = buildCases();
    int32_t n = 0;
    in.read ((char*) &n, 4);
    if (n != (int32_t) cases.size()) { std::printf ("case count mismatch: file %d, build %d\n", n, (int) cases.size()); return 1; }
    double worst = -300.0, worstAbs = 0.0;
    std::string worstName;
    int above100 = 0;
    for (int k = 0; k < n; ++k)
    {
        int32_t nl = 0, ny = 0;
        in.read ((char*) &nl, 4);
        std::string name ((size_t) nl, '\0');
        in.read (name.data(), nl);
        in.read ((char*) &ny, 4);
        std::vector<float> ref ((size_t) ny);
        in.read ((char*) ref.data(), (std::streamsize) (ny * sizeof (float)));
        const auto& c = cases[(size_t) k];
        if (name != c.name) { std::printf ("case %d name mismatch: '%s' vs '%s'\n", k, name.c_str(), c.name.c_str()); return 1; }
        const auto y = renderCase (c);
        double e = 0.0, r = 0.0, mx = 0.0;
        for (size_t i = 0; i < ref.size(); ++i)
        {
            const double d = (double) y[i] - (double) ref[i];
            e += d * d;
            r += (double) ref[i] * ref[i];
            mx = std::max (mx, std::abs (d));
        }
        const double nullDb = e > 0.0 ? 10.0 * std::log10 (e / std::max (r, 1.0e-30)) : -400.0;
        if (nullDb > -100.0) ++above100;
        if (nullDb > worst) { worst = nullDb; worstName = name; }
        worstAbs = std::max (worstAbs, mx);
    }
    std::printf ("%d cases, %d with null shallower than -100 dB\n", n, above100);
    std::printf ("worst null depth: %.1f dB (%s); largest sample difference %.3g\n", worst, worstName.c_str(), worstAbs);
    return above100 == 0 ? 0 : 1;
}
} // namespace

int runPreBench (const juce::StringArray& args)
{
    const auto sub = args.size() > 1 ? args[1] : juce::String ("time");
    if (sub == "time") return runTime();
    if (sub == "stages") return runStages();
    if (sub == "dump" && args.size() > 2) return runDump (args[2]);
    if (sub == "null" && args.size() > 2) return runNull (args[2]);
    std::printf ("pre-bench time | stages | dump <file> | null <file>\n");
    return 1;
}
} // namespace tool

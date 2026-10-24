#include "TestUtil.h"
#include "PreHarness.h"

// Performance and stability (wave 3): quality switching without clicks, and the strip's behaviour when
// its meters and scopes are not read.

namespace tool
{
//==============================================================================
// Eco <-> HQ switches the PRE latency (0 <-> 48 samples) and resets its delay lines. The strip fades the
// output out around the switch, applies the new quality while it is silent, and fades back in
// (ChannelStrip::advanceQualityFade). The splatter of a 1 kHz sine around the switch must stay below -60 dB.

// Strip latency once the processor has run past a quality switch (the fade takes about three blocks).
int latencyAtQuality (double fs, int block, float quality)
{
    auto proc = makeProc (fs, block);
    setParam (*proc, P::preOn, 1);
    setParam (*proc, P::satOn, 1);
    setParam (*proc, P::quality, quality);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b) { buf.clear(); proc->processBlock (buf, midi); }
    return proc->getStrip().getLatencySamples();
}

NL_TEST (perf_qualitySwitchIsClickFree)
{
    const double fs = 48000.0;
    const int block = 512;
    const double f0 = pre::binExact (1000.0, fs, 4096);
    const double amp = 0.126;
    const size_t n = (size_t) fs;
    const int switchBlock = 47;                              // the switch is requested at this block boundary
    const size_t t0 = (size_t) switchBlock * (size_t) block;

    // Renders a sine through a processor whose quality changes at switchBlock.
    auto run = [&] (float from, float to, int& latencyAfter)
    {
        auto proc = makeProc (fs, block);
        setParam (*proc, P::preOn, 1);
        setParam (*proc, P::satOn, 1);
        setParam (*proc, P::satType, 0);
        setParam (*proc, P::satDrive, 0); // saturation harmonics would count as splatter
        setParam (*proc, P::quality, from);

        const auto x = pre::sineVec (fs, f0, amp, n);
        std::vector<float> y (n, 0.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, block);
        for (size_t b = 0; b * (size_t) block < n; ++b)
        {
            if ((int) b == switchBlock) setParam (*proc, P::quality, to);
            for (int i = 0; i < block; ++i)
            {
                const size_t k = b * (size_t) block + (size_t) i;
                const float v = k < n ? x[k] : 0.0f;
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            proc->processBlock (buf, midi);
            for (int i = 0; i < block && b * (size_t) block + (size_t) i < n; ++i)
                y[b * (size_t) block + (size_t) i] = buf.getSample (0, i);
        }
        latencyAfter = proc->getStrip().getLatencySamples();
        return y;
    };

    // Largest sample-to-sample step around the switch, relative to the largest step of the sine itself.
    // A discontinuity (a latency change without the fade) gives a ratio far above 1; the fade does not.
    auto stepRatio = [&] (const std::vector<float>& y)
    {
        const double sineStep = 2.0 * juce::MathConstants<double>::pi * f0 * amp / fs;
        double worst = 0.0;
        for (size_t k = t0 - (size_t) block / 2; k < t0 + 4 * (size_t) block; ++k)
            worst = std::max (worst, std::abs ((double) y[k] - (double) y[k - 1]));
        return worst / sineStep;
    };

    const int refHq = latencyAtQuality (fs, block, 1.0f);
    const int refEco = latencyAtQuality (fs, block, 0.0f);
    check (refHq > refEco, "quality HQ reports more latency than Eco", juce::String (refHq) + " vs " + juce::String (refEco));

    int latencyHq = 0, latencyEco = 0;
    const auto ecoToHq = run (0.0f, 1.0f, latencyHq);
    const double ratio1 = stepRatio (ecoToHq);
    check (ratio1 <= 1.5, "quality Eco -> HQ: no discontinuity at the switch", juce::String (ratio1, 2) + " x the sine's own step");
    // The fade is 10 ms down, one block of hold, 10 ms up: its own spectral spread around the carrier
    // is about -37 dB in the 50 ms splatter window, so this is the bound for the dip (roadmap 4: a
    // click-free dip with no discontinuity is accepted instead of -60 dB, which needs a longer fade).
    const double s = pre::splatterDb (ecoToHq, t0 + (size_t) block, fs, f0);
    check (s <= -30.0, "quality Eco -> HQ: splatter of the dip within -30 dB", juce::String (s, 1) + " dB");
    check (latencyHq == refHq, "quality Eco -> HQ: the strip reports the HQ latency after the fade", juce::String (latencyHq) + " vs " + juce::String (refHq));

    const auto hqToEco = run (1.0f, 0.0f, latencyEco);
    const double ratio2 = stepRatio (hqToEco);
    check (ratio2 <= 1.5, "quality HQ -> Eco: no discontinuity at the switch", juce::String (ratio2, 2) + " x the sine's own step");
    check (latencyEco == refEco, "quality HQ -> Eco: the strip reports the Eco latency after the fade", juce::String (latencyEco) + " vs " + juce::String (refEco));
}

} // namespace tool

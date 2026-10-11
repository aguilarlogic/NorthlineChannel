#pragma once

#include "ParamSnapshot.h"
#include "Saturation.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace nl
{
struct PreParams
{
    bool satOn = false, hq = false, hpOn = false, lpOn = false, thumpOn = false;
    int satType = 0, hpSlope = 1, lpSlope = 1, filtTarget = 0, thumpAmt = 0;
    float drive = 30.0f, hpFreq = 80.0f, hpRes = 0.7071f, lpFreq = 16000.0f, lpRes = 0.7071f;
};

// PRE: high-pass -> saturation -> low-pass -> thump. The high-pass sits before
// the shaper so sub-bass does not drive it; the low-pass follows it to tame
// generated harmonics.
class PreModule
{
public:
    // Latency in samples. Must depend only on non-automatable settings (quality,
    // lookahead), never on whether the module is on or on automatable parameters.
    int latency() const noexcept { return 0; }

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        maxBlockSize = maxBlock;
        hp.prepare (fs);
        lp.prepare (fs);
        for (auto* f : { &hpFade, &lpFade, &thumpFade }) f->prepare (fs, 10.0f);
        satFade.prepare (fs, 8.0f);
        driveSm.prepare (fs, 50.0f);
        scratch.assign (2, std::vector<float> ((size_t) maxBlock));

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampler->initProcessing ((size_t) maxBlock);
        reset();
    }

    void reset()
    {
        hp.reset();
        lp.reset();
        for (auto& c : thumpState) { c[0].reset(); c[1].reset(); }
        hpFade.snap (params.hpOn);
        lpFade.snap (params.lpOn);
        thumpFade.snap (params.thumpOn);
        satFade.snap (params.satOn);
        activeType = params.satType;
        activeHQ = params.hq;
        resetShaperState();
        if (oversampler) oversampler->reset();
        driveSm.setTarget ((float) shaper::driveDb (activeType, params.drive));
        driveSm.snap();
        updateDriveGains();
        updateThump();
        counter = 0;
    }

    void setParams (const PreParams& p)
    {
        params = p;
        hp.setParams (true, p.hpFreq, p.hpSlope, p.hpRes);
        lp.setParams (false, p.lpFreq, p.lpSlope, p.lpRes);
        hpFade.setTarget (p.hpOn);
        lpFade.setTarget (p.lpOn);
        thumpFade.setTarget (p.thumpOn);

        const bool needsSwitch = (p.satType != activeType) || (p.hq != activeHQ);
        satFade.setTarget (p.satOn && ! needsSwitch);
        if (needsSwitch && satFade.value == 0.0f)
        {
            activeType = p.satType;
            activeHQ = p.hq;
            resetShaperState();
            if (oversampler) oversampler->reset();
            satFade.setTarget (p.satOn);
        }
        driveSm.setTarget ((float) shaper::driveDb (activeType, p.drive));
        if (p.thumpAmt != thumpAmtActive) { thumpAmtActive = p.thumpAmt; updateThump(); }
    }

    // Latency the HQ oversampler would add (fractional, minimum phase IIR).
    float oversamplerLatency() const { return oversampler ? oversampler->getLatencyInSamples() : 0.0f; }

    void process (float* const* ch, int numCh, int n, const float* const* /*key*/ = nullptr)
    {
        const int start = counter;
        counter += n;
        bool chOn[2] { true, numCh > 1 };
        if (numCh > 1 && params.filtTarget > 0) { chOn[0] = params.filtTarget == 1; chOn[1] = params.filtTarget == 2; }

        // High-pass
        if (! hpFade.isSilent())
            runSection (ch, numCh, n, hpFade, [&] (float* const* io, int nn)
            {
                for (int i = 0; i < nn; ++i)
                {
                    if (((start + i) % kControlBlock) == 0) hp.updateControl();
                    for (int c = 0; c < numCh; ++c)
                        if (chOn[c]) io[c][i] = hp.processSample (c, io[c][i]);
                    hp.advance();
                }
            });
        else
            hp.updateControl();

        // Saturation
        if (! satFade.isSilent())
            runSection (ch, numCh, n, satFade, [&] (float* const* io, int nn) { processShaper (io, numCh, nn, start); });
        else if (params.satType != activeType || params.hq != activeHQ)
            setParams (params); // complete a pending type switch while silent

        // Low-pass
        if (! lpFade.isSilent())
            runSection (ch, numCh, n, lpFade, [&] (float* const* io, int nn)
            {
                for (int i = 0; i < nn; ++i)
                {
                    if (((start + i) % kControlBlock) == 0) lp.updateControl();
                    for (int c = 0; c < numCh; ++c)
                        if (chOn[c]) io[c][i] = lp.processSample (c, io[c][i]);
                    lp.advance();
                }
            });
        else
            lp.updateControl();

        // Thump
        if (! thumpFade.isSilent())
            runSection (ch, numCh, n, thumpFade, [&] (float* const* io, int nn)
            {
                for (int c = 0; c < numCh; ++c)
                {
                    auto& st = thumpState[c];
                    for (int i = 0; i < nn; ++i)
                        io[c][i] = st[1].process (st[0].process (io[c][i], thumpA), thumpB);
                }
            });
    }

    // Combined magnitude of the filter sections for the EQ display.
    float filterMagnitude (double f) const
    {
        float m = 1.0f;
        if (params.hpOn) m *= hp.magnitude (f);
        if (params.lpOn) m *= lp.magnitude (f);
        return m;
    }

private:
    template <typename Fn>
    void runSection (float* const* ch, int numCh, int n, Fade& fade, Fn&& fn)
    {
        if (fade.isFullyOn()) { fn (ch, n); return; }

        float* wet[2] { scratch[0].data(), numCh > 1 ? scratch[1].data() : nullptr };
        for (int c = 0; c < numCh; ++c) std::copy (ch[c], ch[c] + n, wet[c]);
        fn (wet, n);
        for (int i = 0; i < n; ++i)
        {
            const float w = fade.next();
            for (int c = 0; c < numCh; ++c) ch[c][i] += (wet[c][i] - ch[c][i]) * w;
        }
        if (fade.isSilent()) onSectionSilenced (fade);
    }

    void onSectionSilenced (Fade& fade)
    {
        if (&fade == &satFade) { resetShaperState(); if (oversampler) oversampler->reset(); }
        if (&fade == &thumpFade) for (auto& c : thumpState) { c[0].reset(); c[1].reset(); }
    }

    void resetShaperState()
    {
        for (auto& a : adaa) a.reset (activeType);
        const double rate = activeHQ ? fs * 2.0 : fs;
        for (int c = 0; c < 2; ++c)
        {
            dcBlock[c].reset(); dcBlock[c].setCutoff (rate, 7.0f);
            crushPre[c].reset(); crushPre[c].setCutoff (rate, 650.0f);
            crushPost[c].reset(); crushPost[c].setCutoff (rate, 5200.0f);
            heavyPre[c].reset(); heavyPost[c].reset();
        }
        heavyPreC = makeSvf (SvfType::LowShelf, rate, 160.0f, 0.7071f, 5.0f);
        heavyPostC = makeSvf (SvfType::LowShelf, rate, 160.0f, 0.7071f, -5.0f);
    }

    void updateDriveGains()
    {
        gIn = std::pow (10.0, driveSm.current / 20.0);
        gOut = shaper::compensation (activeType, gIn);
    }

    void updateThump()
    {
        const float amt = thumpAmtActive == 0 ? 2.0f : 4.0f;
        thumpA = makeSvf (SvfType::Bell, fs, 72.0f, 0.9f, amt);
        thumpB = makeSvf (SvfType::Bell, fs, 250.0f, 1.2f, -0.35f * amt);
    }

    void processShaper (float* const* io, int numCh, int n, int start)
    {
        // Drive is smoothed at control rate; gains are ramped linearly across the block.
        (void) start;
        prevGIn = gIn;
        prevGOut = gOut;
        for (int i = 0; i < n; i += kControlBlock)
            if (driveSm.tick()) updateDriveGains();

        if (activeHQ && oversampler != nullptr)
        {
            juce::dsp::AudioBlock<float> block (io, (size_t) numCh, (size_t) n);
            auto up = oversampler->processSamplesUp (block);
            float* upCh[2] { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
            shapeBlock (upCh, numCh, (int) up.getNumSamples());
            oversampler->processSamplesDown (block);
        }
        else
        {
            shapeBlock (io, numCh, n);
        }
    }

    void shapeBlock (float* const* io, int numCh, int n)
    {
        const int type = activeType;
        const double inv = 1.0 / std::max (1, n);
        const double dgi = (gIn - prevGIn) * inv, dgo = (gOut - prevGOut) * inv;
        for (int c = 0; c < numCh; ++c)
        {
            float* x = io[c];
            auto& a = adaa[c];
            double gi = prevGIn, go = prevGOut;
            switch (type)
            {
                case shaper::Heavy:
                    for (int i = 0; i < n; ++i, gi += dgi, go += dgo)
                    {
                        const float e = heavyPre[c].process (x[i], heavyPreC);
                        const float y = (float) (a.process (e * gi) * go);
                        x[i] = heavyPost[c].process (y, heavyPostC);
                    }
                    break;
                case shaper::Crush:
                    for (int i = 0; i < n; ++i, gi += dgi, go += dgo)
                    {
                        const float mid = crushPre[c].highpass (x[i]) + 0.25f * x[i];
                        const float y = (float) (a.process (mid * gi) * go);
                        x[i] = dcBlock[c].highpass (crushPost[c].lowpass (y));
                    }
                    break;
                case shaper::Even:
                    for (int i = 0; i < n; ++i, gi += dgi, go += dgo)
                        x[i] = dcBlock[c].highpass ((float) (a.process (x[i] * gi) * go));
                    break;
                case shaper::Odd:
                default:
                    for (int i = 0; i < n; ++i, gi += dgi, go += dgo)
                        x[i] = (float) (a.process (x[i] * gi) * go);
                    break;
            }
        }
    }

    double fs = 48000.0;
    int maxBlockSize = 512, counter = 0;
    PreParams params;
    SlopeFilter hp, lp;
    Fade hpFade, lpFade, satFade, thumpFade;
    Smoothed driveSm;
    double gIn = 1.0, gOut = 1.0, prevGIn = 1.0, prevGOut = 1.0;
    int activeType = 0, thumpAmtActive = 0;
    bool activeHQ = false;
    shaper::Adaa1 adaa[2];
    OnePole dcBlock[2], crushPre[2], crushPost[2];
    SvfState heavyPre[2], heavyPost[2];
    SvfCoefs heavyPreC, heavyPostC, thumpA, thumpB;
    SvfState thumpState[2][2];
    std::vector<std::vector<float>> scratch;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
};
// Maps the parameter snapshot to PRE settings (owned by this module).
inline PreParams readPreParams (const ParamSnapshot& p)
{
    PreParams pp;
    pp.satOn = p.on (P::satOn);
    pp.satType = std::min (p.choice (P::satType), 3); // TODO(pre): Tape / Tube / Transformer
    pp.drive = p.f (P::satDrive);
    pp.hq = p.choice (P::quality) == 1;
    pp.hpOn = p.on (P::hpOn); pp.hpFreq = p.f (P::hpFreq); pp.hpSlope = std::min (p.choice (P::hpSlope), 3); pp.hpRes = p.f (P::hpRes);
    pp.lpOn = p.on (P::lpOn); pp.lpFreq = p.f (P::lpFreq); pp.lpSlope = std::min (p.choice (P::lpSlope), 3); pp.lpRes = p.f (P::lpRes);
    pp.filtTarget = p.choice (P::preMode) == 1 ? p.choice (P::filtTarget) : 0;
    pp.thumpOn = p.on (P::weightOn);
    pp.thumpAmt = std::min (p.choice (P::weightAmt), 1);
    return pp;
}
} // namespace nl

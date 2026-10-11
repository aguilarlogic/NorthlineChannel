#include "ChannelStrip.h"

namespace nl
{
ChannelStrip::ChannelStrip (juce::AudioProcessorValueTreeState& state)
{
    for (int i = 0; i < P::count; ++i)
    {
        values[(size_t) i] = state.getRawParameterValue (paramID (i));
        jassert (values[(size_t) i] != nullptr);
    }
    packedOrder.store (packOrder (defaultOrder));
    ticksToUs = 1.0e6 / (double) juce::Time::getHighResolutionTicksPerSecond();
}

void ChannelStrip::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    channels = juce::jlimit (1, 2, numChannels);

    pre.prepare (fs, maxBlock);
    gate.prepare (fs);
    ds.prepare (fs);
    eq.prepare (fs);
    comp.prepare (fs);
    limiter.prepare (fs);

    for (auto& f : moduleFade) f.prepare (fs, 10.0f);
    limFade.prepare (fs, 10.0f);
    inGain.prepare (fs, 20.0f);
    outGain.prepare (fs, 20.0f);
    matchGain.prepare (fs, 200.0f);
    matchCoef = std::exp (-1.0 / (1.5 * fs));

    for (int c = 0; c < 2; ++c)
    {
        dry[c].assign ((size_t) maxBlock, 0.0f);
        key[c].assign ((size_t) maxBlock, 0.0f);
        keyMs[c].assign ((size_t) maxBlock, 0.0f);
    }
    sampleScratch.assign ((size_t) maxBlock, StereoSample {});
    frames.allocate (256);
    samples.allocate ((size_t) (fs * 0.75));

    readParams();
    reset();
}

void ChannelStrip::reset()
{
    readParams();
    pre.reset();
    gate.reset();
    ds.reset();
    eq.reset();
    comp.reset();
    limiter.reset();
    for (int m = 0; m < numModules; ++m) moduleFade[(size_t) m].snap (moduleEnabled (m));
    limFade.snap (pb (P::limOn));
    inGain.snap();
    outGain.snap();
    matchGain.setTarget (1.0f);
    matchGain.snap();
    matchInPow = matchOutPow = 0.0;
}

int ChannelStrip::getLatencySamples() const noexcept
{
    return limiter.latencySamples (pb (P::limOn) && pb (P::limLookahead));
}

//==============================================================================
uint32_t ChannelStrip::packOrder (const Order& order) noexcept
{
    uint32_t v = 0;
    for (int i = 0; i < numModules; ++i) v |= (uint32_t) order[(size_t) i] << (3 * i);
    return v;
}

ChannelStrip::Order ChannelStrip::unpackOrder (uint32_t packed) noexcept
{
    Order o {};
    for (int i = 0; i < numModules; ++i) o[(size_t) i] = (int) ((packed >> (3 * i)) & 7u);
    return o;
}

bool ChannelStrip::isValidOrder (const Order& order) noexcept
{
    bool seen[numModules] {};
    for (int id : order)
    {
        if (id < 0 || id >= numModules || seen[id]) return false;
        seen[id] = true;
    }
    return true;
}

void ChannelStrip::setOrder (const Order& order) noexcept
{
    if (isValidOrder (order)) packedOrder.store (packOrder (order));
}

ChannelStrip::Order ChannelStrip::getOrder() const noexcept { return unpackOrder (packedOrder.load()); }

//==============================================================================
bool ChannelStrip::moduleEnabled (int id) const noexcept
{
    switch (id)
    {
        case modPre:  return pb (P::preOn);
        case modGate: return pb (P::gateOn);
        case modDs:   return pb (P::dsOn);
        case modEq:   return pb (P::eqOn);
        case modComp: return pb (P::compOn);
        default:      return false;
    }
}

bool ChannelStrip::moduleWantsMs (int id) const noexcept
{
    if (channels < 2) return false;
    switch (id)
    {
        case modPre:  return pi (P::preMode) == 1;
        case modGate: return pi (P::gateMode) == 2;
        case modDs:   return pi (P::dsMode) == 2;
        case modEq:   return pi (P::eqMode) == 1;
        case modComp: return pi (P::compMode) == 2;
        default:      return false;
    }
}

void ChannelStrip::readParams()
{
    PreParams pp;
    pp.satOn = pb (P::satOn); pp.satType = pi (P::satType); pp.drive = pv (P::satDrive); pp.hq = pb (P::satHQ);
    pp.hpOn = pb (P::hpOn); pp.hpFreq = pv (P::hpFreq); pp.hpSlope = pi (P::hpSlope); pp.hpRes = pv (P::hpRes);
    pp.lpOn = pb (P::lpOn); pp.lpFreq = pv (P::lpFreq); pp.lpSlope = pi (P::lpSlope); pp.lpRes = pv (P::lpRes);
    pp.filtTarget = pi (P::preMode) == 1 ? pi (P::filtTarget) : 0;
    pp.thumpOn = pb (P::thumpOn); pp.thumpAmt = pi (P::thumpAmt);
    pre.setParams (pp);

    GateParams gp;
    gp.type = pi (P::gateType); gp.linked = pi (P::gateMode) == 0; gp.ext = pb (P::gateExt); gp.listen = pb (P::gateListen);
    gp.thresh = pv (P::gateThresh); gp.hyst = pv (P::gateHyst); gp.floorDb = pv (P::gateFloor); gp.ratio = pv (P::gateRatio);
    gp.attackMs = pv (P::gateAttack); gp.holdMs = pv (P::gateHold); gp.releaseMs = pv (P::gateRelease);
    gp.scHp = pv (P::gateScHp); gp.scLp = pv (P::gateScLp);
    gate.setParams (gp);

    DsParams dp;
    dp.linked = pi (P::dsMode) == 0; dp.relative = pb (P::dsRelative); dp.ext = pb (P::dsExt);
    dp.listen = pi (P::dsListen); dp.releaseMs = pv (P::dsRelease); dp.outDb = pv (P::dsOut);
    for (int b = 0; b < 2; ++b)
    {
        const int o = b == 0 ? P::ds1On : P::ds2On;
        dp.band[b].on = pb (o);
        dp.band[b].type = pi (o + 1);
        dp.band[b].freq = pv (o + 2);
        dp.band[b].thresh = pv (o + 3);
        dp.band[b].range = pv (o + 4);
    }
    ds.setParams (dp);

    EqParams ep;
    for (int b = 0; b < 4; ++b)
    {
        const int o = P::eqLowOn + b * 6;
        ep.band[b].on = pb (o);
        ep.band[b].type = pi (o + 1);
        ep.band[b].freq = pv (o + 2);
        ep.band[b].gain = pv (o + 3);
        ep.band[b].q = pv (o + 4);
        ep.band[b].target = pi (P::eqMode) == 1 ? pi (o + 5) : 0;
    }
    eq.setParams (ep);

    CompParams cp;
    cp.type = pi (P::compType); cp.linked = pi (P::compMode) == 0; cp.makeup = pb (P::compMakeup);
    cp.ext = pb (P::compExt); cp.listen = pb (P::compListen);
    cp.thresh = pv (P::compThresh); cp.ratio = pv (P::compRatio); cp.attackMs = pv (P::compAttack);
    cp.releaseMs = pv (P::compRelease); cp.mix = pv (P::compMix) * 0.01f; cp.outDb = pv (P::compOut);
    cp.scHp = pv (P::compScHp); cp.scBellFreq = pv (P::compScBellFreq); cp.scBellGain = pv (P::compScBellGain);
    comp.setParams (cp);

    limiter.setParams (pv (P::limCeiling), pv (P::limRelease), pb (P::limLookahead));
    limFade.setTarget (pb (P::limOn));

    float inG = dbToGain (pv (P::inGain));
    if (pb (P::inPhase)) inG = -inG;
    inGain.setTarget (inG);
    outGain.setTarget (dbToGain (pv (P::outGain)));
}

void ChannelStrip::resetModule (int id)
{
    switch (id)
    {
        case modPre:  pre.reset(); break;
        case modGate: gate.reset(); break;
        case modDs:   ds.reset(); break;
        case modEq:   eq.reset(); break;
        case modComp: comp.reset(); break;
        default: break;
    }
}

void ChannelStrip::runModule (int id, float* const* io, int numChannels, int n, const float* const* k)
{
    switch (id)
    {
        case modPre:  pre.process (io, numChannels, n); break;
        case modGate: gate.process (io, numChannels, n, k); break;
        case modDs:   ds.process (io, numChannels, n, k); break;
        case modEq:   eq.process (io, numChannels, n); break;
        case modComp: comp.process (io, numChannels, n, k); break;
        default: break;
    }
}

//==============================================================================
void ChannelStrip::process (float* const* io, int numChannels, int n, const float* const* sidechain, int sidechainChannels)
{
    jassert (n <= maxBlock);
    const auto tStart = juce::Time::getHighResolutionTicks();
    numChannels = juce::jlimit (1, 2, numChannels);
    readParams();

    MeterFrame frame;
    frame.numSamples = n;
    frame.blockUs = (float) (1.0e6 * n / fs);

    // Input stage: gain / polarity, ramped across the block.
    {
        const float g0 = inGain.current;
        for (int i = 0; i < n; i += kControlBlock) inGain.tick();
        const float g1 = inGain.current, dg = (g1 - g0) / (float) n;
        for (int c = 0; c < numChannels; ++c)
        {
            float g = g0, pk = 0.0f, ms = 0.0f;
            float* x = io[c];
            if (pb (P::levelMatch))
                for (int i = 0; i < n; ++i) matchInPow = matchInPow * matchCoef + (double) x[i] * x[i];
            for (int i = 0; i < n; ++i, g += dg)
            {
                x[i] *= g;
                pk = std::max (pk, std::abs (x[i]));
                ms += x[i] * x[i];
            }
            frame.inPeak[c] = pk;
            frame.inMs[c] = ms / (float) n;
        }
    }

    // External key, converted lazily to M/S if a module needs it.
    const float* keyLr[2] { nullptr, nullptr };
    const float* keyMsPtr[2] { nullptr, nullptr };
    bool keyMsReady = false;
    if (sidechain != nullptr && sidechainChannels > 0)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = sidechain[std::min (c, sidechainChannels - 1)];
            std::copy (src, src + n, key[c].data());
            keyLr[c] = key[c].data();
        }
    }

    // Module chain in the user's order. Disabled modules cost nothing.
    const auto order = getOrder();
    bool inMs = false;
    for (int slot = 0; slot < numModules; ++slot)
    {
        const int id = order[(size_t) slot];
        auto& fade = moduleFade[(size_t) id];
        const bool wasSilent = fade.isSilent();
        fade.setTarget (moduleEnabled (id));
        if (fade.isSilent()) { moduleUsSmoothed[(size_t) id] *= 0.9f; continue; }
        if (wasSilent) resetModule (id);

        const auto t0 = juce::Time::getHighResolutionTicks();
        const bool wantMs = moduleWantsMs (id);
        if (wantMs != inMs && numChannels == 2) { lrToMs (io[0], io[1], n); inMs = wantMs; }

        const float* const* k = nullptr;
        if (keyLr[0] != nullptr)
        {
            if (wantMs && numChannels == 2)
            {
                if (! keyMsReady)
                {
                    for (int c = 0; c < 2; ++c) std::copy (keyLr[c], keyLr[c] + n, keyMs[c].data());
                    lrToMs (keyMs[0].data(), keyMs[1].data(), n);
                    keyMsPtr[0] = keyMs[0].data();
                    keyMsPtr[1] = keyMs[1].data();
                    keyMsReady = true;
                }
                k = keyMsPtr;
            }
            else
            {
                k = keyLr;
            }
        }

        if (fade.isFullyOn())
        {
            runModule (id, io, numChannels, n, k);
        }
        else
        {
            for (int c = 0; c < numChannels; ++c) std::copy (io[c], io[c] + n, dry[c].data());
            runModule (id, io, numChannels, n, k);
            for (int i = 0; i < n; ++i)
            {
                const float w = fade.next();
                for (int c = 0; c < numChannels; ++c) io[c][i] = dry[c][i] + (io[c][i] - dry[c][i]) * w;
            }
            if (fade.isSilent()) resetModule (id);
        }

        const float us = (float) ((double) (juce::Time::getHighResolutionTicks() - t0) * ticksToUs);
        moduleUsSmoothed[(size_t) id] = 0.9f * moduleUsSmoothed[(size_t) id] + 0.1f * us;
    }
    if (inMs) lrToMs (io[0], io[1], n);

    // Master: output gain -> level match -> limiter -> monitor.
    const auto tMaster = juce::Time::getHighResolutionTicks();
    {
        const float g0 = outGain.current;
        for (int i = 0; i < n; i += kControlBlock) outGain.tick();
        const float g1 = outGain.current, dg = (g1 - g0) / (float) n;
        for (int c = 0; c < numChannels; ++c)
        {
            float g = g0;
            for (int i = 0; i < n; ++i, g += dg) io[c][i] *= g;
        }
    }

    if (pb (P::levelMatch))
    {
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < n; ++i) matchOutPow = matchOutPow * matchCoef + (double) io[c][i] * io[c][i];
        const double ratio = matchOutPow > 1.0e-12 ? std::sqrt (matchInPow / matchOutPow) : 1.0;
        matchGain.setTarget ((float) std::clamp (ratio, 0.0631, 15.85)); // +/-24 dB
    }
    else
    {
        matchGain.setTarget (1.0f);
        matchInPow = matchOutPow = 0.0;
    }
    {
        const float g0 = matchGain.current;
        for (int i = 0; i < n; i += kControlBlock) matchGain.tick();
        const float g1 = matchGain.current;
        if (g0 != 1.0f || g1 != 1.0f)
        {
            const float dg = (g1 - g0) / (float) n;
            for (int c = 0; c < numChannels; ++c)
            {
                float g = g0;
                for (int i = 0; i < n; ++i, g += dg) io[c][i] *= g;
            }
        }
    }

    const bool lookaheadNow = pb (P::limLookahead);
    if (pb (P::limOn) || ! limFade.isSilent())
    {
        if (lookaheadNow)
        {
            // Lookahead changes latency, so no cross-fade: the host re-aligns.
            limFade.snap (pb (P::limOn));
            if (limFade.isFullyOn()) limiter.process (io, numChannels, n);
        }
        else if (limFade.isFullyOn())
        {
            limiter.process (io, numChannels, n);
        }
        else
        {
            for (int c = 0; c < numChannels; ++c) std::copy (io[c], io[c] + n, dry[c].data());
            limiter.process (io, numChannels, n);
            for (int i = 0; i < n; ++i)
            {
                const float w = limFade.next();
                for (int c = 0; c < numChannels; ++c) io[c][i] = dry[c][i] + (io[c][i] - dry[c][i]) * w;
            }
        }
        frame.limGr = limiter.getGainReductionDb();
    }
    limLookaheadActive = lookaheadNow;

    if (numChannels == 2)
    {
        float* l = io[0];
        float* r = io[1];
        switch (pi (P::monitor))
        {
            case 1: for (int i = 0; i < n; ++i) { const float m = 0.5f * (l[i] + r[i]); l[i] = r[i] = m; } break;
            case 2: std::copy (l, l + n, r); break;
            case 3: std::copy (r, r + n, l); break;
            case 4: for (int i = 0; i < n; ++i) { const float m = 0.5f * (l[i] + r[i]); l[i] = r[i] = m; } break;
            case 5: for (int i = 0; i < n; ++i) { const float s = 0.5f * (l[i] - r[i]); l[i] = r[i] = s; } break;
            default: break;
        }
    }

    // Output meters and sample stream.
    for (int c = 0; c < numChannels; ++c)
    {
        float pk = 0.0f, ms = 0.0f;
        for (int i = 0; i < n; ++i) { pk = std::max (pk, std::abs (io[c][i])); ms += io[c][i] * io[c][i]; }
        frame.outPeak[c] = pk;
        frame.outMs[c] = ms / (float) n;
    }
    if (numChannels == 1) { frame.inPeak[1] = frame.inPeak[0]; frame.outPeak[1] = frame.outPeak[0]; frame.inMs[1] = frame.inMs[0]; frame.outMs[1] = frame.outMs[0]; }

    if (streamSamples.load (std::memory_order_relaxed))
    {
        for (int i = 0; i < n; ++i) sampleScratch[(size_t) i] = { io[0][i], numChannels > 1 ? io[1][i] : io[0][i] };
        samples.push (sampleScratch.data(), (size_t) n);
    }

    const auto tEnd = juce::Time::getHighResolutionTicks();
    moduleUsSmoothed[5] = 0.9f * moduleUsSmoothed[5] + 0.1f * (float) ((double) (tEnd - tMaster) * ticksToUs);

    frame.gateGr = moduleFade[modGate].isSilent() ? 0.0f : gate.getGainReductionDb();
    frame.ds1Gr = moduleFade[modDs].isSilent() ? 0.0f : ds.getGainReductionDb (0);
    frame.ds2Gr = moduleFade[modDs].isSilent() ? 0.0f : ds.getGainReductionDb (1);
    frame.compGr = moduleFade[modComp].isSilent() ? 0.0f : comp.getGainReductionDb();
    for (int m = 0; m < 6; ++m) frame.moduleUs[m] = moduleUsSmoothed[(size_t) m];
    frame.totalUs = (float) ((double) (tEnd - tStart) * ticksToUs);
    frames.push (frame);
}
} // namespace nl

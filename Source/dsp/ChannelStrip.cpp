#include "ChannelStrip.h"

namespace nl
{
namespace
{
// Contiguous parameter blocks. A module's settings may depend only on its own block plus the
// global quality and op_level parameters; the strip calls setParams only when one of those changed.
struct Range { int first, last; };
constexpr Range kPreRange { P::preOn, P::weightTrack };
constexpr Range kGateRange { P::gateOn, P::gateLookahead };
constexpr Range kDsRange { P::dsOn, P::ds2Range };
constexpr Range kEqRange { P::eqOn, P::eqHighDynExt };
constexpr Range kCompRange { P::compOn, P::compLookahead };
constexpr Range kLimiterRange { P::limOn, P::limRelease };
constexpr Range kInputRange { P::inGain, P::globalMix }; // input stage, output gain, mix

constexpr double kMaxRingSampleRate = 384000.0;
} // namespace

ChannelStrip::ChannelStrip (juce::AudioProcessorValueTreeState& state)
{
    for (int i = 0; i < P::count; ++i)
    {
        values[(size_t) i] = state.getRawParameterValue (paramID (i));
        jassert (values[(size_t) i] != nullptr);
    }
    packedOrder.store (packOrder (defaultOrder));
    ticksToUs = 1.0e6 / (double) juce::Time::getHighResolutionTicksPerSecond();

    // The rings are sized once for the worst case, so prepare() never reallocates under a live consumer.
    frames.allocate (256);
    samples.allocate ((size_t) (kMaxRingSampleRate * 0.75));
}

void ChannelStrip::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    channels = juce::jlimit (1, 2, numChannels);

    pre.prepare (fs, maxBlock);
    gate.prepare (fs, maxBlock);
    ds.prepare (fs, maxBlock);
    eq.prepare (fs, maxBlock);
    comp.prepare (fs, maxBlock);
    master.prepare (fs, maxBlock);

    for (auto& f : moduleFade) f.prepare (fs, 10.0f);
    for (auto& g : inGain) g.prepare (fs, 20.0f);
    outGain.prepare (fs, 20.0f);
    matchGain.prepare (fs, 50.0f);
    mixAmt.prepare (fs, 20.0f);
    swapAmt.prepare (fs, 10.0f);
    monitor.prepare (fs, 10.0f);
    levelMatch.prepare (fs);

    // Compare and Delta (9.3).
    wholeW.prepare (fs, 20.0f);
    modW.prepare (fs, 20.0f);
    modGainV.prepare (fs, 20.0f);
    for (auto& d : deltaW) d.prepare (fs, 20.0f);
    cmpOutGain.prepare (fs, 20.0f);
    learnAlpha = timeCoef (300.0f, fs);

    const int maxLat = maxModuleLatency (fs);
    for (int m = 0; m < numModules; ++m)
    {
        bypassDelay[(size_t) m].prepare (2, maxLat);
        if (testExtra[(size_t) m] > 0) testWet[(size_t) m].prepare (2, maxLat);
    }
    dryDelay.prepare (2, maxLat * numModules);
    cmpDelay.prepare (2, maxLat * (numModules + 1));

    for (int c = 0; c < 2; ++c)
    {
        fadeDry[c].assign ((size_t) maxBlock, 0.0f);
        tapDry[c].assign ((size_t) maxBlock, 0.0f);
        key[c].assign ((size_t) maxBlock, 0.0f);
        keyMs[c].assign ((size_t) maxBlock, 0.0f);
        cmpBuf[c].assign ((size_t) maxBlock, 0.0f);
        dryScratch[c].assign ((size_t) maxBlock, 0.0f);
    }
    modGainBuf.assign ((size_t) maxBlock, 1.0f);
    sampleScratch.assign ((size_t) maxBlock, StereoSample {});

    // Consumers drop whatever the rings still hold from before this prepare.
    ringGeneration.fetch_add (1, std::memory_order_release);

    haveSnap = false;
    reset();
}

void ChannelStrip::reset()
{
    updateParams (true);
    clock.reset();

    pre.reset();
    gate.reset();
    ds.reset();
    eq.reset();
    comp.reset();
    master.reset (snap);

    for (int m = 0; m < numModules; ++m)
    {
        moduleFade[(size_t) m].snap (moduleEnabled (m));
        holdoff[(size_t) m] = 0;
        bypassDelay[(size_t) m].reset();
        testWet[(size_t) m].reset();
    }
    dryDelay.reset();

    for (auto& g : inGain) g.snap();
    outGain.snap();
    swapAmt.snap();
    mixAmt.snap();

    // Compare and Delta: ramps settle on their targets, learned levels start again.
    wholeW.snap();
    modW.snap();
    modGainV.snap();
    for (auto& d : deltaW) d.snap();
    cmpOutGain.snap();
    cmpDelay.reset();
    for (auto& l : learn) l = ModuleLearn {};

    levelMatch.resetMeasurement();
    levelMatch.update (pi (P::matchMode), pv (P::matchTrim));
    lastMatchDb = levelMatch.gainDb();
    matchGain.setTarget (dbToGain (lastMatchDb));
    matchGain.snap();

    monitor.snap (pi (P::monitor));
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
size_t ChannelStrip::popMeterFrames (MeterFrame* out, size_t maxCount, bool* wasReset) noexcept
{
    if (wasReset != nullptr) *wasReset = false;
    if (maxCount == 0) return 0;
    const auto generation = ringGeneration.load (std::memory_order_acquire);
    if (generation != seenFrameGeneration)
    {
        seenFrameGeneration = generation;
        while (frames.pop (out, maxCount) == maxCount) {}
        if (wasReset != nullptr) *wasReset = true;
        return 0;
    }
    return frames.pop (out, maxCount);
}

size_t ChannelStrip::popOutputSamples (StereoSample* out, size_t maxCount, bool* wasReset) noexcept
{
    if (wasReset != nullptr) *wasReset = false;
    if (maxCount == 0) return 0;
    const auto generation = ringGeneration.load (std::memory_order_acquire);
    if (generation != seenSampleGeneration)
    {
        seenSampleGeneration = generation;
        while (samples.pop (out, maxCount) == maxCount) {}
        if (wasReset != nullptr) *wasReset = true;
        return 0;
    }
    return samples.pop (out, maxCount);
}

void ChannelStrip::setTestModuleLatency (int moduleId, int extraSamples)
{
    jassert (moduleId >= 0 && moduleId < numModules);
    testExtra[(size_t) moduleId] = std::clamp (extraSamples, 0, maxModuleLatency (fs));
    if (testExtra[(size_t) moduleId] > 0) testWet[(size_t) moduleId].prepare (2, maxModuleLatency (fs));
    refreshLatency();
}

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

void ChannelStrip::updateParams (bool force)
{
    for (int i = 0; i < P::count; ++i) snap.v[(size_t) i] = values[(size_t) i]->load (std::memory_order_relaxed);
    if (! haveSnap || forceFull.load (std::memory_order_relaxed)) force = true;

    const auto changed = [&] (Range r)
    {
        return force || std::memcmp (&snap.v[(size_t) r.first], &prevSnap.v[(size_t) r.first],
                                     (size_t) (r.last - r.first + 1) * sizeof (float)) != 0;
    };
    const bool globals = changed ({ P::quality, P::quality }) || changed ({ P::opLevel, P::opLevel });

    bool latencyMayChange = false;
    const auto update = [&] (int index, Range r, auto&& apply)
    {
        if (! (globals || changed (r))) return;
        apply();
        paramUpdates[(size_t) index].fetch_add (1, std::memory_order_relaxed);
        latencyMayChange = true;
    };
    update (modPre, kPreRange, [&] { pre.setParams (readPreParams (snap)); });
    update (modGate, kGateRange, [&] { gate.setParams (readGateParams (snap)); });
    update (modDs, kDsRange, [&] { ds.setParams (readDsParams (snap)); });
    update (modEq, kEqRange, [&] { eq.setParams (readEqParams (snap)); });
    update (modComp, kCompRange, [&] { comp.setParams (readCompParams (snap)); });
    if (globals || changed (kLimiterRange) || changed ({ P::vuSource, P::vuSource }))
    {
        master.setParams (snap);
        paramUpdates[numModules].fetch_add (1, std::memory_order_relaxed);
        latencyMayChange = true;
    }

    // Strip-level controls. Everything ramps at control rate (deterministic across block sizes).
    if (changed (kInputRange))
    {
        const float g = dbToGain (pv (P::inGain));
        const int pol = pi (P::inPolarity);
        inGain[0].setTarget ((pol == 1 || pol == 3) ? -g : g);
        inGain[1].setTarget ((pol == 2 || pol == 3) ? -g : g);
        swapAmt.setTarget (pb (P::inSwap) ? 1.0f : 0.0f);
        outGain.setTarget (dbToGain (pv (P::outGain)));
        cmpOutGain.setTarget (dbToGain (pv (P::outGain)));
        mixAmt.setTarget (std::clamp (pv (P::globalMix) * 0.01f, 0.0f, 1.0f));
    }

    if (latencyMayChange) refreshLatency();
    prevSnap = snap;
    haveSnap = true;
}

void ChannelStrip::refreshLatency()
{
    const int cap = maxModuleLatency (fs);
    const int raw[numModules] { pre.latency(), gate.latency(), ds.latency(), eq.latency(), comp.latency() };
    int chain = 0;
    for (int m = 0; m < numModules; ++m)
    {
        jassert (raw[m] >= 0 && raw[m] + testExtra[(size_t) m] <= cap);
        moduleLat[(size_t) m] = std::clamp (raw[m] + testExtra[(size_t) m], 0, cap);
        chain += moduleLat[(size_t) m];
    }
    latencyChain.store (chain, std::memory_order_relaxed);
    latencyTotal.store (chain + std::max (0, master.latency()), std::memory_order_relaxed);
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
    testWet[(size_t) id].reset();
}

void ChannelStrip::runModule (int id, float* const* io, int numChannels, int n, const float* const* k)
{
    switch (id)
    {
        case modPre:  pre.process (io, numChannels, n, k); break;
        case modGate: gate.process (io, numChannels, n, k); break;
        case modDs:   ds.process (io, numChannels, n, k); break;
        case modEq:   eq.process (io, numChannels, n, k); break;
        case modComp: comp.process (io, numChannels, n, k); break;
        default: break;
    }
    if (testExtra[(size_t) id] > 0)
    {
        testWet[(size_t) id].setDelay (testExtra[(size_t) id]);
        testWet[(size_t) id].process (io, io, numChannels, n);
    }
}

//==============================================================================
std::array<float, ChannelStrip::Monitor::kMatrixSize> ChannelStrip::Monitor::matrixFor (int mode) noexcept
{
    // out L = a L + b R, out R = c L + d R
    switch (mode)
    {
        case 1:  return { 0.5f, 0.5f, 0.5f, 0.5f };   // Mono
        case 2:  return { 1.0f, 0.0f, 1.0f, 0.0f };   // Left
        case 3:  return { 0.0f, 1.0f, 0.0f, 1.0f };   // Right
        case 4:  return { 0.5f, -0.5f, 0.5f, -0.5f }; // Side: 0.5 (L - R)
        case 5:  return { 0.0f, 1.0f, 1.0f, 0.0f };   // Swap
        default: return { 1.0f, 0.0f, 0.0f, 1.0f };   // Stereo
    }
}

void ChannelStrip::Monitor::set (int newMode) noexcept
{
    if (newMode == mode) return;
    // Start the new ramp from where the running one is now.
    if (t >= 1.0f) from = to;
    else for (size_t k = 0; k < kMatrixSize; ++k) from[k] = from[k] + (to[k] - from[k]) * t;
    to = matrixFor (newMode);
    t = 0.0f;
    mode = newMode;
}

void ChannelStrip::Monitor::process (float* l, float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (t < 1.0f) t = std::min (1.0f, t + step);
        float m[kMatrixSize];
        for (size_t k = 0; k < kMatrixSize; ++k) m[k] = t >= 1.0f ? to[k] : from[k] + (to[k] - from[k]) * t;
        const float a = l[i], b = r[i];
        l[i] = m[0] * a + m[1] * b;
        r[i] = m[2] * a + m[3] * b;
    }
}

//==============================================================================
void ChannelStrip::inputStage (float* const* io, int numCh, int n)
{
    const uint32_t phase = clock.phase;

    if (numCh == 2)
    {
        if (swapAmt.isConstant())
        {
            if (swapAmt.target() >= 1.0f) std::swap_ranges (io[0], io[0] + n, io[1]);
        }
        else
        {
            swapAmt.forEachSegment (n, phase, [&] (int s, int len, float base, float slope, int offset)
            {
                float* l = io[0] + s;
                float* r = io[1] + s;
                for (int j = 0; j < len; ++j)
                {
                    const float m = base + slope * (float) (offset + j), a = l[j], b = r[j];
                    l[j] = a * (1.0f - m) + b * m;
                    r[j] = b * (1.0f - m) + a * m;
                }
            });
        }
    }

    for (int c = 0; c < numCh; ++c)
        if (! inGain[(size_t) c].isUnity()) inGain[(size_t) c].apply (io + c, 1, n, phase);
}

void ChannelStrip::matchStage (float* const* io, int numCh, int n)
{
    const uint32_t phase = clock.phase;
    bool measuring = levelMatch.isMeasuring();
    int done = 0;
    while (done < n)
    {
        int seg = n - done;
        if (measuring) seg = std::min (seg, levelMatch.samplesToHop());

        float* out[2] { io[0] + done, numCh > 1 ? io[1] + done : nullptr };
        bool hopFinished = false;
        if (measuring)
        {
            const float* ref[2] { tapDry[0].data() + done, tapDry[1].data() + done };
            hopFinished = levelMatch.process (ref, out, numCh, seg);
        }
        matchGain.apply (out, numCh, seg, phase + (uint32_t) done);

        if (hopFinished)
        {
            measuring = levelMatch.isMeasuring();
            const float db = levelMatch.gainDb();
            if (db != lastMatchDb) { lastMatchDb = db; matchGain.setTarget (dbToGain (db)); }
        }
        done += seg;
    }
}

void ChannelStrip::mixStage (float* const* io, int numCh, int n)
{
    mixAmt.forEachSegment (n, clock.phase, [&] (int s, int len, float base, float slope, int offset)
    {
        if (base == 1.0f && slope == 0.0f) return;
        for (int c = 0; c < numCh; ++c)
        {
            float* wet = io[c] + s;
            const float* dry = tapDry[(size_t) c].data() + s;
            for (int j = 0; j < len; ++j)
            {
                const float m = base + slope * (float) (offset + j);
                wet[j] = dry[j] + (wet[j] - dry[j]) * m;
            }
        }
    });
}

//==============================================================================
//==============================================================================
// Compare and Delta (9.3)

void ChannelStrip::setDelta (int moduleId, bool on) noexcept
{
    if (moduleId != modPre && moduleId != modGate && moduleId != modEq) return;
    // The message thread is the only writer, so a load and a store are enough.
    const int mask = deltaMask.load (std::memory_order_relaxed);
    const int bit = 1 << moduleId;
    deltaMask.store (on ? (mask | bit) : (mask & ~bit), std::memory_order_relaxed);
}

bool ChannelStrip::getDelta (int moduleId) const noexcept
{
    if (moduleId < 0 || moduleId >= numModules) return false;
    return ((deltaMask.load (std::memory_order_relaxed) >> moduleId) & 1) != 0;
}

// Smoothed energies of the module's wet and dry signals. The learned wet/dry level ratio is
// refreshed on control ticks only, so it does not depend on the host block size. It is clamped to +/-12 dB.
void ChannelStrip::learnModule (int id, const float* const* wet, const float* const* dry, int numCh, int n, uint32_t phase) noexcept
{
    auto& l = learn[(size_t) id];
    float ew = l.ew, ed = l.ed, g = l.gainLin;
    const float a = learnAlpha, b = 1.0f - a;
    for (int i = 0; i < n; ++i)
    {
        if (ew > 1.0e-10f && ed > 1.0e-10f && ((phase + (uint32_t) i) % (uint32_t) kControlBlock) == 0)
            g = std::clamp (std::sqrt (ew / ed), 0.25f, 4.0f);
        float sw = 0.0f, sd = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            sw += wet[c][i] * wet[c][i];
            sd += dry[c][i] * dry[c][i];
        }
        ew = a * ew + b * sw;
        ed = a * ed + b * sd;
    }
    l.ew = ew;
    l.ed = ed;
    l.gainLin = g;
}

// Delta output: the module's output minus its latency-aligned input, crossfaded in and out.
// dry may alias io (a silent module's output is its input).
void ChannelStrip::applyDelta (int id, float* const* io, const float* const* dry, int numCh, int n, uint32_t phase) noexcept
{
    if (id != modPre && id != modGate && id != modEq) return;
    auto& w = deltaW[(size_t) id];
    if (w.isConstant() && w.target() == 0.0f) return;
    w.forEachSegment (n, phase, [&] (int s, int len, float base, float slope, int offset)
    {
        for (int j = 0; j < len; ++j)
        {
            const float d = base + slope * (float) (offset + j);
            for (int c = 0; c < numCh; ++c) io[c][s + j] -= d * dry[c][s + j];
        }
    });
}

// Whole-strip Compare: crossfade from the processed output to the bypass path.
void ChannelStrip::applyWholeCompare (float* const* io, int numCh, int n, uint32_t phase) noexcept
{
    wholeW.forEachSegment (n, phase, [&] (int s, int len, float base, float slope, int offset)
    {
        for (int j = 0; j < len; ++j)
        {
            const float w = base + slope * (float) (offset + j);
            for (int c = 0; c < numCh; ++c) io[c][s + j] += (cmpBuf[(size_t) c][(size_t) (s + j)] - io[c][s + j]) * w;
        }
    });
}

// Module Compare: the module is bypassed in the chain (its fade does that), and the bypassed chain is
// scaled by the module's learned wet/dry level, so the two states are loudness-matched.
void ChannelStrip::applyModuleCompare (float* const* io, int numCh, int n, uint32_t phase) noexcept
{
    float* g = modGainBuf.data();
    modGainV.forEachSegment (n, phase, [&] (int s, int len, float base, float slope, int offset)
    {
        for (int j = 0; j < len; ++j) g[s + j] = base + slope * (float) (offset + j);
    });
    modW.forEachSegment (n, phase, [&] (int s, int len, float base, float slope, int offset)
    {
        for (int j = 0; j < len; ++j)
        {
            const float w = base + slope * (float) (offset + j);
            const float gain = 1.0f + w * (g[s + j] - 1.0f);
            for (int c = 0; c < numCh; ++c) io[c][s + j] *= gain;
        }
    });
}

//==============================================================================
void ChannelStrip::process (float* const* io, int numChannels, int n, const float* const* sidechain, int sidechainChannels)
{
    jassert (n <= maxBlock);
    const auto tStart = juce::Time::getHighResolutionTicks();
    numChannels = juce::jlimit (1, 2, numChannels);
    updateParams (false);
    levelMatch.update (pi (P::matchMode), pv (P::matchTrim));
    if (const float db = levelMatch.gainDb(); db != lastMatchDb)
    {
        lastMatchDb = db;
        matchGain.setTarget (dbToGain (db));
    }
    monitor.set (pi (P::monitor));

    // Compare and Delta targets (transient). The ramps run at control rate in the output stage.
    const int cmpTarget = compareTarget.load (std::memory_order_relaxed);
    const int deltaBits = deltaMask.load (std::memory_order_relaxed);
    const bool cmpChanged = cmpTarget != lastCompareTarget;
    lastCompareTarget = cmpTarget;
    wholeW.setTarget (cmpTarget == 0 ? 1.0f : 0.0f);
    modW.setTarget (cmpTarget >= 1 ? 1.0f : 0.0f);
    modGainV.setTarget (cmpTarget >= 1 ? learn[(size_t) (cmpTarget - 1)].gainLin : 1.0f);
    if (cmpChanged) modGainV.snap(); // a new compared module starts from its own learned level, not a glide
    for (int m : { (int) modPre, (int) modGate, (int) modEq })
        deltaW[(size_t) m].setTarget (((deltaBits >> m) & 1) != 0 ? 1.0f : 0.0f);

    MeterFrame frame;
    frame.numSamples = n;
    frame.blockUs = (float) (1.0e6 * n / fs);

    // Input stage: swap, polarity and gain.
    inputStage (io, numChannels, n);
    for (int c = 0; c < numChannels; ++c)
    {
        float pk = 0.0f, ms = 0.0f;
        const float* x = io[c];
        for (int i = 0; i < n; ++i) { pk = std::max (pk, std::abs (x[i])); ms += x[i] * x[i]; }
        frame.inPeak[c] = pk;
        frame.inMs[c] = ms / (float) n;
    }

    // Dry tap: the input-stage output, delayed by the chain latency. It is the reference of Level
    // Match and the dry side of the global mix. The delay line is fed every block whenever the
    // chain has latency, so switching either feature on is seamless.
    const bool mixActive = ! (mixAmt.isConstant() && mixAmt.target() >= 1.0f);
    const bool matchActive = levelMatch.isMeasuring() || ! matchGain.isUnity();
    const int chainLatency = latencyChain.load (std::memory_order_relaxed);
    if (mixActive || matchActive)
    {
        float* tap[2] { tapDry[0].data(), tapDry[1].data() };
        dryDelay.setDelay (chainLatency);
        dryDelay.process (io, tap, numChannels, n);
    }
    else if (chainLatency > 0)
    {
        dryDelay.setDelay (chainLatency);
        dryDelay.write (io, numChannels, n);
    }

    // Compare path: the input stage output delayed by the total latency, so that it lines up with the
    // processed output (which also carries the master section's latency). Always run, so it is primed.
    {
        float* cmp[2] { cmpBuf[0].data(), cmpBuf[1].data() };
        cmpDelay.setDelay (latencyTotal.load (std::memory_order_relaxed));
        cmpDelay.process (io, cmp, numChannels, n);
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

    // Module chain in the user's order. A disabled module costs nothing but the delay of its input
    // by the module's latency, so switching it never shifts timing.
    const auto order = getOrder();
    bool inMs = false;
    for (int slot = 0; slot < numModules; ++slot)
    {
        const int id = order[(size_t) slot];
        auto& fade = moduleFade[(size_t) id];
        const int lat = moduleLat[(size_t) id];
        const bool wasSilent = fade.isSilent();
        fade.setTarget (moduleEnabled (id) && cmpTarget != id + 1); // a compared module is bypassed
        if (fade.isSilent())
        {
            moduleUsSmoothed[(size_t) id] *= 0.9f;
            holdoff[(size_t) id] = 0;
            if (lat > 0)
            {
                bypassDelay[(size_t) id].setDelay (lat);
                bypassDelay[(size_t) id].process (io, io, numChannels, n);
            }
            applyDelta (id, io, io, numChannels, n, clock.phase); // silent: input equals output, Delta is silence
            continue;
        }
        if (wasSilent)
        {
            resetModule (id);
            holdoff[(size_t) id] = lat; // the module's first `lat` output samples are not yet valid
        }

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

        if (lat > 0) bypassDelay[(size_t) id].setDelay (lat);
        if (fade.isFullyOn())
        {
            // Latency-aligned dry copy: the learned level of the module and Delta both need it.
            float* dryNow[2] { dryScratch[0].data(), dryScratch[1].data() };
            if (lat > 0) bypassDelay[(size_t) id].process (io, dryNow, numChannels, n);
            else for (int c = 0; c < numChannels; ++c) std::copy (io[c], io[c] + n, dryNow[c]);
            runModule (id, io, numChannels, n, k);
            learnModule (id, io, dryNow, numChannels, n, clock.phase);
            applyDelta (id, io, dryNow, numChannels, n, clock.phase);
        }
        else
        {
            float* dryPtr[2] { fadeDry[0].data(), fadeDry[1].data() };
            if (lat > 0)
            {
                bypassDelay[(size_t) id].process (io, dryPtr, numChannels, n);
            }
            else
            {
                for (int c = 0; c < numChannels; ++c) std::copy (io[c], io[c] + n, dryPtr[c]);
            }
            runModule (id, io, numChannels, n, k);
            auto& hold = holdoff[(size_t) id];
            for (int i = 0; i < n; ++i)
            {
                float w = 0.0f;
                if (hold > 0) --hold;
                else w = fade.next();
                for (int c = 0; c < numChannels; ++c) io[c][i] = dryPtr[c][i] + (io[c][i] - dryPtr[c][i]) * w;
            }
            applyDelta (id, io, dryPtr, numChannels, n, clock.phase);
            if (fade.isSilent()) resetModule (id);
        }

        const float us = (float) ((double) (juce::Time::getHighResolutionTicks() - t0) * ticksToUs);
        moduleUsSmoothed[(size_t) id] = 0.9f * moduleUsSmoothed[(size_t) id] + 0.1f * us;
    }
    if (inMs) lrToMs (io[0], io[1], n);

    // After the chain: level match -> global mix -> output gain -> master -> monitor.
    const auto tMaster = juce::Time::getHighResolutionTicks();
    if (matchActive) matchStage (io, numChannels, n);
    if (mixActive) mixStage (io, numChannels, n);
    if (! outGain.isUnity()) outGain.apply (io, numChannels, n, clock.phase);
    if (! cmpOutGain.isUnity())
    {
        float* cmp[2] { cmpBuf[0].data(), cmpBuf[1].data() };
        cmpOutGain.apply (cmp, numChannels, n, clock.phase); // same ramp as outGain: the bypass keeps the output level
    }

    master.process (io, numChannels, n);
    frame.limGr = master.getGainReductionDb();

    // Compare (9.3): a bypassed module is replaced by its learned level, the whole strip by its input.
    if (! modW.isConstant() || modW.target() > 0.0f) applyModuleCompare (io, numChannels, n, clock.phase);
    if (! wholeW.isConstant() || wholeW.target() > 0.0f) applyWholeCompare (io, numChannels, n, clock.phase);

    if (numChannels == 2 && ! monitor.isIdentity()) monitor.process (io[0], io[1], n);

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
    frame.gateKeyDb = gate.getKeyLevelDb();
    frame.ds1KeyDb = ds.getKeyLevelDb (0);
    frame.ds2KeyDb = ds.getKeyLevelDb (1);
    frame.compKeyDb = comp.getKeyLevelDb();
    for (int b = 0; b < 4; ++b) frame.eqDynGainDb[b] = moduleFade[modEq].isSilent() ? 0.0f : eq.getDynGainDb (b);
    for (int m = 0; m < 6; ++m) frame.moduleUs[m] = moduleUsSmoothed[(size_t) m];
    frame.totalUs = (float) ((double) (tEnd - tStart) * ticksToUs);
    // Read-only copies for the Instance Hub.
    float eqMax = 0.0f;
    for (int b = 0; b < 4; ++b) eqMax = std::max (eqMax, std::abs (frame.eqDynGainDb[b]));
    hubGr[0].store (frame.gateGr, std::memory_order_relaxed);
    hubGr[1].store (frame.ds1Gr, std::memory_order_relaxed);
    hubGr[2].store (frame.ds2Gr, std::memory_order_relaxed);
    hubGr[3].store (eqMax, std::memory_order_relaxed);
    hubGr[4].store (frame.compGr, std::memory_order_relaxed);
    hubGr[5].store (frame.limGr, std::memory_order_relaxed);

    frames.push (frame);

    clock.advance (n); // once per block, after every user of the clock
}
} // namespace nl

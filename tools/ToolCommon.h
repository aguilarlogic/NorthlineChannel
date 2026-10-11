#pragma once

#include "PluginProcessor.h"
#include <atomic>

namespace tool
{
// Allocation counter (global operator new is overridden in ToolMain.cpp).
extern std::atomic<bool> countAllocations;
extern std::atomic<long long> allocationCount;

int runTests (const juce::StringArray& args);
int runMeasure (const juce::StringArray& args);
int runBench (const juce::StringArray& args);

// Sets a parameter by plain value.
inline void setParam (nl::NorthlineProcessor& p, int index, float plain)
{
    auto* prm = p.param (index);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

// Fills `buffer` with deterministic noise.
inline void fillNoise (juce::AudioBuffer<float>& buffer, juce::Random& rng, float gain)
{
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * gain);
}

// Configures a heavy but realistic setting with every module active.
inline void configureEverything (nl::NorthlineProcessor& p, bool hq, bool lookahead)
{
    using namespace nl;
    setParam (p, P::preOn, 1); setParam (p, P::satOn, 1); setParam (p, P::satType, 1); setParam (p, P::satDrive, 40); setParam (p, P::quality, hq ? 1.0f : 0.0f);
    setParam (p, P::hpOn, 1); setParam (p, P::hpFreq, 60); setParam (p, P::hpSlope, 3);
    setParam (p, P::lpOn, 1); setParam (p, P::lpFreq, 16000);
    setParam (p, P::weightOn, 1);
    setParam (p, P::gateOn, 1); setParam (p, P::gateThresh, -60); setParam (p, P::gateScHp, 100);
    setParam (p, P::dsOn, 1); setParam (p, P::ds2On, 1);
    setParam (p, P::eqOn, 1);
    setParam (p, P::eqLowGain, 2); setParam (p, P::eqMidGain, -3); setParam (p, P::eqToneGain, 2); setParam (p, P::eqHighGain, 3);
    setParam (p, P::compOn, 1); setParam (p, P::compType, 2); setParam (p, P::compThresh, -30); setParam (p, P::compScHp, 80);
    setParam (p, P::compScBellGain, 3);
    setParam (p, P::limOn, 1); setParam (p, P::limMode, lookahead ? 1.0f : 0.0f);
}
} // namespace tool

#pragma once

#include "dsp/ChannelStrip.h"

namespace nl
{
struct PresetValue
{
    int index;   // parameter index (P::...)
    float value; // plain value
};

struct FactoryPreset
{
    juce::String category, name;
    std::vector<PresetValue> values;
    std::vector<int> focus;                    // parameters highlighted in FOCUS mode
    ChannelStrip::Order order = ChannelStrip::defaultOrder;
};

const std::vector<FactoryPreset>& factoryPresets();

juce::File userPresetFolder();
} // namespace nl

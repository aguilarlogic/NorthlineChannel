#include "Params.h"

namespace nl
{
namespace
{
using K = ParamDef::Kind;

const std::vector<const char*> kStMs { "ST", "MS" };
const std::vector<const char*> kStDuoMs { "ST", "DUO", "MS" };
const std::vector<const char*> kTarget { "Both", "Mid / L", "Side / R" };
const std::vector<const char*> kSlopes { "6 dB", "12 dB", "18 dB", "24 dB" };
const std::vector<const char*> kShelfTypes { "Shelf", "Res Shelf", "Bell" };
const std::vector<const char*> kBellTypes { "Wide", "Narrow", "Param" };
const std::vector<const char*> kDsTypes { "Low Shelf", "Bell", "Narrow", "High Shelf" };
const std::vector<const char*> kNone {};

#define NL_EQ_BAND(prefix, label, types, defFreq, defQ) \
    { prefix "_on", label " On", K::Bool, 0, 1, 1, 0, Unit::None, kNone }, \
    { prefix "_type", label " Type", K::Choice, 0, 2, 0, 0, Unit::None, types }, \
    { prefix "_freq", label " Freq", K::Float, 20, 20000, defFreq, 1000, Unit::Hz, kNone }, \
    { prefix "_gain", label " Gain", K::Float, -18, 18, 0, 0, Unit::Db, kNone }, \
    { prefix "_q", label " Q", K::Float, 0.2f, 10, defQ, 1.4f, Unit::Q, kNone }, \
    { prefix "_target", label " Target", K::Choice, 0, 2, 0, 0, Unit::None, kTarget }

const std::vector<ParamDef>& table()
{
    static const std::vector<ParamDef> defs {
        // Master
        { "in_gain", "Input Gain", K::Float, -24, 24, 0, 0, Unit::Db, kNone },
        { "in_phase", "Input Phase Invert", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "out_gain", "Output Gain", K::Float, -24, 24, 0, 0, Unit::Db, kNone },
        { "monitor", "Monitor", K::Choice, 0, 5, 0, 0, Unit::None, { "Stereo", "Mono", "Left", "Right", "Mid", "Side" } },
        { "lim_on", "Limiter On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "lim_ceiling", "Limiter Ceiling", K::Float, -30, 0, -0.3f, 0, Unit::Db, kNone },
        { "lim_lookahead", "Limiter Lookahead", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "lim_release", "Limiter Release", K::Float, 10, 1000, 100, 100, Unit::Ms, kNone },
        { "level_match", "Level Match", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "vu_source", "VU Source", K::Choice, 0, 2, 1, 0, Unit::None, { "Input", "Output", "GR" } },
        { "vu_ref", "VU Reference", K::Float, -26, -4, -18, 0, Unit::Db, kNone },

        // PRE
        { "pre_on", "PRE On", K::Bool, 0, 1, 1, 0, Unit::None, kNone },
        { "pre_mode", "PRE Mode", K::Choice, 0, 1, 0, 0, Unit::None, kStMs },
        { "sat_on", "Saturation On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "sat_type", "Saturation Type", K::Choice, 0, 3, 0, 0, Unit::None, { "Odd", "Even", "Heavy", "Crush" } },
        { "sat_drive", "Saturation Drive", K::Float, 0, 100, 30, 0, Unit::Percent, kNone },
        { "sat_hq", "Saturation HQ", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "hp_on", "HP On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "hp_freq", "HP Freq", K::Float, 20, 20000, 80, 400, Unit::Hz, kNone },
        { "hp_slope", "HP Slope", K::Choice, 0, 3, 1, 0, Unit::None, kSlopes },
        { "hp_res", "HP Res", K::Float, 0.1f, 4, 0.7071f, 0.9f, Unit::Q, kNone },
        { "lp_on", "LP On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "lp_freq", "LP Freq", K::Float, 20, 20000, 16000, 2000, Unit::Hz, kNone },
        { "lp_slope", "LP Slope", K::Choice, 0, 3, 1, 0, Unit::None, kSlopes },
        { "lp_res", "LP Res", K::Float, 0.1f, 4, 0.7071f, 0.9f, Unit::Q, kNone },
        { "filt_target", "Filter Target", K::Choice, 0, 2, 0, 0, Unit::None, kTarget },
        { "thump_on", "Thump On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "thump_amt", "Thump Amount", K::Choice, 0, 1, 0, 0, Unit::None, { "+2 dB", "+4 dB" } },

        // GATE
        { "gate_on", "Gate On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "gate_mode", "Gate Mode", K::Choice, 0, 2, 0, 0, Unit::None, kStDuoMs },
        { "gate_type", "Gate Type", K::Choice, 0, 1, 0, 0, Unit::None, { "Gate", "Expander" } },
        { "gate_thresh", "Gate Threshold", K::Float, -80, 0, -40, 0, Unit::Db, kNone },
        { "gate_hyst", "Gate Hysteresis", K::Float, 0, 24, 4, 0, Unit::Db, kNone },
        { "gate_floor", "Gate Floor", K::Float, -80, 0, -40, 0, Unit::Db, kNone },
        { "gate_ratio", "Expander Ratio", K::Float, 1.2f, 8, 2, 2.5f, Unit::Ratio, kNone },
        { "gate_attack", "Gate Attack", K::Float, 0.05f, 100, 0.5f, 5, Unit::Ms, kNone },
        { "gate_hold", "Gate Hold", K::Float, 0, 500, 30, 60, Unit::Ms, kNone },
        { "gate_release", "Gate Release", K::Float, 5, 5000, 150, 250, Unit::Ms, kNone },
        { "gate_sc_hp", "Gate SC HP", K::Float, 20, 15000, 20, 500, Unit::Hz, kNone },
        { "gate_sc_lp", "Gate SC LP", K::Float, 200, 20000, 20000, 3000, Unit::Hz, kNone },
        { "gate_ext", "Gate External SC", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "gate_listen", "Gate SC Listen", K::Bool, 0, 1, 0, 0, Unit::None, kNone },

        // DS
        { "ds_on", "DS On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "ds_mode", "DS Mode", K::Choice, 0, 2, 0, 0, Unit::None, kStDuoMs },
        { "ds_relative", "DS Relative Detect", K::Bool, 0, 1, 1, 0, Unit::None, kNone },
        { "ds_release", "DS Release", K::Float, 10, 500, 60, 80, Unit::Ms, kNone },
        { "ds_out", "DS Output", K::Float, -12, 12, 0, 0, Unit::Db, kNone },
        { "ds_listen", "DS Listen", K::Choice, 0, 2, 0, 0, Unit::None, { "Off", "Band 1", "Band 2" } },
        { "ds_ext", "DS External SC", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "ds1_on", "DS1 On", K::Bool, 0, 1, 1, 0, Unit::None, kNone },
        { "ds1_type", "DS1 Type", K::Choice, 0, 3, 3, 0, Unit::None, kDsTypes },
        { "ds1_freq", "DS1 Freq", K::Float, 20, 20000, 6000, 1500, Unit::Hz, kNone },
        { "ds1_thresh", "DS1 Threshold", K::Float, -48, 0, -20, 0, Unit::Db, kNone },
        { "ds1_range", "DS1 Range", K::Float, 0, 24, 12, 0, Unit::Db, kNone },
        { "ds2_on", "DS2 On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "ds2_type", "DS2 Type", K::Choice, 0, 3, 1, 0, Unit::None, kDsTypes },
        { "ds2_freq", "DS2 Freq", K::Float, 20, 20000, 3500, 1500, Unit::Hz, kNone },
        { "ds2_thresh", "DS2 Threshold", K::Float, -48, 0, -20, 0, Unit::Db, kNone },
        { "ds2_range", "DS2 Range", K::Float, 0, 24, 8, 0, Unit::Db, kNone },

        // EQ
        { "eq_on", "EQ On", K::Bool, 0, 1, 1, 0, Unit::None, kNone },
        { "eq_mode", "EQ Mode", K::Choice, 0, 1, 0, 0, Unit::None, kStMs },
        NL_EQ_BAND ("eq_low", "Low", kShelfTypes, 100, 0.7f),
        NL_EQ_BAND ("eq_mid", "Mid", kBellTypes, 500, 1.0f),
        NL_EQ_BAND ("eq_tone", "Tone", kBellTypes, 2500, 1.0f),
        NL_EQ_BAND ("eq_high", "High", kShelfTypes, 10000, 0.7f),

        // COMP
        { "comp_on", "Comp On", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "comp_mode", "Comp Mode", K::Choice, 0, 2, 0, 0, Unit::None, kStDuoMs },
        { "comp_type", "Comp Type", K::Choice, 0, 3, 0, 0, Unit::None, { "VCA", "FET", "OPT", "SOFT" } },
        { "comp_thresh", "Comp Threshold", K::Float, -50, 0, -18, 0, Unit::Db, kNone },
        { "comp_ratio", "Comp Ratio", K::Float, 1, kRatioInf, 3, 4, Unit::Ratio, kNone },
        { "comp_attack", "Comp Attack", K::Float, 0.1f, 150, 10, 12, Unit::Ms, kNone },
        { "comp_release", "Comp Release", K::Float, 20, 5000, 150, 300, Unit::Ms, kNone },
        { "comp_makeup", "Comp Auto Makeup", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "comp_mix", "Comp Mix", K::Float, 0, 100, 100, 0, Unit::Percent, kNone },
        { "comp_out", "Comp Output", K::Float, -12, 24, 0, 0, Unit::Db, kNone },
        { "comp_sc_hp", "Comp SC HP", K::Float, 20, 500, 20, 100, Unit::Hz, kNone },
        { "comp_sc_bell_freq", "Comp SC Bell Freq", K::Float, 20, 20000, 3000, 1000, Unit::Hz, kNone },
        { "comp_sc_bell_gain", "Comp SC Bell Gain", K::Float, -18, 18, 0, 0, Unit::Db, kNone },
        { "comp_ext", "Comp External SC", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
        { "comp_listen", "Comp SC Listen", K::Bool, 0, 1, 0, 0, Unit::None, kNone },
    };
    jassert ((int) defs.size() == P::count);
    return defs;
}
#undef NL_EQ_BAND
} // namespace

const ParamDef& paramDef (int index) { return table()[(size_t) index]; }
juce::String paramID (int index) { return paramDef (index).id; }

// juce::String (float, 0) prints full precision, so whole numbers are rounded explicitly.
static juce::String num (float v, int decimals)
{
    return decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v));
}

juce::String formatValue (int index, float v)
{
    const auto& d = paramDef (index);
    if (d.kind == ParamDef::Bool) return v >= 0.5f ? "On" : "Off";
    if (d.kind == ParamDef::Choice)
    {
        const int i = juce::jlimit (0, (int) d.choices.size() - 1, (int) std::lround (v));
        return d.choices.empty() ? juce::String (i) : juce::String (d.choices[(size_t) i]);
    }
    switch (d.unit)
    {
        case Unit::Db:      return (v > 0.0f ? "+" : "") + num (v, std::abs (v) < 9.95f ? 1 : 0) + " dB";
        case Unit::Hz:      return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        case Unit::Ms:      return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : num (v, v < 9.995f ? 2 : (v < 99.95f ? 1 : 0)) + " ms";
        case Unit::Percent: return juce::String (juce::roundToInt (v)) + " %";
        case Unit::Ratio:   return v >= kRatioInf - 0.01f ? juce::String ("inf : 1") : num (v, v < 9.95f ? 1 : 0) + " : 1";
        case Unit::Q:       return juce::String (v, 2);
        case Unit::None:
        default:            return juce::String (v, 2);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < P::count; ++i)
    {
        const auto& d = paramDef (i);
        const juce::ParameterID pid { d.id, 1 };
        if (d.kind == ParamDef::Bool)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.defV >= 0.5f));
        }
        else if (d.kind == ParamDef::Choice)
        {
            juce::StringArray items;
            for (auto* c : d.choices) items.add (c);
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, items, (int) d.defV));
        }
        else
        {
            juce::NormalisableRange<float> range (d.minV, d.maxV);
            if (d.centre > 0.0f) range.setSkewForCentre (d.centre);
            const int index = i;
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction ([index] (float v, int) { return formatValue (index, v); })
                                  .withValueFromStringFunction ([] (const juce::String& s)
                                  {
                                      const float v = s.retainCharacters ("-.0123456789").getFloatValue();
                                      if (s.containsIgnoreCase ("inf")) return kRatioInf;
                                      if (s.containsIgnoreCase ("khz")) return v * 1000.0f;
                                      if (s.trim().endsWithIgnoreCase (" s") && ! s.containsIgnoreCase ("ms")) return v * 1000.0f;
                                      return v;
                                  });
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, range, d.defV, attributes));
        }
    }
    return layout;
}

const char* moduleName (int moduleId)
{
    static const char* names[] { "PRE", "GATE", "DS2", "EQ", "COMP" };
    return names[juce::jlimit (0, numModules - 1, moduleId)];
}
} // namespace nl

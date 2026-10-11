#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nl
{
// Parameter indices. The order must match the definition table in Params.cpp.
// IDs are frozen from the first beta: after that, never rename or reuse an ID.
namespace P
{
enum Index : int
{
    // Master / global
    inGain, inPolarity, inSwap, outGain, globalMix, monitor, quality,
    limOn, limMode, limCeiling, limRelease,
    matchMode, matchTrim, vuSource, opLevel,

    // PRE
    preOn, preMode, satOn, satType, satDrive, satMix, satTone, satAutoGain,
    hpOn, hpFreq, hpSlope, hpRes, lpOn, lpFreq, lpSlope, lpRes, filtTarget,
    weightOn, weightFreq, weightAmt, weightTrack,

    // GATE
    gateOn, gateMode, gateType, gateThresh, gateHyst, gateFloor, gateRatio, gateKnee,
    gateAttack, gateHold, gateRelease, gateScHp, gateScLp, gateExt, gateListen, gateLookahead,

    // DE-ESS (band blocks have a fixed stride: on, mode, type, freq, thresh, range)
    dsOn, dsMode, dsRelative, dsAttack, dsRelease, dsOut, dsListen, dsExt, dsLookahead,
    ds1On, ds1Mode, ds1Type, ds1Freq, ds1Thresh, ds1Range,
    ds2On, ds2Mode, ds2Type, ds2Freq, ds2Thresh, ds2Range,

    // EQ (band blocks have a fixed stride, see kEqBandStride)
    eqOn, eqMode, eqChar, eqSolo,
    eqLowOn, eqLowType, eqLowFreq, eqLowGain, eqLowQ, eqLowTarget, eqLowDyn, eqLowDynThresh, eqLowDynRange, eqLowDynSpeed, eqLowDynExt,
    eqMidOn, eqMidType, eqMidFreq, eqMidGain, eqMidQ, eqMidTarget, eqMidDyn, eqMidDynThresh, eqMidDynRange, eqMidDynSpeed, eqMidDynExt,
    eqToneOn, eqToneType, eqToneFreq, eqToneGain, eqToneQ, eqToneTarget, eqToneDyn, eqToneDynThresh, eqToneDynRange, eqToneDynSpeed, eqToneDynExt,
    eqHighOn, eqHighType, eqHighFreq, eqHighGain, eqHighQ, eqHighTarget, eqHighDyn, eqHighDynThresh, eqHighDynRange, eqHighDynSpeed, eqHighDynExt,

    // COMP
    compOn, compMode, compType, compThresh, compRatio, compKnee, compAttack, compRelease, compAutoRel,
    compMakeup, compMix, compOut, compRange, compLink,
    compScHp, compScLp, compScBellFreq, compScBellGain, compExt, compListen, compLookahead,

    count
};

constexpr int kDsBandStride = ds2On - ds1On;     // 6
constexpr int kEqBandStride = eqMidOn - eqLowOn; // 11
static_assert (kDsBandStride == 6 && kEqBandStride == 11, "band blocks must keep their layout");
} // namespace P

enum class Unit { None, Db, Hz, Ms, Percent, Ratio, Q };

// Parameter flags.
//  NonAutomatable: settings that change latency or calibration (hosts must not automate them).
//  Transient: monitoring aids (solo, listen, monitor mode). Not automatable, never saved in
//             presets, A/B slots or undo, and reset to default when a session or preset loads.
enum ParamFlags : int { kNonAutomatable = 1, kTransient = 2 };

struct ParamDef
{
    const char* id;
    const char* name;
    enum Kind { Float, Bool, Choice } kind;
    float minV, maxV, defV, centre; // centre <= 0 means linear
    Unit unit;
    std::vector<const char*> choices;
    int flags = 0;
};

const ParamDef& paramDef (int index);
juce::String paramID (int index);
bool isAutomatable (int index);
bool isTransient (int index);
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::String formatValue (int index, float value);

// Ratio values at or above this are treated as infinity.
constexpr float kRatioInf = 30.0f;

// Lookahead choices shared by GATE, DE-ESS and COMP (index -> milliseconds).
constexpr float kLookaheadMs[] { 0.0f, 1.0f, 2.0f, 5.0f };

enum ModuleId : int { modPre = 0, modGate, modDs, modEq, modComp, numModules };
const char* moduleName (int moduleId);
} // namespace nl

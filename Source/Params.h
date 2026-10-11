#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nl
{
// Parameter indices. The order must match the definition table in Params.cpp.
// IDs are stable across versions: never rename or reuse an ID.
namespace P
{
enum Index : int
{
    inGain, inPhase, outGain, monitor, limOn, limCeiling, limLookahead, limRelease, levelMatch, vuSource, vuRef,

    preOn, preMode, satOn, satType, satDrive, satHQ,
    hpOn, hpFreq, hpSlope, hpRes, lpOn, lpFreq, lpSlope, lpRes, filtTarget, thumpOn, thumpAmt,

    gateOn, gateMode, gateType, gateThresh, gateHyst, gateFloor, gateRatio, gateAttack, gateHold, gateRelease,
    gateScHp, gateScLp, gateExt, gateListen,

    dsOn, dsMode, dsRelative, dsRelease, dsOut, dsListen, dsExt,
    ds1On, ds1Type, ds1Freq, ds1Thresh, ds1Range,
    ds2On, ds2Type, ds2Freq, ds2Thresh, ds2Range,

    eqOn, eqMode,
    eqLowOn, eqLowType, eqLowFreq, eqLowGain, eqLowQ, eqLowTarget,
    eqMidOn, eqMidType, eqMidFreq, eqMidGain, eqMidQ, eqMidTarget,
    eqToneOn, eqToneType, eqToneFreq, eqToneGain, eqToneQ, eqToneTarget,
    eqHighOn, eqHighType, eqHighFreq, eqHighGain, eqHighQ, eqHighTarget,

    compOn, compMode, compType, compThresh, compRatio, compAttack, compRelease, compMakeup, compMix, compOut,
    compScHp, compScBellFreq, compScBellGain, compExt, compListen,

    count
};
} // namespace P

enum class Unit { None, Db, Hz, Ms, Percent, Ratio, Q };

struct ParamDef
{
    const char* id;
    const char* name;
    enum Kind { Float, Bool, Choice } kind;
    float minV, maxV, defV, centre; // centre <= 0 means linear
    Unit unit;
    std::vector<const char*> choices;
};

const ParamDef& paramDef (int index);
juce::String paramID (int index);
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::String formatValue (int index, float value);

// Ratio values at or above this are treated as infinity.
constexpr float kRatioInf = 30.0f;

enum ModuleId : int { modPre = 0, modGate, modDs, modEq, modComp, numModules };
const char* moduleName (int moduleId);
} // namespace nl

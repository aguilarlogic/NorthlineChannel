#include "Presets.h"

namespace nl
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace P;
    static const std::vector<FactoryPreset> presets {
        { "", "Init", {}, {} },

        { "Vocals", "Vocal - Upfront",
          { { hpOn, 1 }, { hpFreq, 90 }, { hpSlope, 2 },
            { satOn, 1 }, { satType, 1 }, { satDrive, 25 },
            { dsOn, 1 }, { ds1Type, 3 }, { ds1Freq, 6500 }, { ds1Thresh, -18 }, { ds1Range, 8 },
            { eqMidType, 0 }, { eqMidFreq, 320 }, { eqMidGain, -2 }, { eqToneType, 2 }, { eqToneFreq, 3000 }, { eqToneGain, 1.5f }, { eqToneQ, 1.2f },
            { eqHighType, 0 }, { eqHighFreq, 11000 }, { eqHighGain, 3 },
            { compOn, 1 }, { compType, 2 }, { compThresh, -26 }, { compRatio, 3 }, { compAttack, 10 }, { compRelease, 300 }, { compMakeup, 1 } },
          { hpFreq, ds1Thresh, eqHighGain, compThresh, satDrive } },

        { "Vocals", "Vocal - Smooth Opto",
          { { hpOn, 1 }, { hpFreq, 80 },
            { dsOn, 1 }, { ds1Type, 3 }, { ds1Freq, 7000 }, { ds1Thresh, -20 }, { ds1Range, 6 },
            { eqLowType, 0 }, { eqLowFreq, 180 }, { eqLowGain, -1.5f }, { eqHighType, 1 }, { eqHighFreq, 12000 }, { eqHighGain, 2 },
            { compOn, 1 }, { compType, 2 }, { compThresh, -30 }, { compRatio, 4 }, { compAttack, 15 }, { compRelease, 600 }, { compMakeup, 1 } },
          { compThresh, compRelease, eqHighGain, ds1Thresh } },

        { "Vocals", "Vocal - Rap Presence",
          { { hpOn, 1 }, { hpFreq, 110 }, { hpSlope, 2 },
            { satOn, 1 }, { satType, 0 }, { satDrive, 35 },
            { dsOn, 1 }, { ds1Type, 1 }, { ds1Freq, 5500 }, { ds1Thresh, -16 }, { ds1Range, 10 },
            { eqToneType, 0 }, { eqToneFreq, 2200 }, { eqToneGain, 3 }, { eqHighType, 0 }, { eqHighFreq, 9000 }, { eqHighGain, 2 },
            { compOn, 1 }, { compType, 1 }, { compThresh, -22 }, { compRatio, 6 }, { compAttack, 3 }, { compRelease, 90 }, { compMix, 80 }, { compMakeup, 1 } },
          { compThresh, compMix, eqToneGain, ds1Thresh } },

        { "Drums", "Kick - Punch",
          { { gateOn, 1 }, { gateThresh, -32 }, { gateFloor, -24 }, { gateRelease, 120 }, { gateHold, 40 },
            { weightOn, 1 }, { weightAmt, 0 },
            { eqLowType, 2 }, { eqLowFreq, 60 }, { eqLowGain, 3 }, { eqLowQ, 1.2f },
            { eqMidType, 1 }, { eqMidFreq, 360 }, { eqMidGain, -4 },
            { eqToneType, 2 }, { eqToneFreq, 3500 }, { eqToneGain, 3 }, { eqToneQ, 1.5f },
            { compOn, 1 }, { compType, 1 }, { compThresh, -16 }, { compRatio, 4 }, { compAttack, 12 }, { compRelease, 80 }, { compMix, 70 } },
          { gateThresh, eqLowGain, eqMidGain, compMix } },

        { "Drums", "Snare - Crack",
          { { gateOn, 1 }, { gateThresh, -28 }, { gateFloor, -18 }, { gateRelease, 180 },
            { satOn, 1 }, { satType, 2 }, { satDrive, 30 },
            { eqLowType, 2 }, { eqLowFreq, 200 }, { eqLowGain, 2 }, { eqLowQ, 1.0f },
            { eqToneType, 0 }, { eqToneFreq, 5000 }, { eqToneGain, 3 },
            { compOn, 1 }, { compType, 0 }, { compThresh, -20 }, { compRatio, 4 }, { compAttack, 6 }, { compRelease, 70 } },
          { gateThresh, satDrive, eqToneGain, compThresh } },

        { "Drums", "Drum Bus - Glue",
          { { satOn, 1 }, { satType, 1 }, { satDrive, 30 },
            { eqHighType, 0 }, { eqHighFreq, 10000 }, { eqHighGain, 1.5f },
            { compOn, 1 }, { compType, 0 }, { compThresh, -14 }, { compRatio, 2 }, { compAttack, 30 }, { compRelease, 150 }, { compMix, 60 }, { compMakeup, 1 } },
          { compThresh, compMix, satDrive } },

        { "Drums", "Overheads - Wide M/S",
          { { hpOn, 1 }, { hpFreq, 120 },
            { eqMode, 1 },
            { eqMidType, 0 }, { eqMidFreq, 300 }, { eqMidGain, 2 }, { eqMidTarget, 1 },
            { eqHighType, 0 }, { eqHighFreq, 9000 }, { eqHighGain, 3 }, { eqHighTarget, 2 },
            { compOn, 1 }, { compMode, 1 }, { compType, 3 }, { compThresh, -20 }, { compRatio, 2 }, { compAttack, 20 }, { compRelease, 200 } },
          { eqMidGain, eqHighGain, hpFreq } },

        { "Drums", "Room - Crush",
          { { satOn, 1 }, { satType, 3 }, { satDrive, 55 },
            { lpOn, 1 }, { lpFreq, 7000 },
            { compOn, 1 }, { compType, 1 }, { compThresh, -30 }, { compRatio, kRatioInf }, { compAttack, 0.5f }, { compRelease, 50 }, { compMix, 40 } },
          { satDrive, compMix, lpFreq } },

        { "Bass", "Bass - Solid",
          { { hpOn, 1 }, { hpFreq, 32 }, { hpSlope, 3 }, { lpOn, 1 }, { lpFreq, 9000 },
            { satOn, 1 }, { satType, 0 }, { satDrive, 40 },
            { eqLowType, 0 }, { eqLowFreq, 80 }, { eqLowGain, 2 }, { eqMidType, 0 }, { eqMidFreq, 250 }, { eqMidGain, -3 },
            { compOn, 1 }, { compType, 2 }, { compThresh, -22 }, { compRatio, 4 }, { compAttack, 20 }, { compRelease, 200 }, { compMakeup, 1 } },
          { satDrive, eqLowGain, compThresh } },

        { "Bass", "Bass DI - Grit",
          { { hpOn, 1 }, { hpFreq, 35 }, { hpSlope, 3 },
            { satOn, 1 }, { satType, 2 }, { satDrive, 45 },
            { eqToneType, 2 }, { eqToneFreq, 900 }, { eqToneGain, 2.5f }, { eqToneQ, 1.0f },
            { compOn, 1 }, { compType, 1 }, { compThresh, -20 }, { compRatio, 4 }, { compAttack, 5 }, { compRelease, 120 } },
          { satDrive, eqToneGain, compThresh } },

        { "Guitars", "Acoustic - Air",
          { { hpOn, 1 }, { hpFreq, 100 },
            { eqMidType, 0 }, { eqMidFreq, 250 }, { eqMidGain, -3 }, { eqHighType, 1 }, { eqHighFreq, 12000 }, { eqHighGain, 3 },
            { compOn, 1 }, { compType, 3 }, { compThresh, -24 }, { compRatio, 2.5f }, { compAttack, 15 }, { compRelease, 200 } },
          { eqMidGain, eqHighGain, compThresh } },

        { "Guitars", "Electric - Focus",
          { { hpOn, 1 }, { hpFreq, 90 }, { lpOn, 1 }, { lpFreq, 9000 },
            { eqMidType, 2 }, { eqMidFreq, 1500 }, { eqMidGain, 2 }, { eqMidQ, 1.2f },
            { eqToneType, 1 }, { eqToneFreq, 3500 }, { eqToneGain, -2 },
            { compOn, 1 }, { compType, 1 }, { compThresh, -18 }, { compRatio, 3 }, { compAttack, 15 }, { compRelease, 120 } },
          { hpFreq, lpFreq, eqMidGain } },

        { "Keys", "Piano - Clear",
          { { hpOn, 1 }, { hpFreq, 40 },
            { eqMidType, 0 }, { eqMidFreq, 300 }, { eqMidGain, -2 }, { eqHighType, 0 }, { eqHighFreq, 8000 }, { eqHighGain, 2 },
            { compOn, 1 }, { compType, 3 }, { compThresh, -22 }, { compRatio, 2 }, { compAttack, 25 }, { compRelease, 250 } },
          { eqMidGain, eqHighGain } },

        { "Buses", "Mix Bus - Gentle",
          { { eqHighType, 0 }, { eqHighFreq, 12000 }, { eqHighGain, 1 },
            { compOn, 1 }, { compType, 3 }, { compThresh, -16 }, { compRatio, 1.5f }, { compAttack, 30 }, { compRelease, 300 }, { compMakeup, 1 },
            { limOn, 1 }, { limCeiling, -0.3f } },
          { compThresh, limCeiling } },

        { "Spoken", "Podcast - Voice",
          { { hpOn, 1 }, { hpFreq, 80 }, { hpSlope, 2 },
            { gateOn, 1 }, { gateType, 1 }, { gateThresh, -45 }, { gateFloor, -15 }, { gateRatio, 2 },
            { dsOn, 1 }, { ds1Type, 3 }, { ds1Freq, 6000 }, { ds1Thresh, -20 }, { ds1Range, 8 },
            { eqMidType, 0 }, { eqMidFreq, 300 }, { eqMidGain, -2 }, { eqToneType, 0 }, { eqToneFreq, 3000 }, { eqToneGain, 2 },
            { compOn, 1 }, { compType, 0 }, { compThresh, -24 }, { compRatio, 3 }, { compAttack, 8 }, { compRelease, 150 }, { compMakeup, 1 },
            { limOn, 1 }, { limCeiling, -1 } },
          { gateThresh, compThresh, ds1Thresh } },
    };
    return presets;
}

juce::File userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Northline Audio")
        .getChildFile ("Northline Channel")
        .getChildFile ("Presets");
}
} // namespace nl

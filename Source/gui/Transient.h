#pragma once

// The rules behind the transient controls (roadmap 9.3 and the Compare / Delta / Q items of wave 3).
// They hold no state the processor does not own. Free of JUCE so the tests can check them directly.
namespace nl::transient
{
constexpr int kCompareOff = -1;   // processor setCompare(-1): the strip as it is
constexpr int kCompareStrip = 0;  // the whole strip, loudness-matched against the bypassed signal

// A module's compare target is 1 + its module id (the processor's convention, see ChannelStrip::setCompare).
inline int moduleCompareTarget (int moduleId) noexcept { return moduleId + 1; }

// The per-module Compare toggle: on when the current target already is that module, otherwise it asks for it.
inline int toggleModuleCompare (int current, int moduleId) noexcept
{
    return current == moduleCompareTarget (moduleId) ? kCompareOff : moduleCompareTarget (moduleId);
}

// The top-bar COMPARE button is momentary: holding it compares the whole strip, releasing it goes back.
// Shift-click latches the comparison on; a later click (with or without Shift) lets it go.
class CompareHold
{
public:
    // The target to send when the button is pressed.
    int press (bool shift) noexcept
    {
        if (latched)
        {
            latched = false;
            return kCompareOff;
        }
        latched = shift;
        return kCompareStrip;
    }

    // The target to send when the button is released: a latch stays on, a hold ends.
    int release() const noexcept { return latched ? kCompareStrip : kCompareOff; }

    bool isLatched() const noexcept { return latched; }

private:
    bool latched = false;
};

// An EQ band's type decides whether its Q means anything. Each band has three types, and the last one (Bell on
// LOW and HIGH, Param on MID and TONE) is the one that uses the band's Q. Shelves and the Wide and Narrow types
// set their own width (see EqModule::kindOf), so for them the Q knob has no effect.
inline bool eqBandUsesQ (int bandType) noexcept { return bandType == 2; }
} // namespace nl::transient

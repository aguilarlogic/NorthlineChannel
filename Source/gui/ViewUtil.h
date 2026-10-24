#pragma once

#include "../PluginProcessor.h"

namespace nl::view
{
// Plain (unnormalised) value of a parameter, from the message thread.
inline float plain (NorthlineProcessor& p, int index)
{
    auto* r = p.param (index);
    return r->convertFrom0to1 (r->getValue());
}

// Every parameter as plain values, in the form the module read*Params() functions take.
inline ParamSnapshot snapshot (NorthlineProcessor& p)
{
    ParamSnapshot s;
    for (int i = 0; i < P::count; ++i) s.v[(size_t) i] = plain (p, i);
    return s;
}

// Sets a parameter from a UI gesture (the caller brackets drags with begin and end).
inline void setPlain (NorthlineProcessor& p, int index, float value)
{
    auto* r = p.param (index);
    r->setValueNotifyingHost (r->convertTo0to1 (value));
}
} // namespace nl::view

#pragma once

#include "../Params.h"
#include <array>
#include <cmath>

namespace nl
{
// Plain parameter values captured once per block by the strip. Modules build their
// settings from it with their read*Params() function, so a module owns the mapping
// from parameters to its own settings struct.
struct ParamSnapshot
{
    std::array<float, P::count> v {};

    float f (int index) const noexcept { return v[(size_t) index]; }
    bool on (int index) const noexcept { return v[(size_t) index] >= 0.5f; }
    int choice (int index) const noexcept { return (int) std::lround (v[(size_t) index]); }

    // Snapshot holding every parameter's default value (for unit tests of single modules).
    static ParamSnapshot defaults()
    {
        ParamSnapshot s;
        for (int i = 0; i < P::count; ++i) s.v[(size_t) i] = paramDef (i).defV;
        return s;
    }
};
} // namespace nl

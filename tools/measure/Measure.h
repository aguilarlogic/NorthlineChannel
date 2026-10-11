#pragma once

#include "../TestUtil.h"

// Measurement suites: `NorthlineTool measure <suite|all> [--out <dir>] [--quick]`.
// Each suite writes a Markdown report (printed, and saved to <dir>/<suite>.md with --out).
// Suites report numbers; pass/fail thresholds belong in the tests (tools/tests).
//
// Adding a suite: create tools/measure/Measure<Area>.cpp and write
//
//     NL_MEASURE (alias, "Alias energy per saturation type and drive")
//     {
//         r.table ({ "Type", "Drive", "Alias (dB)" });
//         r.row ({ "Odd", "100 %", juce::String (-41.2, 1) });
//     }
//
// Inside the body, `r` is the MeasureReport and `quick` is true for a fast reduced run.

namespace tool
{
struct MeasureReport
{
    juce::String text;

    void h2 (const juce::String& s) { text << "\n## " << s << "\n\n"; }
    void h3 (const juce::String& s) { text << "\n### " << s << "\n\n"; }
    void para (const juce::String& s) { text << s << "\n\n"; }

    void table (std::initializer_list<juce::String> cols)
    {
        juce::StringArray c (cols.begin(), (int) cols.size());
        text << "| " << c.joinIntoString (" | ") << " |\n|";
        for (int i = 0; i < c.size(); ++i) text << "---|";
        text << "\n";
    }

    void row (std::initializer_list<juce::String> cols)
    {
        juce::StringArray c (cols.begin(), (int) cols.size());
        text << "| " << c.joinIntoString (" | ") << " |\n";
    }

    void endTable() { text << "\n"; }
};

struct MeasureSuite
{
    const char* name;
    const char* description;
    void (*fn) (MeasureReport&, bool quick);
};

inline std::vector<MeasureSuite>& measureRegistry()
{
    static std::vector<MeasureSuite> r;
    return r;
}

struct MeasureRegistrar
{
    MeasureRegistrar (const char* name, const char* desc, void (*fn) (MeasureReport&, bool))
    {
        measureRegistry().push_back ({ name, desc, fn });
    }
};

#define NL_MEASURE(name, description)                                                                  \
    static void measure_##name (tool::MeasureReport& r, bool quick);                                   \
    static const tool::MeasureRegistrar measure_##name##_registrar { #name, description, measure_##name }; \
    static void measure_##name ([[maybe_unused]] tool::MeasureReport& r, [[maybe_unused]] bool quick)

// Formats a number with a fixed number of decimals.
inline juce::String fmt (double v, int decimals = 1) { return juce::String (v, decimals); }

int runMeasure (const juce::StringArray& args);
} // namespace tool

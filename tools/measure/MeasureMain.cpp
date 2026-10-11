#include "Measure.h"

namespace tool
{
int runMeasure (const juce::StringArray& args)
{
    const auto suite = args.size() > 1 ? args[1] : juce::String ("all");
    const bool quick = args.contains ("--quick");
    juce::File outDir;
    if (const int i = args.indexOf ("--out"); i >= 0 && i + 1 < args.size())
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1]);

    auto& reg = measureRegistry();
    std::sort (reg.begin(), reg.end(), [] (const MeasureSuite& a, const MeasureSuite& b) { return juce::String (a.name) < juce::String (b.name); });

    if (suite == "list")
    {
        for (auto& s : reg) std::printf ("  %-12s %s\n", s.name, s.description);
        return 0;
    }

    int ran = 0;
    for (auto& s : reg)
    {
        if (suite != "all" && suite != s.name) continue;
        MeasureReport r;
        r.text << "# Northline Channel measurement: " << s.name << "\n\n" << s.description << "\n\n"
               << "Build: " << __DATE__ << ", " << juce::SystemStats::getCpuModel() << (quick ? ", quick run" : "") << "\n";
        s.fn (r, quick);
        std::printf ("%s\n", r.text.toRawUTF8());
        if (outDir != juce::File())
        {
            outDir.createDirectory();
            outDir.getChildFile (juce::String (s.name) + ".md").replaceWithText (r.text);
        }
        ++ran;
    }
    if (ran == 0)
    {
        std::printf ("Unknown suite '%s'. Available:\n", suite.toRawUTF8());
        for (auto& s : reg) std::printf ("  %-12s %s\n", s.name, s.description);
        return 1;
    }
    return 0;
}
} // namespace tool

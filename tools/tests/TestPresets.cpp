#include "TestUtil.h"
#include "Presets.h"
#include "gui/PresetBrowser.h"
#include "gui/TopBar.h"
#include <set>

using namespace nl;

namespace tool
{
namespace
{
// Parameters are compared with a tolerance: user preset files store values as decimal text.
bool sameValue (const juce::var& a, const juce::var& b)
{
    if (a.isString() || b.isString()) return a.toString() == b.toString();
    return std::abs ((double) a - (double) b) <= 1.0e-4 * juce::jmax (1.0, std::abs ((double) a));
}

bool sameSnapshot (const juce::ValueTree& a, const juce::ValueTree& b, juce::String& diff)
{
    if (a.getNumProperties() != b.getNumProperties()) { diff = "property count"; return false; }
    for (int i = 0; i < a.getNumProperties(); ++i)
    {
        const auto name = a.getPropertyName (i);
        if (! b.hasProperty (name)) { diff = name.toString() + " missing"; return false; }
        if (! sameValue (a.getProperty (name), b.getProperty (name))) { diff = name.toString(); return false; }
    }
    return true;
}

// Program material at the Operating Level (-18 dBFS RMS), deterministic.
juce::AudioBuffer<float> makeSource (int kind, int n, float rmsDb)
{
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (7 + kind);
    const double fs = 48000.0;
    double phase = 0.0, hatPrev = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        float v = 0.0f;
        if (kind == 0) // voice-like: harmonic tone with syllables and breath noise
        {
            const double env = 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 3.0 * t);
            double s = 0.0;
            for (int h = 1; h <= 8; ++h) s += std::sin (phase * h) / h;
            phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / fs;
            v = (float) (env * s + 0.05 * (rng.nextFloat() * 2.0 - 1.0));
        }
        else if (kind == 1) // drums-like kit: kick, snare and hi-hat, so the energy is spread across the spectrum
        {
            const double local = std::fmod (t, 0.25);
            const double kick = 0.45 * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 120.0 * std::exp (-local * 40.0)) * local) * std::exp (-local * 12.0);
            const double snare = (std::fmod (t, 0.5) > 0.25) ? 0.35 * (rng.nextFloat() * 2.0 - 1.0) * std::exp (-local * 30.0) : 0.0;
            const double hatLocal = std::fmod (t, 0.125);
            const double white = rng.nextFloat() * 2.0 - 1.0;
            const double hat = 0.2 * (white - hatPrev) * std::exp (-hatLocal * 60.0);
            hatPrev = white;
            v = (float) std::tanh (2.0 * (kick + snare + hat)); // a glued drum bus: peaks sit closer to the RMS
        }
        else // bass-like: 55 Hz harmonic series with a slow pluck envelope
        {
            const double env = std::exp (-std::fmod (t, 0.5) * 4.0);
            double s = 0.0;
            for (int h = 1; h <= 10; ++h) s += std::sin (phase * h) / (double) h;
            phase += 2.0 * juce::MathConstants<double>::pi * 55.0 / fs;
            v = (float) (env * s);
        }
        b.setSample (0, i, v);
        b.setSample (1, i, v * 0.95f);
    }
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += (double) b.getSample (0, i) * b.getSample (0, i);
    const double rms = std::sqrt (juce::jmax (1.0e-20, acc / juce::jmax (1, n)));
    b.applyGain ((float) (juce::Decibels::decibelsToGain ((double) rmsDb) / rms));
    return b;
}

struct RunResult
{
    bool finite = true;
    float limGrDb = 0.0f; // most negative limiter gain reduction seen
    float inDb = 0.0f, outDb = 0.0f;
};

// Renders with the limiter in Brickwall mode on top of the preset, so the GR reading shows how much
// peak headroom the preset leaves at the Operating Level.
RunResult runPreset (NorthlineProcessor& p, const juce::AudioBuffer<float>& in)
{
    setParam (p, P::limOn, 1);
    setParam (p, P::limMode, 1);
    const int n = in.getNumSamples(), block = 256;
    juce::AudioBuffer<float> out (2, n), work (2, block);
    juce::MidiBuffer midi;
    std::vector<MeterFrame> frames (512);
    RunResult r;
    p.getStrip().reset();
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        work.setSize (2, len, false, false, true);
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, in, c, pos, len);
        p.processBlock (work, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
            {
                out.setSample (c, pos + i, work.getSample (c, i));
                if (! std::isfinite (work.getSample (c, i))) r.finite = false;
            }
        for (;;)
        {
            const size_t got = p.getStrip().popMeterFrames (frames.data(), frames.size());
            for (size_t i = 0; i < got; ++i) r.limGrDb = std::min (r.limGrDb, frames[i].limGr);
            if (got < frames.size()) break;
        }
    }
    const int from = (int) (0.5 * 48000.0);
    r.inDb = rmsDbRange (in, from, n);
    r.outDb = rmsDbRange (out, from, n);
    return r;
}

// One folder per process, so test runs in parallel (other worktrees, CI shards) never share files.
// It is removed when the process exits.
juce::File testFolder()
{
    struct Folder { juce::File dir; ~Folder() { dir.deleteRecursively(); } };
    static const Folder folder { juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("NorthlineChannelTests").getChildFile ("Presets-" + juce::Uuid().toString()) };
    return folder.dir;
}
} // namespace

//==============================================================================
NL_TEST (presets_libraryDefinitionsAreValid)
{
    std::printf ("Factory library: definitions\n");
    int bad = 0;
    juce::String firstError;
    for (const auto* part : { &factoryDefsPartOne(), &factoryDefsPartTwo() })
        for (const auto& def : *part)
        {
            const auto err = validateFactoryDef (def);
            if (err.isNotEmpty() && bad++ == 0) firstError = err;
        }
    check (bad == 0, "every definition parses, has known ids in range, a note and 3 to 6 Spotlight controls", firstError);

    const auto& list = factoryPresets();
    int starting = 0, character = 0, withNote = 0, spotOk = 0;
    std::set<juce::String> names;
    for (const auto& fp : list)
    {
        names.insert (fp.name);
        if (fp.family == "Starting points") ++starting;
        if (fp.family == "Character") ++character;
        if (fp.note.isNotEmpty()) ++withNote;
        if (fp.focus.size() >= 3 && fp.focus.size() <= 6) ++spotOk;
    }
    const int sounds = (int) list.size() - 1; // Init is the reset entry
    check (sounds >= 150, "at least 150 factory presets (Init excluded)", juce::String (sounds));
    check (starting >= 75 && character >= 75, "both families are populated", juce::String (starting) + " starting points, " + juce::String (character) + " character");
    check ((int) names.size() == (int) list.size(), "preset names are unique", juce::String ((int) names.size()) + " unique names");
    check (withNote == (int) list.size(), "every preset has a note");
    check (spotOk == (int) list.size(), "every preset has 3 to 6 Spotlight controls");
}

NL_TEST (presets_factoryRoundTripsThroughSaveAndLoad)
{
    std::printf ("Factory library: save and load round trip\n");
    testFolder().createDirectory();
    const auto file = testFolder().getChildFile ("roundtrip.nlpreset");
    const auto& list = factoryPresets();
    int mismatches = 0;
    juce::String firstFailure;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto a = makeProc();
        a->loadFactoryPreset (i);
        const auto before = a->makeSnapshot();
        PresetMeta meta;
        meta.category = list[(size_t) i].category;
        meta.tags = list[(size_t) i].tags;
        meta.note = list[(size_t) i].note;
        if (! a->saveUserPreset (file, list[(size_t) i].name, meta)) { ++mismatches; continue; }

        auto b = makeProc();
        b->loadUserPreset (file);
        juce::String diff;
        if (! sameSnapshot (before, b->makeSnapshot(), diff))
        {
            if (mismatches++ == 0) firstFailure = list[(size_t) i].name + ": " + diff;
        }
    }
    file.deleteFile();
    check (mismatches == 0, "every factory preset loads and round-trips through save and load exactly", firstFailure);
}

NL_TEST (presets_everyFactoryPresetIsLevelHonestAndHasHeadroom)
{
    std::printf ("Factory library: sound at the Operating Level\n");
    const int n = (int) (1.5 * 48000.0);
    const struct { const char* name; int kind; } sources[] { { "voice", 0 }, { "drums", 1 }, { "bass", 2 } };
    const auto& list = factoryPresets();
    int nonFinite = 0, hot = 0, level = 0;
    juce::String firstHot, firstLevel;
    for (const auto& s : sources)
    {
        const auto in = makeSource (s.kind, n, -18.0f);
        std::printf ("    %s source: RMS %.2f dB, peak %.2f dB\n", s.name, (double) rmsDb (in, 0, 0), (double) peakDb (in, 0, 0));
        for (int i = 0; i < (int) list.size(); ++i)
        {
            auto p = makeProc();
            p->loadFactoryPreset (i);
            const auto r = runPreset (*p, in);
            const auto tag = list[(size_t) i].name + " (" + s.name + ")";
            if (! r.finite && nonFinite++ == 0) std::printf ("    NaN or Inf: %s\n", tag.toRawUTF8());
            if (-r.limGrDb >= 0.5f) { if (hot++ == 0) firstHot = tag; std::printf ("    limiter GR %s dB: %s\n", juce::String (-r.limGrDb, 2).toRawUTF8(), tag.toRawUTF8()); }
            if (std::abs (r.outDb - r.inDb) > 6.0f) { if (level++ == 0) firstLevel = tag; std::printf ("    level %s dB: %s\n", juce::String (r.outDb - r.inDb, 2).toRawUTF8(), tag.toRawUTF8()); }
        }
    }
    check (nonFinite == 0, "no NaN or Inf in any preset on voice, drum or bass material");
    check (hot == 0, "no preset needs more than 0.5 dB of limiter gain reduction at the Operating Level", firstHot);
    check (level == 0, "every preset stays within 6 dB of the input RMS (level-honest)", firstLevel);
}

NL_TEST (presets_queryFiltersSearchesAndSorts)
{
    std::printf ("Preset browser: search, filter and sort\n");
    std::vector<PresetEntry> e (4);
    e[0].name = "Kick - Punch"; e[0].category = "Kick"; e[0].family = "Character"; e[0].tags = { "drums", "punch" }; e[0].note = "tight"; e[0].favourite = true;
    e[1].name = "Bass - Solid"; e[1].category = "Bass amp"; e[1].family = "Starting points"; e[1].note = "solid low end";
    e[2].isFactory = false; e[2].name = "My Lead"; e[2].category = "Vocals"; e[2].tags = { "vocal", "bright" };
    e[3].isFactory = false; e[3].name = "Demo kit"; e[3].category = "Drums"; e[3].folder = "Demo";

    const auto ids = [&] (PresetQuery q) { return queryPresets (e, q); };
    PresetQuery q;
    check (ids ({ .text = "punch" }) == std::vector<int> { 0 }, "search matches a name word");
    check (ids ({ .text = "drums" }) == std::vector<int> { 0, 3 }, "search matches tags and categories");
    check (ids ({ .text = "vocal bright" }) == std::vector<int> { 2 }, "every search word must match");
    check (ids ({ .text = "solid", .category = "Kick" }).empty(), "category filter combines with search");
    check (ids ({ .category = "Drums" }) == std::vector<int> { 3 }, "category filter");
    check (ids ({ .family = "Starting points" }) == std::vector<int> { 1 }, "family filter");
    check (ids ({ .favouritesOnly = true }) == std::vector<int> { 0 }, "favourites only");
    check (ids ({ .folder = "" }) == std::vector<int> { 2 }, "folder filter: user root only, factory excluded");
    check (ids ({ .folder = "Demo" }) == std::vector<int> { 3 }, "folder filter: named folder");
    check (ids ({}).size() == 4, "default query shows everything");

    q.sort = PresetQuery::Sort::Name;
    check (ids (q) == std::vector<int> { 1, 3, 0, 2 }, "sort by name is case-insensitive");
    q.sort = PresetQuery::Sort::Category;
    check (ids (q) == std::vector<int> { 1, 3, 0, 2 }, "sort by category, then name");
    q.sort = PresetQuery::Sort::Favourites;
    check (ids (q) == std::vector<int> { 0, 1, 2, 3 }, "favourites first, library order kept");
}

NL_TEST (presets_userLibraryMetadataAndFavourites)
{
    std::printf ("Preset browser: user library, metadata and favourites\n");
    const auto root = testFolder().getChildFile ("library");
    root.createDirectory();
    const auto file = root.getChildFile ("Drums").getChildFile ("Kit.nlpreset");
    auto p = makeProc();
    PresetMeta meta;
    meta.category = "Drums";
    meta.tags = { "kit", "warm" };
    meta.note = "Set the threshold first.";
    check (p->saveUserPreset (file, "Kit", meta), "user preset saves with metadata");

    const auto users = scanUserPresets (root);
    check (users.size() == 1, "scan finds the user preset in a subfolder", juce::String ((int) users.size()));
    if (! users.empty())
    {
        const auto& u = users[0];
        check (u.name == "Kit" && u.category == "Drums" && u.folder == "Drums" && u.note == "Set the threshold first.", "metadata survives the file");
        check (u.tags.size() == 2 && u.tags[0] == "kit", "tags survive the file");
    }
    check (userPresetFolders (root) == std::vector<juce::String> { "Drums" }, "folders are listed relative to the root");

    const auto library = presetLibrary (root);
    check ((int) library.size() == (int) factoryPresets().size() + 1, "library is factory presets followed by user presets");
    std::set<juce::String> keys;
    for (const auto& entry : library) keys.insert (entry.key);
    check ((int) keys.size() == (int) library.size(), "favourite keys are unique");

    const auto favFile = root.getChildFile ("favourites.xml");
    std::set<juce::String> favs { "factory:Init", "user:Drums/Kit.nlpreset" };
    check (saveFavourites (favFile, favs) && loadFavourites (favFile) == favs, "favourites round-trip through the file");
    root.deleteRecursively();
}

NL_TEST (presets_undoLabelsFormatCorrectly)
{
    std::printf ("Undo history: labels\n");
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    const auto arrow = juce::String::fromUTF8 ("\xe2\x86\x92");
    check (undoLabelForParam (P::compThresh, -18.0f, -22.0f) == "Comp Threshold " + minus + "18 " + arrow + " " + minus + "22 dB",
           "compressor threshold label with typeset minus and unit once", undoLabelForParam (P::compThresh, -18.0f, -22.0f));
    check (undoLabelForParam (P::satType, 0.0f, 4.0f) == "PRE Saturation Type: Odd " + arrow + " Tape", "choice label names the old and new choice");
    check (undoLabelForParam (P::hpOn, 0.0f, 1.0f) == "PRE HP On: Off " + arrow + " On", "bool label reads Off and On");
    check (undoLabelForParam (P::eqLowGain, 2.0f, 3.5f) == "EQ Low Gain +2.0 " + arrow + " +3.5 dB", "EQ label gets the module prefix");
    const auto master = undoLabelForParam (P::inGain, 0.0f, 3.0f);
    check (master.startsWith ("Input Gain ") && ! master.contains ("PRE"), "master parameters have no module prefix", master);
}

NL_TEST (presets_gestureAndHistoryUndoLabels)
{
    std::printf ("Undo history: gestures, undoTo and redo\n");
    auto p = makeProc();
    auto* prm = p->param (P::compThresh);
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 (-22.0f));
    prm->endChangeGesture();
    p->flushPendingUndo();

    const auto arrow = juce::String::fromUTF8 ("\xe2\x86\x92");
    const auto history = p->getUndoHistory();
    check (! history.empty() && history.back().contains ("Comp Threshold") && history.back().contains (arrow),
           "a finished gesture adds a labelled undo step", history.empty() ? juce::String() : history.back());

    p->loadFactoryPreset (1);
    check (p->getUndoHistory().back() == "Load preset: " + factoryPresets()[1].name, "preset loads are labelled");
    p->resetToDefaults();
    check (p->getUndoHistory().back() == "Reset all settings", "reset is labelled");

    const int steps = (int) p->getUndoHistory().size();
    p->undo();
    check ((int) p->getRedoHistory().size() == 1, "undo moves one step into the redo list");
    p->undoTo (0);
    check ((int) p->getUndoHistory().size() == 1 && (int) p->getRedoHistory().size() == steps - 1, "undoTo jumps to any step and keeps the redo list");
}

NL_TEST (presets_moduleCopyPasteIsOneModuleAndNoTransients)
{
    std::printf ("Module presets\n");
    auto a = makeProc();
    setParam (*a, P::compThresh, -30);
    setParam (*a, P::compRatio, 6);
    setParam (*a, P::eqLowGain, 4);
    const auto xml = a->getModuleXml (modComp, "Comp test");
    check (! xml.contains ("comp_listen"), "transient parameters are not in a module preset");
    check (xml.contains ("comp_thresh") && ! xml.contains ("eq_low_gain"), "a module preset holds only its own parameters");

    auto b = makeProc();
    check (b->pasteModuleXml (xml), "a copied module pastes into another instance");
    check (std::abs (b->param (P::compThresh)->convertFrom0to1 (b->param (P::compThresh)->getValue()) + 30.0f) < 0.01f, "the pasted threshold is applied");
    check (std::abs (b->param (P::eqLowGain)->convertFrom0to1 (b->param (P::eqLowGain)->getValue())) < 0.01f, "other modules are untouched by a paste");
    check (b->getUndoHistory().back() == "Paste COMP module", "a paste is one labelled undo step");
    check (! b->pasteModuleXml ("not a module"), "text that is not a module preset is refused");

    const auto file = testFolder().getChildFile ("comp.nlmodule");
    check (a->saveModulePreset (file, modComp, "Comp test") && b->loadModulePreset (file), "module presets save and load as files");
    file.deleteFile();
}

NL_TEST (presets_browserAndTopBarBuildHeadless)
{
    std::printf ("Top bar and browser\n");
    auto p = makeProc();
    TopBar bar (*p);
    bar.setBounds (0, 0, 1334, 44);
    bar.resized();
    bar.refresh();
    bar.setCpu (1.25f);
    setParam (*p, P::compThresh, -30);
    p->pushUndoSnapshot ("Test step");
    check (bar.handleKeyPress (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) && p->getRedoHistory().size() == 1,
           "Ctrl+Z undoes through the top bar");
    check (bar.handleKeyPress (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) && p->getRedoHistory().empty(),
           "Ctrl+Shift+Z redoes");
    bar.setSpotlightEditMode (true);
    check (bar.isSpotlightEditMode() && ! bar.isFocusMode(), "Edit Spotlight is separate from Spotlight focus");
    bar.setSpotlightEditMode (false);

    PresetBrowser browser (*p);
    browser.setBounds (0, 0, 1100, 640);
    browser.resized();
    browser.refreshLibrary();
    browser.showSavePanel();
    browser.resized();
    check (true, "preset browser builds, lays out and rescans");
}
} // namespace tool

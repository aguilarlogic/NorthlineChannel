#include "Presets.h"
#include <algorithm>
#include <map>

namespace nl
{
namespace
{
const char* const kStartingPoints = "Starting points";
const char* const kCharacter = "Character";

int indexForParamId (const juce::String& id)
{
    static const std::map<juce::String, int> table = [] { std::map<juce::String, int> m; for (int i = 0; i < P::count; ++i) m[paramID (i)] = i; return m; }();
    const auto it = table.find (id);
    return it == table.end() ? -1 : it->second;
}

juce::StringArray trimmedTokens (const char* text, const char* separators)
{
    juce::StringArray out;
    for (auto token : juce::StringArray::fromTokens (text, separators, ""))
        if (token.trim().isNotEmpty()) out.add (token.trim());
    return out;
}

// "id=value" pairs. Returns false for a malformed pair or an unknown id.
bool parsePair (const juce::String& token, int& index, float& value)
{
    const int eq = token.indexOf ("=");
    if (eq <= 0) return false;
    index = indexForParamId (token.substring (0, eq));
    if (index < 0) return false;
    const auto text = token.substring (eq + 1);
    if (! text.containsOnly ("-+.0123456789eE")) return false;
    value = text.getFloatValue();
    return true;
}

void parseValues (const char* text, std::vector<PresetValue>& out)
{
    for (auto token : juce::StringArray::fromTokens (text, " ", ""))
    {
        int index = -1;
        float value = 0.0f;
        if (parsePair (token, index, value)) out.push_back ({ index, value });
    }
}

} // namespace

//==============================================================================
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        std::vector<FactoryPreset> out;
        for (const auto* part : { &factoryDefsPartOne(), &factoryDefsPartTwo() })
            for (const auto& def : *part)
            {
                jassert (validateFactoryDef (def).isEmpty());
                FactoryPreset fp;
                fp.category = def.category;
                fp.name = def.name;
                fp.family = def.family;
                fp.note = def.note;
                for (auto tag : trimmedTokens (def.tags, ",")) fp.tags.add (tag);
                parseValues (def.values, fp.values);
                for (auto id : trimmedTokens (def.spotlight, " "))
                    if (const int i = indexForParamId (id); i >= 0) fp.focus.push_back (i);
                out.push_back (std::move (fp));
            }
        return out;
    }();
    return presets;
}

juce::String validateFactoryDef (const FactoryDef& def)
{
    const juce::String name = def.name;
    if (juce::String (def.category).isEmpty()) return name + ": no category";
    if (juce::String (def.family) != kStartingPoints && juce::String (def.family) != kCharacter) return name + ": unknown family";
    if (juce::String (def.note).trim().isEmpty()) return name + ": no note";

    for (auto token : juce::StringArray::fromTokens (def.values, " ", ""))
    {
        if (token.trim().isEmpty()) continue;
        int index = -1;
        float value = 0.0f;
        if (! parsePair (token, index, value)) return name + ": bad or unknown pair '" + token + "'";
        const auto& d = paramDef (index);
        if (d.flags & kTransient) return name + ": transient parameter " + paramID (index);
        if (value < d.minV - 1.0e-4f || value > d.maxV + 1.0e-4f) return name + ": " + paramID (index) + " out of range";
        if (d.kind == ParamDef::Bool && value != 0.0f && value != 1.0f) return name + ": " + paramID (index) + " must be 0 or 1";
        if (d.kind == ParamDef::Choice && std::abs (value - std::round (value)) > 1.0e-4f) return name + ": " + paramID (index) + " must be a choice index";
    }

    const auto spot = trimmedTokens (def.spotlight, " ");
    if (spot.size() < 3 || spot.size() > 6) return name + ": Spotlight needs 3 to 6 controls";
    for (const auto& id : spot)
    {
        const int i = indexForParamId (id);
        if (i < 0) return name + ": unknown Spotlight control " + id;
        if (paramDef (i).flags & kTransient) return name + ": transient Spotlight control " + id;
    }
    return {};
}

//==============================================================================
bool readUserPresetMeta (const juce::File& file, juce::String& name, PresetMeta& meta)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("NorthlinePreset")) return false;
    name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    meta.category = xml->getStringAttribute ("category");
    meta.tags.clear();
    for (auto tag : trimmedTokens (xml->getStringAttribute ("tags").toRawUTF8(), ",")) meta.tags.add (tag);
    meta.note = xml->getStringAttribute ("note");
    return true;
}

std::vector<PresetEntry> scanUserPresets (const juce::File& root)
{
    std::vector<PresetEntry> out;
    if (! root.isDirectory()) return out;
    auto files = root.findChildFiles (juce::File::findFiles, true, "*.nlpreset");
    files.sort();
    for (const auto& file : files)
    {
        PresetMeta meta;
        juce::String name;
        if (! readUserPresetMeta (file, name, meta)) continue;
        const juce::String rel = file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
        const int slash = rel.lastIndexOfChar ('/');

        PresetEntry e;
        e.isFactory = false;
        e.file = file;
        e.folder = slash < 0 ? juce::String() : rel.substring (0, slash);
        e.name = name;
        e.category = meta.category;
        e.tags = meta.tags;
        e.note = meta.note;
        e.key = "user:" + rel;
        out.push_back (e);
    }
    return out;
}

std::vector<PresetEntry> presetLibrary (const juce::File& userRoot)
{
    std::vector<PresetEntry> out;
    const auto& list = factoryPresets();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& fp = list[(size_t) i];
        PresetEntry e;
        e.isFactory = true;
        e.factoryIndex = i;
        e.name = fp.name;
        e.category = fp.category;
        e.family = fp.family;
        e.note = fp.note;
        e.tags = fp.tags;
        e.key = "factory:" + fp.name;
        out.push_back (e);
    }
    for (auto& e : scanUserPresets (userRoot)) out.push_back (e);
    return out;
}

std::vector<juce::String> userPresetFolders (const juce::File& root)
{
    std::vector<juce::String> out;
    if (! root.isDirectory()) return out;
    for (const auto& dir : root.findChildFiles (juce::File::findDirectories, true))
        out.push_back (dir.getRelativePathFrom (root).replaceCharacter ('\\', '/'));
    std::sort (out.begin(), out.end());
    return out;
}

//==============================================================================
std::vector<int> queryPresets (const std::vector<PresetEntry>& entries, const PresetQuery& q)
{
    const auto words = trimmedTokens (q.text.toLowerCase().toRawUTF8(), " ");
    std::vector<int> out;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (q.favouritesOnly && ! e.favourite) continue;
        if (q.category.isNotEmpty() && e.category != q.category) continue;
        if (q.family.isNotEmpty() && e.family != q.family) continue;
        if (q.folder != "*" && (e.isFactory || e.folder != q.folder)) continue;

        if (words.size() > 0)
        {
            const juce::String haystack = (e.name + " " + e.category + " " + e.family + " " + e.tags.joinIntoString (" ") + " " + e.note).toLowerCase();
            bool all = true;
            for (const auto& w : words) all = all && haystack.contains (w.toLowerCase());
            if (! all) continue;
        }
        out.push_back (i);
    }

    switch (q.sort)
    {
        case PresetQuery::Sort::Name:
            std::stable_sort (out.begin(), out.end(), [&] (int a, int b) { return entries[(size_t) a].name.compareIgnoreCase (entries[(size_t) b].name) < 0; });
            break;
        case PresetQuery::Sort::Category:
            std::stable_sort (out.begin(), out.end(), [&] (int a, int b)
            {
                const auto& ea = entries[(size_t) a];
                const auto& eb = entries[(size_t) b];
                const int c = ea.category.compareIgnoreCase (eb.category);
                return c != 0 ? c < 0 : ea.name.compareIgnoreCase (eb.name) < 0;
            });
            break;
        case PresetQuery::Sort::Favourites:
            std::stable_sort (out.begin(), out.end(), [&] (int a, int b) { return entries[(size_t) a].favourite && ! entries[(size_t) b].favourite; });
            break;
        case PresetQuery::Sort::Library:
        default:
            break;
    }
    return out;
}

//==============================================================================
juce::File userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Northline Audio")
        .getChildFile ("Northline Channel")
        .getChildFile ("Presets");
}

juce::File moduleFolder()
{
    return userPresetFolder().getSiblingFile ("Modules");
}

juce::File appDataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Northline Audio")
        .getChildFile ("Northline Channel");
}

juce::File favouritesFile()
{
    return appDataFolder().getChildFile ("favourites.xml");
}

std::set<juce::String> loadFavourites (const juce::File& file)
{
    std::set<juce::String> keys;
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("Favourites")) return keys;
    forEachXmlChildElementWithTagName (*xml, item, "Item")
        keys.insert (item->getStringAttribute ("key"));
    return keys;
}

bool saveFavourites (const juce::File& file, const std::set<juce::String>& keys)
{
    juce::XmlElement root ("Favourites");
    for (const auto& key : keys) root.createNewChildElement ("Item")->setAttribute ("key", key);
    file.getParentDirectory().createDirectory();
    return root.writeTo (file);
}
} // namespace nl

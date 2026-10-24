#pragma once

#include "dsp/ChannelStrip.h"
#include <set>

namespace nl
{
struct PresetValue
{
    int index;   // parameter index (P::...)
    float value; // plain value
};

//==============================================================================
// Factory presets are authored as text (FactoryPresetsA.cpp and FactoryPresetsB.cpp) and parsed
// once. Parameters are named by the IDs in Params.cpp, so a table edit cannot move a value onto
// the wrong control.
struct FactoryDef
{
    const char* category;  // the source: "Lead vocal", "Kick", "Mix bus", ...; "Utility" for Init
    const char* name;      // unique display name: "<source> - <descriptor>"
    const char* family;    // "Starting points" (gentle, source-specific) or "Character" (obvious colour)
    const char* tags;      // comma-separated lowercase words: source group, style, character
    const char* values;    // "param_id=value" pairs separated by single spaces; unlisted parameters keep their defaults
    const char* spotlight; // 3 to 6 parameter IDs separated by single spaces (the Spotlight controls)
    const char* note;      // one line: what to adjust first
};

// The two halves of the authored table. Part one starts with Init.
const std::vector<FactoryDef>& factoryDefsPartOne();
const std::vector<FactoryDef>& factoryDefsPartTwo();

struct FactoryPreset
{
    juce::String category, name, family, note;
    juce::StringArray tags;
    std::vector<PresetValue> values;
    std::vector<int> focus; // Spotlight parameters
    ChannelStrip::Order order = ChannelStrip::defaultOrder;
};

// All factory presets, parsed from both parts in table order. Names are unique.
const std::vector<FactoryPreset>& factoryPresets();

// Empty when the definition is valid: every ID exists, every value is inside the parameter's range,
// no transient parameter is set, Spotlight has 3 to 6 known IDs, and a note is present.
juce::String validateFactoryDef (const FactoryDef& def);

//==============================================================================
// Metadata stored with a user preset (inline save in the browser).
struct PresetMeta
{
    juce::String category;
    juce::StringArray tags;
    juce::String note;
};

// One entry of the preset library: factory presets and user presets, as the browser lists them.
struct PresetEntry
{
    bool isFactory = true;
    int factoryIndex = -1;    // isFactory: index into factoryPresets()
    juce::File file;          // user presets: the .nlpreset file
    juce::String folder;      // user presets: folder relative to userPresetFolder(), "" for the root
    juce::String name, category, family, note;
    juce::StringArray tags;
    juce::String key;         // stable id for favourites: "factory:<name>" or "user:<folder/file name>"
    bool favourite = false;   // set by the caller from loadFavourites()
};

// Factory entries in table order, then user presets (recursively, sorted by path).
std::vector<PresetEntry> presetLibrary (const juce::File& userRoot);

// User presets (*.nlpreset) under root, recursively. Metadata comes from the file.
std::vector<PresetEntry> scanUserPresets (const juce::File& root);

// Subfolders of root, relative paths with "/" separators, sorted. The root itself is not listed.
std::vector<juce::String> userPresetFolders (const juce::File& root);

// Reads the name, category, tags and note of a user preset file. Returns false when it is not one.
bool readUserPresetMeta (const juce::File& file, juce::String& name, PresetMeta& meta);

//==============================================================================
// Search, filter and sort. Pure: no I/O, no globals.
struct PresetQuery
{
    juce::String text;             // every word must appear in the name, category, family, tags or note
    juce::String category;         // "" = any category
    juce::String family;           // "" = any family
    juce::String folder = "*";     // "*" = all entries; "" = user presets in the root; "x/y" = that folder (factory entries are excluded)
    bool favouritesOnly = false;
    enum class Sort { Library, Name, Category, Favourites };
    Sort sort = Sort::Library;
};

// Indices into entries that match the query, in the requested order.
std::vector<int> queryPresets (const std::vector<PresetEntry>& entries, const PresetQuery& query);

//==============================================================================
// Locations. Favourites are global (all instances, all sessions) and live in the user's app-data folder.
juce::File userPresetFolder();       // Documents\Northline Audio\Northline Channel\Presets
juce::File appDataFolder();          // AppData\Roaming\Northline Audio\Northline Channel
juce::File favouritesFile();         // appDataFolder() \ favourites.xml
juce::File moduleFolder();           // Northline Channel \ Modules (module presets, *.nlmodule)

std::set<juce::String> loadFavourites (const juce::File& file);
bool saveFavourites (const juce::File& file, const std::set<juce::String>& keys);
} // namespace nl

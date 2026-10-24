#pragma once

#include "../PluginProcessor.h"
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace nl::hub
{
// One row of the Instance Hub table, read on the message thread.
struct InstanceInfo
{
    int id = 0;
    juce::String name;                 // track name from the host, or "Instance <id>"
    juce::uint32 colourARGB = 0;       // 0 when the host gives no colour
    juce::String preset;
    double integratedLufs = -200.0;    // -200 until the meter has integrated something
    float grDb[6] {};                  // gate, de-ess 1, de-ess 2, dynamic EQ, compressor, limiter (see ChannelStrip::getHubGrDb)
    int latencySamples = 0;
    float cpuPercent = 0.0f;           // smoothed share of the block duration
};

// A group links chosen parameters across instances: a change on one member moves the others by the
// same delta, so their offsets are kept.
struct GroupInfo
{
    int id = 0;
    juce::String name;
    std::vector<int> members;          // instance ids
    std::vector<int> params;           // P::Index values
};

// What the registry can see. It lists Northline instances of this process only.
struct Scope
{
    int visibleInstances = 0;
    bool limitedToProcess = true;      // always true: other processes cannot be seen from inside a plug-in
};

// Process-wide registry of Northline instances (roadmap 9.2). A singleton used on the message thread
// only. It holds weak references: an instance that is destroyed disappears from the registry, and its
// entry is removed by the processor's destructor as well.
//
// Limitation, documented in the Hub: hosts that run plug-ins in separate processes (plug-in
// sandboxing, bridged 32/64-bit plug-ins) keep their other instances out of this view. A plug-in cannot
// see another process, so the Hub always states this limit (see limitationText()).
class InstanceRegistry
{
public:
    static InstanceRegistry& instance();

    // Registers a live instance and returns its id. Called from the processor's constructor.
    int add (NorthlineProcessor& processor);
    // Removes an instance (and its group memberships). Called from the processor's destructor.
    void remove (int id);
    // The live processor with this id, or nullptr.
    NorthlineProcessor* find (int id) const;
    int count() const;
    // One row per live instance, in registration order.
    std::vector<InstanceInfo> snapshot() const;

    // Groups. Parameters are linked per group; members are instances.
    int createGroup (const juce::String& name);
    void removeGroup (int groupId);
    bool addMember (int groupId, int instanceId);
    bool removeMember (int groupId, int instanceId);
    bool linkParam (int groupId, int paramIndex);
    bool unlinkParam (int groupId, int paramIndex);
    std::vector<GroupInfo> groups() const;

    // Moves the members of each group by the delta of a linked parameter that changed on one member.
    // The caches are refreshed after every move, so a move is propagated once and cannot feed back.
    // If two members move within one poll, the first one in list order wins. Runs from a 30 Hz timer
    // while groups exist; tests call it directly. Returns the number of parameter writes made.
    int poll();

    static Scope scope (const InstanceRegistry& registry);
    static juce::String limitationText();

private:
    struct Entry
    {
        int id;
        juce::WeakReference<NorthlineProcessor> processor;
    };
    struct Group
    {
        int id = 0;
        juce::String name;
        std::vector<int> members, params;
        std::map<std::pair<int, int>, float> last; // (instance, parameter) -> plain value at the last poll
    };

    InstanceRegistry() = default;
    void dropMissingMembers (Group& g);
    void refreshCache (Group& g, int instance, int param, const NorthlineProcessor& p);
    void updateTimer();

    std::vector<Entry> entries;
    std::vector<Group> groupList;
    int nextId = 1, nextGroupId = 1;
    std::unique_ptr<juce::Timer> poller;
};
} // namespace nl::hub

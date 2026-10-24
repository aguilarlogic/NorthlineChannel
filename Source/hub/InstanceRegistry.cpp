#include "InstanceRegistry.h"
#include <algorithm>
#include <cmath>

namespace nl::hub
{
namespace
{
// Drives Group propagation while any group exists.
class GroupPoller : public juce::Timer
{
public:
    void timerCallback() override { InstanceRegistry::instance().poll(); }
};

constexpr int kPollIntervalMs = 30;

float readPlain (const NorthlineProcessor& p, int index)
{
    auto* prm = p.param (index);
    return prm->convertFrom0to1 (prm->getValue());
}

void writePlain (NorthlineProcessor& p, int index, float plain)
{
    auto* prm = p.param (index);
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    prm->endChangeGesture();
}
} // namespace

InstanceRegistry& InstanceRegistry::instance()
{
    // Never destroyed: processors may be deleted during host shutdown, after static destructors would run.
    static InstanceRegistry* registry = new InstanceRegistry();
    return *registry;
}

int InstanceRegistry::add (NorthlineProcessor& processor)
{
    const int id = nextId++;
    entries.push_back ({ id, juce::WeakReference<NorthlineProcessor> (&processor) });
    return id;
}

void InstanceRegistry::remove (int id)
{
    entries.erase (std::remove_if (entries.begin(), entries.end(), [id] (const Entry& e) { return e.id == id; }), entries.end());
    for (auto& g : groupList)
    {
        g.members.erase (std::remove (g.members.begin(), g.members.end(), id), g.members.end());
        for (auto it = g.last.begin(); it != g.last.end();)
            it = it->first.first == id ? g.last.erase (it) : std::next (it);
    }
}

NorthlineProcessor* InstanceRegistry::find (int id) const
{
    for (const auto& e : entries)
        if (e.id == id) return e.processor.get();
    return nullptr;
}

int InstanceRegistry::count() const
{
    int n = 0;
    for (const auto& e : entries) n += e.processor.get() != nullptr ? 1 : 0;
    return n;
}

std::vector<InstanceInfo> InstanceRegistry::snapshot() const
{
    std::vector<InstanceInfo> rows;
    for (const auto& e : entries)
    {
        auto* p = e.processor.get();
        if (p == nullptr) continue;

        InstanceInfo info;
        info.id = e.id;
        info.name = p->getTrackName();
        if (info.name.isEmpty()) info.name = "Instance " + juce::String (e.id);
        info.colourARGB = p->getTrackColourARGB();
        info.preset = p->getPresetName();
        info.integratedLufs = p->getStrip().meterHub().read().integrated;
        for (int i = 0; i < 6; ++i) info.grDb[i] = p->getStrip().getHubGrDb (i);
        info.latencySamples = p->getLatencySamples();
        info.cpuPercent = p->getCpuLoadPercent();
        rows.push_back (info);
    }
    return rows;
}

//==============================================================================
int InstanceRegistry::createGroup (const juce::String& name)
{
    Group g;
    g.id = nextGroupId++;
    g.name = name;
    groupList.push_back (g);
    updateTimer();
    return g.id;
}

void InstanceRegistry::removeGroup (int groupId)
{
    groupList.erase (std::remove_if (groupList.begin(), groupList.end(), [groupId] (const Group& g) { return g.id == groupId; }), groupList.end());
    updateTimer();
}

bool InstanceRegistry::addMember (int groupId, int instanceId)
{
    auto* p = find (instanceId);
    for (auto& g : groupList)
    {
        if (g.id != groupId) continue;
        if (p == nullptr || std::find (g.members.begin(), g.members.end(), instanceId) != g.members.end()) return false;
        g.members.push_back (instanceId);
        for (int param : g.params) refreshCache (g, instanceId, param, *p);
        return true;
    }
    return false;
}

bool InstanceRegistry::removeMember (int groupId, int instanceId)
{
    for (auto& g : groupList)
    {
        if (g.id != groupId) continue;
        const auto before = g.members.size();
        g.members.erase (std::remove (g.members.begin(), g.members.end(), instanceId), g.members.end());
        for (auto it = g.last.begin(); it != g.last.end();)
            it = it->first.first == instanceId ? g.last.erase (it) : std::next (it);
        return g.members.size() != before;
    }
    return false;
}

bool InstanceRegistry::linkParam (int groupId, int paramIndex)
{
    if (paramIndex < 0 || paramIndex >= P::count) return false;
    for (auto& g : groupList)
    {
        if (g.id != groupId) continue;
        if (std::find (g.params.begin(), g.params.end(), paramIndex) != g.params.end()) return false;
        g.params.push_back (paramIndex);
        for (int member : g.members)
            if (auto* p = find (member)) refreshCache (g, member, paramIndex, *p);
        return true;
    }
    return false;
}

bool InstanceRegistry::unlinkParam (int groupId, int paramIndex)
{
    for (auto& g : groupList)
    {
        if (g.id != groupId) continue;
        const auto before = g.params.size();
        g.params.erase (std::remove (g.params.begin(), g.params.end(), paramIndex), g.params.end());
        for (auto it = g.last.begin(); it != g.last.end();)
            it = it->first.second == paramIndex ? g.last.erase (it) : std::next (it);
        return g.params.size() != before;
    }
    return false;
}

std::vector<GroupInfo> InstanceRegistry::groups() const
{
    std::vector<GroupInfo> out;
    for (const auto& g : groupList) out.push_back ({ g.id, g.name, g.members, g.params });
    return out;
}

//==============================================================================
void InstanceRegistry::dropMissingMembers (Group& g)
{
    g.members.erase (std::remove_if (g.members.begin(), g.members.end(), [this] (int id) { return find (id) == nullptr; }), g.members.end());
}

void InstanceRegistry::refreshCache (Group& g, int instance, int param, const NorthlineProcessor& p)
{
    g.last[{ instance, param }] = readPlain (p, param);
}

int InstanceRegistry::poll()
{
    int writes = 0;
    for (auto& g : groupList)
    {
        dropMissingMembers (g);
        if (g.members.size() < 2) continue;

        for (int param : g.params)
        {
            const auto& def = paramDef (param);
            const bool isFloat = def.kind == ParamDef::Float;

            // The first member whose value moved since the last poll is the one the user moved.
            int source = -1;
            float delta = 0.0f, value = 0.0f;
            for (int id : g.members)
            {
                const auto* p = find (id);
                const float now = readPlain (*p, param);
                const auto it = g.last.find ({ id, param });
                if (it == g.last.end()) { g.last[{ id, param }] = now; continue; }
                if (source < 0 && std::abs (now - it->second) > 1.0e-6f)
                {
                    source = id;
                    delta = now - it->second;
                    value = now;
                }
            }

            if (source >= 0)
            {
                for (int id : g.members)
                {
                    if (id == source) continue;
                    auto* p = find (id);
                    const float current = readPlain (*p, param);
                    // Floats move by the same delta (clamped to the range); switches and choices take the value.
                    const float target = isFloat ? std::clamp (current + delta, def.minV, def.maxV) : value;
                    if (std::abs (target - current) > 1.0e-6f)
                    {
                        writePlain (*p, param, target);
                        ++writes;
                    }
                }
            }

            // Refresh every member's cache from its real value, so the writes above are not seen again.
            for (int id : g.members)
                if (auto* p = find (id)) refreshCache (g, id, param, *p);
        }
    }
    return writes;
}

void InstanceRegistry::updateTimer()
{
    if (groupList.empty())
    {
        if (poller != nullptr) poller->stopTimer();
        return;
    }
    if (poller == nullptr) poller = std::make_unique<GroupPoller>();
    poller->startTimer (kPollIntervalMs);
}

Scope InstanceRegistry::scope (const InstanceRegistry& registry)
{
    Scope s;
    s.visibleInstances = registry.count();
    return s;
}

juce::String InstanceRegistry::limitationText()
{
    return "The Hub lists Northline instances of this process only. Hosts that run plug-ins in separate "
           "processes (plug-in sandboxing, bridged 32/64-bit plug-ins) keep their other instances out of view, "
           "and a plug-in cannot see another process.";
}
} // namespace nl::hub

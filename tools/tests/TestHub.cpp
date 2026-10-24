#include "../TestUtil.h"
#include "hub/InstanceRegistry.h"
#include "gui/HubPanel.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>

// Instance Hub (roadmap 9.2): the in-process registry, host track names and colours, and groups that
// move parameters by a shared delta without feedback.

using namespace tool;
using namespace nl;

namespace
{
// Writes a component snapshot as PNG. Used only by the development snapshot tests.
static void writeSnapshot (juce::Component& c, const juce::File& file)
{
    juce::Image img = c.createComponentSnapshot (c.getLocalBounds());
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    }
}

float plainOf (NorthlineProcessor& p, int index)
{
    auto* prm = p.param (index);
    return prm->convertFrom0to1 (prm->getValue());
}

bool near (float a, float b, float tol = 0.01f) { return std::abs (a - b) <= tol; }
} // namespace

NL_TEST (hub_registry_add_remove_and_weak_references)
{
    auto& reg = hub::InstanceRegistry::instance();
    const int before = reg.count();

    auto a = makeProc();
    auto b = makeProc();
    check (reg.count() == before + 2, "each new instance registers itself", juce::String (reg.count() - before) + " added");
    const int idA = a->getHubId(), idB = b->getHubId();
    check (reg.find (idA) == a.get() && reg.find (idB) == b.get(), "find returns the live processors");

    juce::AudioProcessor::TrackProperties props;
    props.name = "Kick In";
    props.colourARGB = (juce::uint32) 0xffe04040;
    a->updateTrackProperties (props);
    const auto rows = reg.snapshot();
    const auto row = std::find_if (rows.begin(), rows.end(), [idA] (const hub::InstanceInfo& r) { return r.id == idA; });
    check (row != rows.end() && row->name == "Kick In", "the host track name reaches the Hub row");
    check (row != rows.end() && row->colourARGB == (juce::uint32) 0xffe04040, "the host track colour reaches the Hub row");
    check (rows.size() == (size_t) reg.count(), "one row per live instance");

    // Weak references: a destroyed instance is gone from the registry and from groups.
    const int g = reg.createGroup ("weak");
    check (reg.addMember (g, idA) && reg.addMember (g, idB), "both instances join a group");
    b.reset();
    check (reg.find (idB) == nullptr, "a destroyed instance is not found (no dangling pointer)");
    check (reg.count() == before + 1, "a destroyed instance leaves the registry");
    const auto groups = reg.groups();
    const auto mine = std::find_if (groups.begin(), groups.end(), [g] (const hub::GroupInfo& x) { return x.id == g; });
    check (mine != groups.end() && mine->members.size() == 1 && mine->members[0] == idA, "the group drops the destroyed member");
    check (reg.poll() == 0, "polling with a destroyed member is safe");

    reg.removeGroup (g);
    a.reset();
    check (reg.count() == before, "all instances removed again");
}

NL_TEST (hub_group_moves_members_by_the_same_delta_without_feedback)
{
    auto& reg = hub::InstanceRegistry::instance();
    auto a = makeProc();
    auto b = makeProc();
    auto c = makeProc();
    setParam (*a, P::compThresh, -18);
    setParam (*b, P::compThresh, -24);
    setParam (*c, P::compThresh, -12);

    const int g = reg.createGroup ("Drum compressors");
    reg.addMember (g, a->getHubId());
    reg.addMember (g, b->getHubId());
    reg.addMember (g, c->getHubId());
    check (reg.linkParam (g, P::compThresh), "the compressor threshold is linked");

    // A moves by -4 dB: B and C move by the same -4 dB, so their offsets to A stay as they were.
    setParam (*a, P::compThresh, -22);
    const int writes = reg.poll();
    check (writes == 2, "one change on A moves the two other members", juce::String (writes) + " writes");
    check (near (plainOf (*b, P::compThresh), -28.0f) && near (plainOf (*c, P::compThresh), -16.0f),
           "the other thresholds move by the same delta (B -28, C -16)",
           juce::String (plainOf (*b, P::compThresh), 2) + " and " + juce::String (plainOf (*c, P::compThresh), 2));

    check (reg.poll() == 0, "no feedback: a poll with nothing moved writes nothing");
    check (near (plainOf (*a, P::compThresh), -22.0f), "the moved instance keeps its value");

    // B moves by +2 dB: A and C follow.
    setParam (*b, P::compThresh, -26);
    reg.poll();
    check (near (plainOf (*a, P::compThresh), -20.0f) && near (plainOf (*c, P::compThresh), -14.0f),
           "a move on B is carried to A and C (A -20, C -14)");
    check (reg.poll() == 0, "still no feedback after the second move");

    // A switch takes the same value on every member (mirror, not a delta).
    check (reg.linkParam (g, P::compType), "the compressor type is linked");
    reg.poll();
    setParam (*a, P::compType, 2);
    reg.poll();
    check (near (plainOf (*b, P::compType), 2.0f, 0.5f) && near (plainOf (*c, P::compType), 2.0f, 0.5f),
           "a choice is mirrored to every member");

    reg.removeGroup (g);
    const auto remaining = reg.groups();
    check (std::none_of (remaining.begin(), remaining.end(), [g] (const hub::GroupInfo& x) { return x.id == g; }),
           "a removed group is gone");
}

NL_TEST (hub_scope_is_declared_as_this_process_only)
{
    const auto scope = hub::InstanceRegistry::scope (hub::InstanceRegistry::instance());
    check (scope.limitedToProcess, "the Hub declares that it sees this process only");
    check (hub::InstanceRegistry::limitationText().isNotEmpty(), "the limitation is documented in the text the Hub shows");

    auto a = makeProc();
    const auto rows = hub::InstanceRegistry::instance().snapshot();
    const auto row = std::find_if (rows.begin(), rows.end(), [&a] (const hub::InstanceInfo& r) { return r.id == a->getHubId(); });
    check (row != rows.end() && row->name == "Instance " + juce::String (a->getHubId()), "an instance without a host name gets a default name");
    check (row != rows.end() && std::isfinite (row->cpuPercent) && row->integratedLufs <= 0.0, "the row carries finite CPU and loudness values");
}

// Development aid, not a behaviour check: with NL_SNAPSHOT_DIR set, shows three instances with names,
// colours and some gain reduction, one group, and writes the Hub panel to hub.png. Does nothing otherwise.
NL_TEST (hub_panel_snapshot_for_review)
{
    const char* dir = std::getenv ("NL_SNAPSHOT_DIR");
    if (dir == nullptr) return;
    auto a = makeProc();
    auto b = makeProc();
    auto c = makeProc();
    const char* names[] { "Kick In", "Snare Top", "Bass DI" };
    const juce::uint32 trackColours[] { 0xffe04040, 0xffe9d35b, 0xff4fa3f7 };
    NorthlineProcessor* all[] { a.get(), b.get(), c.get() };
    for (int i = 0; i < 3; ++i)
    {
        juce::AudioProcessor::TrackProperties props;
        props.name = names[i];
        props.colourARGB = trackColours[i];
        all[i]->updateTrackProperties (props);
    }
    setParam (*a, P::compOn, 1);
    setParam (*a, P::compThresh, -30);
    setParam (*a, P::compRatio, 6);
    auto in = sine (48000.0, 120.0, 0.3f, 48000);
    render (*a, in, 256);

    auto& reg = hub::InstanceRegistry::instance();
    const int g = reg.createGroup ("Drum compressors");
    reg.addMember (g, a->getHubId());
    reg.addMember (g, b->getHubId());
    reg.linkParam (g, P::compThresh);

    HubPanel panel;
    panel.setSize (1020, 420);
    writeSnapshot (panel, juce::File (dir).getChildFile ("hub.png"));
    reg.removeGroup (g);
    check (true, "hub panel snapshot written");
}

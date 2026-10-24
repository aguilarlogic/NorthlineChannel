#include "HubPanel.h"
#include <algorithm>
#include <cmath>

namespace nl
{
namespace
{
enum Column { colName = 1, colLufs, colGate, colDeEss, colEq, colComp, colLimit, colLatency, colCpu };
enum Filter { filterAll = 1, filterReducing, filterHot };
constexpr int kPad = 16;
constexpr int kRowHeight = 26;
constexpr float kHotLufs = -14.0f;
constexpr float kReducingDb = 0.5f;

juce::String grText (float grDb)
{
    const float a = std::abs (grDb);
    return a < 0.05f ? juce::String ("-") : juce::String (a, 1);
}
} // namespace

HubPanel::HubPanel()
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    heading.setText ("Instance Hub", juce::dontSendNotification);
    heading.setFont (NorthlineLookAndFeel::font (18.0f, true));
    heading.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (heading);
    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onClose) onClose(); };

    scopeNote.setMultiLine (true, true);
    scopeNote.setReadOnly (true);
    scopeNote.setScrollbarsShown (false);
    scopeNote.setText (hub::InstanceRegistry::limitationText(), juce::dontSendNotification);
    scopeNote.setFont (NorthlineLookAndFeel::font (12.0f));
    scopeNote.setColour (juce::TextEditor::textColourId, colours::dim);
    scopeNote.setColour (juce::TextEditor::backgroundColourId, colours::panelHeader);
    scopeNote.setColour (juce::TextEditor::outlineColourId, colours::edge);
    addAndMakeVisible (scopeNote);

    search.setTextToShowWhenEmpty ("Search instances", colours::faint);
    search.setFont (NorthlineLookAndFeel::font (12.0f));
    search.onTextChange = [this] { rebuildRows(); repaint (); };
    addAndMakeVisible (search);

    filterBox.addItem ("All instances", filterAll);
    filterBox.addItem ("Gain reduction now", filterReducing);
    filterBox.addItem ("Hot: above -14 LUFS", filterHot);
    filterBox.setSelectedId (filterAll, juce::dontSendNotification);
    filterBox.onChange = [this] { rebuildRows(); };
    addAndMakeVisible (filterBox);

    table.getHeader().addColumn ("Instance", colName, 200, 120, 420, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("LUFS", colLufs, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("Gate GR", colGate, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("De-Ess GR", colDeEss, 90, 60, 130, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("EQ GR", colEq, 70, 50, 110, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("Comp GR", colComp, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("Limit GR", colLimit, 80, 60, 120, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("Latency", colLatency, 90, 70, 130, juce::TableHeaderComponent::defaultFlags);
    table.getHeader().addColumn ("CPU", colCpu, 70, 50, 110, juce::TableHeaderComponent::defaultFlags);
    table.setModel (this);
    table.setMultipleSelectionEnabled (true);
    table.setRowHeight (kRowHeight);
    table.setColour (juce::ListBox::backgroundColourId, colours::panel);
    addAndMakeVisible (table);

    // Group controls.
    groupBox.onChange = [this] { refreshGroups(); };
    addAndMakeVisible (groupBox);
    for (int i = 0; i < P::count; ++i)
    {
        if (isTransient (i)) continue;
        paramChoices.push_back (i);
        paramBox.addItem (paramDef (i).name, (int) paramChoices.size());
    }
    for (size_t k = 0; k < paramChoices.size(); ++k)
        if (paramChoices[k] == P::compThresh) paramBox.setSelectedId ((int) k + 1, juce::dontSendNotification);
    addAndMakeVisible (paramBox);
    newGroupButton.onClick = [this]
    {
        auto& reg = hub::InstanceRegistry::instance();
        const int id = reg.createGroup ("Group " + juce::String (++groupCount));
        refreshGroups();
        groupBox.setSelectedId (id, juce::sendNotification);
    };
    addSelectedButton.onClick = [this]
    {
        const int gid = selectedGroupId();
        if (gid == 0) return;
        for (int i = 0; i < table.getNumSelectedRows(); ++i)
        {
            const int row = table.getSelectedRow (i);
            if (row >= 0 && row < (int) rows.size()) hub::InstanceRegistry::instance().addMember (gid, rows[(size_t) row].id);
        }
        refreshGroups();
    };
    linkButton.onClick = [this]
    {
        const int gid = selectedGroupId();
        const int pid = paramBox.getSelectedId();
        if (gid != 0 && pid > 0 && pid <= (int) paramChoices.size())
            hub::InstanceRegistry::instance().linkParam (gid, paramChoices[(size_t) pid - 1]);
        refreshGroups();
    };
    addAndMakeVisible (newGroupButton);
    addAndMakeVisible (addSelectedButton);
    addAndMakeVisible (linkButton);
    groupInfo.setFont (NorthlineLookAndFeel::font (12.0f));
    groupInfo.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (groupInfo);

    refreshGroups();
    rebuildRows();
    startTimerHz (30);
}

HubPanel::~HubPanel()
{
    stopTimer();
    table.setModel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
bool HubPanel::matches (const hub::InstanceInfo& r) const
{
    const auto needle = search.getText().trim();
    if (needle.isNotEmpty() && ! r.name.containsIgnoreCase (needle)) return false;
    switch (filterBox.getSelectedId())
    {
        case filterReducing:
            for (float g : r.grDb)
                if (std::abs (g) >= kReducingDb) return true;
            return false;
        case filterHot: return r.integratedLufs > kHotLufs;
        default: return true;
    }
}

void HubPanel::rebuildRows()
{
    std::vector<hub::InstanceInfo> next;
    for (const auto& r : hub::InstanceRegistry::instance().snapshot())
        if (matches (r)) next.push_back (r);

    const bool sameIds = next.size() == rows.size()
                         && std::equal (next.begin(), next.end(), rows.begin(), [] (const hub::InstanceInfo& a, const hub::InstanceInfo& b) { return a.id == b.id; });
    rows = std::move (next);
    if (! sameIds) table.updateContent();
    table.repaint();
}

void HubPanel::refreshGroups()
{
    const int keep = selectedGroupId();
    groupBox.clear (juce::dontSendNotification);
    const auto groups = hub::InstanceRegistry::instance().groups();
    for (const auto& g : groups) groupBox.addItem (g.name, g.id);
    if (keep != 0) groupBox.setSelectedId (keep, juce::dontSendNotification);
    else if (! groups.empty()) groupBox.setSelectedId (groups.front().id, juce::dontSendNotification);

    const int sel = selectedGroupId();
    juce::String text = "No group yet. A group links parameters across instances: moving one moves the others by the same amount.";
    for (const auto& g : groups)
    {
        if (g.id != sel) continue;
        juce::StringArray names;
        for (int p : g.params) names.add (paramDef (p).name);
        text = g.name + ": " + juce::String ((int) g.members.size()) + " member" + (g.members.size() == 1 ? "" : "s")
               + ", linked: " + (names.isEmpty() ? juce::String ("nothing yet") : names.joinIntoString (", "));
    }
    groupInfo.setText (text, juce::dontSendNotification);
}

int HubPanel::selectedGroupId() const
{
    return groupBox.getSelectedId();
}

void HubPanel::timerCallback()
{
    rebuildRows();
    if ((int) hub::InstanceRegistry::instance().groups().size() != groupBox.getNumItems()) refreshGroups();
}

//==============================================================================
int HubPanel::getNumRows() { return (int) rows.size(); }

void HubPanel::paintRowBackground (juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused (rowNumber);
    g.fillAll (rowIsSelected ? colours::track : colours::panel);
    g.setColour (colours::edge);
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void HubPanel::paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused (rowIsSelected);
    if (rowNumber < 0 || rowNumber >= (int) rows.size()) return;
    const auto& r = rows[(size_t) rowNumber];

    juce::String text;
    int justify = juce::Justification::centredRight;
    switch (columnId)
    {
        case colName:
        {
            const auto swatch = r.colourARGB != 0 ? juce::Colour (r.colourARGB) : colours::faint;
            g.setColour (swatch);
            g.fillRoundedRectangle (6.0f, (float) height * 0.5f - 4.0f, 8.0f, 8.0f, 2.0f);
            text = r.name;
            justify = juce::Justification::centredLeft;
            g.setColour (colours::text);
            g.setFont (NorthlineLookAndFeel::font (12.0f, true));
            g.drawText (text, juce::Rectangle<int> (20, 0, width - 24, height), juce::Justification::centredLeft, true);
            return;
        }
        case colLufs: text = r.integratedLufs < -150.0 ? juce::String ("-") : juce::String (r.integratedLufs, 1); break;
        case colGate: text = grText (r.grDb[0]); break;
        case colDeEss: text = grText (std::max (std::abs (r.grDb[1]), std::abs (r.grDb[2]))); break;
        case colEq: text = grText (r.grDb[3]); break;
        case colComp: text = grText (r.grDb[4]); break;
        case colLimit: text = grText (r.grDb[5]); break;
        case colLatency: text = juce::String (r.latencySamples) + " smp"; break;
        case colCpu: text = juce::String (r.cpuPercent, 1) + " %"; break;
        default: break;
    }
    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (12.0f));
    g.drawText (text, juce::Rectangle<int> (6, 0, width - 12, height), justify, true);
}

void HubPanel::cellClicked (int rowNumber, int columnId, const juce::MouseEvent&)
{
    juce::ignoreUnused (columnId);
    if (rowNumber < 0 || rowNumber >= (int) rows.size()) return;
    if (onEditInstance) onEditInstance (rows[(size_t) rowNumber].id);
}

//==============================================================================
void HubPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::edge);
    g.drawRect (getLocalBounds(), 1);
}

void HubPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad);

    auto top = area.removeFromTop (30);
    closeButton.setBounds (top.removeFromRight (84).reduced (0, 2));
    heading.setBounds (top);
    area.removeFromTop (6);

    scopeNote.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    auto filters = area.removeFromTop (28);
    filterBox.setBounds (filters.removeFromRight (200));
    filters.removeFromRight (10);
    search.setBounds (filters);
    area.removeFromTop (8);

    auto groupsArea = area.removeFromBottom (96);
    area.removeFromBottom (8);
    table.setBounds (area);

    auto row1 = groupsArea.removeFromTop (28);
    groupBox.setBounds (row1.removeFromLeft (200));
    row1.removeFromLeft (8);
    newGroupButton.setBounds (row1.removeFromLeft (120));
    row1.removeFromLeft (8);
    addSelectedButton.setBounds (row1.removeFromLeft (140));
    groupsArea.removeFromTop (6);

    auto row2 = groupsArea.removeFromTop (28);
    paramBox.setBounds (row2.removeFromLeft (260));
    row2.removeFromLeft (8);
    linkButton.setBounds (row2.removeFromLeft (150));
    groupsArea.removeFromTop (6);
    groupInfo.setBounds (groupsArea);
}
} // namespace nl

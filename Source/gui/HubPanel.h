#pragma once

#include "../hub/InstanceRegistry.h"
#include "LookAndFeel.h"
#include <functional>
#include <memory>

namespace nl
{
// Instance Hub overlay (roadmap 9.2): a table of the Northline instances in this process, with search and
// a filter, plus the group controls. Clicking a row selects that instance and calls onEditInstance (id);
// the editor decides what to show for it. A standalone component: the editor sizes it to the content
// area and shows it above everything; onClose fires on Escape.
class HubPanel : public juce::Component, private juce::Timer, private juce::TableListBoxModel
{
public:
    HubPanel();
    ~HubPanel() override;

    void resized() override;

    std::function<void()> onClose;
    std::function<void (int instanceId)> onEditInstance;

private:
    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellClicked (int rowNumber, int columnId, const juce::MouseEvent&) override;

    void timerCallback() override;
    void rebuildRows();
    void refreshGroups();
    int selectedGroupId() const;
    bool matches (const hub::InstanceInfo& r) const;

    NorthlineLookAndFeel lookAndFeel;
    juce::TextEditor scopeNote, search;
    juce::ComboBox filterBox, groupBox, paramBox;
    juce::TextButton newGroupButton { "New group" }, addSelectedButton { "Add selected" }, linkButton { "Link parameter" };
    juce::Label groupInfo;
    juce::TableListBox table;

    std::vector<hub::InstanceInfo> rows;
    std::vector<int> paramChoices; // P::Index behind the parameter list
    int groupCount = 0;
};
} // namespace nl

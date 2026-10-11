#include "TopBar.h"

namespace nl
{
namespace
{
const float kScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
}

TopBar::TopBar (NorthlineProcessor& p) : proc (p)
{
    for (auto* b : { &prev, &next, &presetButton, &save, &slotA, &slotB, &copyAB, &undoButton, &redoButton, &focusButton, &resetButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, colours::eq);
        addAndMakeVisible (b);
    }
    addAndMakeVisible (scale);
    for (int i = 0; i < 5; ++i) scale.addItem (juce::String (juce::roundToInt (kScales[i] * 100)) + "%", i + 1);
    scale.setTooltip ("Interface size");

    prev.setTooltip ("Previous factory preset");
    next.setTooltip ("Next factory preset");
    presetButton.setTooltip ("Browse presets");
    save.setTooltip ("Save a user preset");
    slotA.setTooltip ("Compare: setting A");
    slotB.setTooltip ("Compare: setting B");
    copyAB.setTooltip ("Copy the active setting to the other slot");
    undoButton.setTooltip ("Undo (32 levels, includes module order)");
    redoButton.setTooltip ("Redo");
    focusButton.setTooltip ("FOCUS: dim everything except the controls the preset highlights");
    resetButton.setTooltip ("Reset all settings");
    focusButton.setClickingTogglesState (true);
    focusButton.setColour (juce::TextButton::buttonOnColourId, colours::focus.darker (0.3f));

    prev.onClick = [this] { stepPreset (-1); };
    next.onClick = [this] { stepPreset (+1); };
    presetButton.onClick = [this] { showPresetMenu(); };
    save.onClick = [this] { savePreset(); };
    slotA.onClick = [this] { proc.selectSlot (0); refresh(); };
    slotB.onClick = [this] { proc.selectSlot (1); refresh(); };
    copyAB.onClick = [this] { proc.copyActiveToOther(); };
    undoButton.onClick = [this] { proc.undo(); refresh(); };
    redoButton.onClick = [this] { proc.redo(); refresh(); };
    resetButton.onClick = [this] { proc.resetToDefaults(); refresh(); };
    focusButton.onClick = [this] { if (onFocusChanged) onFocusChanged(); };
    scale.onChange = [this]
    {
        const int i = scale.getSelectedItemIndex();
        if (i >= 0) { proc.setUiScale (kScales[i]); if (onScaleChanged) onScaleChanged(); }
    };
    refresh();
}

void TopBar::refresh()
{
    presetButton.setButtonText (proc.getPresetName());
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    copyAB.setButtonText (proc.getActiveSlot() == 0 ? "A>B" : "B>A");
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    for (int i = 0; i < 5; ++i)
        if (std::abs (kScales[i] - proc.getUiScale()) < 0.01f) scale.setSelectedItemIndex (i, juce::dontSendNotification);
}

void TopBar::setCpu (float percent)
{
    const auto t = "CPU " + juce::String (percent, 2) + "%";
    if (t != cpuText) { cpuText = t; repaint(); }
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panelHeader);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    auto r = getLocalBounds().reduced (12, 0);
    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (18.0f, true));
    g.drawText ("NORTHLINE", r.removeFromLeft (112), juce::Justification::centredLeft);
    g.setColour (colours::eq);
    g.setFont (NorthlineLookAndFeel::font (12.0f, true));
    g.drawText ("CHANNEL", r.removeFromLeft (70), juce::Justification::centredLeft);

    g.setColour (colours::dim);
    g.setFont (NorthlineLookAndFeel::font (12.0f));
    g.drawText (cpuText, getLocalBounds().reduced (12, 0).removeFromRight (200).withTrimmedRight (82), juce::Justification::centredRight);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (10, 9);
    r.removeFromLeft (190);
    scale.setBounds (r.removeFromRight (72));
    r.removeFromRight (110);

    prev.setBounds (r.removeFromLeft (24));
    r.removeFromLeft (3);
    presetButton.setBounds (r.removeFromLeft (190));
    r.removeFromLeft (3);
    next.setBounds (r.removeFromLeft (24));
    r.removeFromLeft (6);
    save.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (18);
    slotA.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (3);
    slotB.setBounds (r.removeFromLeft (28));
    r.removeFromLeft (3);
    copyAB.setBounds (r.removeFromLeft (44));
    r.removeFromLeft (18);
    undoButton.setBounds (r.removeFromLeft (56));
    r.removeFromLeft (3);
    redoButton.setBounds (r.removeFromLeft (56));
    r.removeFromLeft (18);
    focusButton.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (6);
    resetButton.setBounds (r.removeFromLeft (60));
}

void TopBar::stepPreset (int delta)
{
    const int n = (int) factoryPresets().size();
    factoryIndex = (factoryIndex + delta + n) % n;
    proc.loadFactoryPreset (factoryIndex);
    refresh();
}

void TopBar::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& list = factoryPresets();
    juce::StringArray categories;
    for (auto& fp : list)
        if (fp.category.isNotEmpty()) categories.addIfNotAlreadyThere (fp.category);

    menu.addSectionHeader ("Factory");
    menu.addItem (1, "Init");
    for (auto& cat : categories)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].category == cat) sub.addItem (1 + i, list[(size_t) i].name);
        menu.addSubMenu (cat, sub);
    }

    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.nlpreset");
    files.sort();
    menu.addSeparator();
    menu.addSectionHeader ("User");
    if (files.isEmpty()) menu.addItem (-1, "(no user presets yet)", false);
    for (int i = 0; i < files.size(); ++i) menu.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    menu.addSeparator();
    menu.addItem (900, "Save preset...");
    menu.addItem (901, "Load preset file...");
    menu.addItem (902, "Show preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [this, files] (int result)
                        {
                            if (result <= 0) return;
                            if (result >= 1000) { proc.loadUserPreset (files[result - 1000]); }
                            else if (result == 900) { savePreset(); return; }
                            else if (result == 901)
                            {
                                chooser = std::make_unique<juce::FileChooser> ("Load preset", userPresetFolder(), "*.nlpreset");
                                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                      [this] (const juce::FileChooser& fc)
                                                      {
                                                          if (fc.getResult().existsAsFile()) proc.loadUserPreset (fc.getResult());
                                                          refresh();
                                                      });
                                return;
                            }
                            else if (result == 902)
                            {
                                userPresetFolder().createDirectory();
                                userPresetFolder().revealToUser();
                                return;
                            }
                            else
                            {
                                factoryIndex = result - 1;
                                proc.loadFactoryPreset (factoryIndex);
                            }
                            refresh();
                        });
}

void TopBar::savePreset()
{
    userPresetFolder().createDirectory();
    const auto name = proc.getPresetName().isEmpty() ? juce::String ("My Preset") : proc.getPresetName();
    chooser = std::make_unique<juce::FileChooser> ("Save preset", userPresetFolder().getChildFile (juce::File::createLegalFileName (name) + ".nlpreset"), "*.nlpreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("nlpreset")) f = f.withFileExtension ("nlpreset");
                              proc.saveUserPreset (f, f.getFileNameWithoutExtension());
                              refresh();
                          });
}
} // namespace nl

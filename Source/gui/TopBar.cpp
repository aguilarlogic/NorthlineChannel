#include "TopBar.h"
#include "../Presets.h"
#include <limits>

namespace nl
{
namespace
{
const float kScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
const juce::String kSmart = juce::String::fromUTF8 ("\xe2\x9c\xa6 "), kSpot = juce::String::fromUTF8 ("\xe2\x97\x89 ");
const juce::String kGear = juce::String::fromUTF8 ("\xe2\x9a\x99");

void collectTargets (juce::Component& root, std::vector<juce::Component*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (child == nullptr || ! child->isVisible()) continue;
        if (dynamic_cast<FocusTarget*> (child) != nullptr) out.push_back (child);
        collectTargets (*child, out);
    }
}
} // namespace

//==============================================================================
// Edit Spotlight: while it is on, this layer sits over the editor content (below the top bar)
// and turns each click into a Spotlight toggle for the control under it.
class TopBar::SpotlightEditLayer : public juce::Component
{
public:
    explicit SpotlightEditLayer (NorthlineProcessor& p) : proc (p) { setInterceptsMouseClicks (true, false); }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (colours::focus.withAlpha (0.06f));
        g.fillRect (bounds);
        g.setColour (colours::focus);
        g.drawRect (bounds.reduced (1.0f), 2.0f);

        std::vector<juce::Component*> list;
        if (auto* parent = getParentComponent()) collectTargets (*parent, list);
        for (auto* c : list)
        {
            auto* target = dynamic_cast<FocusTarget*> (c);
            if (target == nullptr || ! proc.isInSpotlight (target->focusParam())) continue;
            g.setColour (colours::focus);
            g.drawRoundedRectangle (getLocalArea (c, c->getLocalBounds()).toFloat().expanded (3.0f), 5.0f, 2.0f);
        }

        const juce::String hint = "EDIT SPOTLIGHT: click a control to add it to or remove it from the Spotlight";
        g.setFont (NorthlineLookAndFeel::font (12.0f, true));
        const int w = 540;
        g.setColour (colours::background.withAlpha (0.92f));
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 10.0f, (float) w, 24.0f), 6.0f);
        g.setColour (colours::focus);
        g.drawText (hint, juce::Rectangle<int> (22, 10, w, 24), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (auto* target = targetAt (e.getPosition()))
        {
            proc.toggleSpotlight (target->focusParam());
            repaint();
        }
    }

private:
    FocusTarget* targetAt (juce::Point<int> pos)
    {
        std::vector<juce::Component*> list;
        if (auto* parent = getParentComponent()) collectTargets (*parent, list);
        FocusTarget* best = nullptr;
        int bestArea = std::numeric_limits<int>::max();
        for (auto* c : list)
        {
            const auto area = getLocalArea (c, c->getLocalBounds());
            if (! area.contains (pos)) continue;
            const int size = area.getWidth() * area.getHeight();
            if (size < bestArea) { bestArea = size; best = dynamic_cast<FocusTarget*> (c); }
        }
        return best;
    }

    NorthlineProcessor& proc;
};

//==============================================================================
juce::String scalePercentText (float scale)
{
    return juce::String (juce::roundToInt (scale * 100.0f)) + "%";
}

ArrowButton::ArrowButton (bool redo) : juce::Button (redo ? "Redo" : "Undo"), mirrored (redo)
{
    setTooltip (redo ? "Redo (Ctrl+Shift+Z or Ctrl+Y)" : "Undo (Ctrl+Z)");
}

void ArrowButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, colours::track, over, down);

    // Design box 22 x 22: a head pointing left, and a shaft that turns down on the right. Redo mirrors it.
    juce::Path head, shaft;
    head.addTriangle (2.5f, 9.0f, 8.5f, 4.0f, 8.5f, 14.0f);
    shaft.startNewSubPath (7.0f, 9.0f);
    shaft.cubicTo (12.5f, 9.0f, 17.5f, 11.0f, 17.5f, 17.0f);

    const auto r = getLocalBounds().toFloat();
    const float k = juce::jmin (r.getWidth(), r.getHeight()) / 22.0f;
    const auto t = juce::AffineTransform::scale (k).translated (r.getCentreX() - 11.0f * k, r.getCentreY() - 11.0f * k);
    if (mirrored) { head.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 11.0f, 0.0f)); shaft.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 11.0f, 0.0f)); }
    head.applyTransform (t);
    shaft.applyTransform (t);

    g.setColour (isEnabled() ? colours::text : colours::faint);
    g.fillPath (head);
    g.strokePath (shaft, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

CompareButton::CompareButton (NorthlineProcessor& p) : juce::TextButton ("COMPARE"), proc (p)
{
    setClickingTogglesState (false);
    setColour (juce::TextButton::buttonOnColourId, colours::amber);
    setTooltip ("Compare: hold to hear the strip without its processing, loudness-matched. Shift-click to latch it on.");
}

void CompareButton::mouseDown (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown (e);
    proc.setCompare (hold.press (e.mods.isShiftDown()));
    refresh();
}

void CompareButton::mouseUp (const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp (e);
    proc.setCompare (hold.release());
    refresh();
}

void CompareButton::refresh()
{
    setToggleState (proc.getCompare() != nl::transient::kCompareOff, juce::dontSendNotification);
}

TopBar::TopBar (NorthlineProcessor& p) : proc (p), compareButton (p)
{
    quality = std::make_unique<Segmented> (p, P::quality, colours::eq);
    addAndMakeVisible (*quality);

    for (auto* b : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &presetButton, &saveButton, &slotA, &slotB, &copyAB,
                                                             &undoButton, &redoButton, &historyButton, &smartStartButton, &spotlightButton,
                                                             &editSpotlightButton, &hubButton, &settingsButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, colours::eq);
        addAndMakeVisible (b);
    }
    addAndMakeVisible (compareButton);
    shownScale = proc.getUiScale();
    addAndMakeVisible (scale);
    for (int i = 0; i < 5; ++i) scale.addItem (scalePercentText (kScales[i]), i + 1);
    scale.setTooltip ("Interface size");

    smartStartButton.setButtonText (kSmart + "Smart Start");
    spotlightButton.setButtonText (kSpot + "Spotlight");
    settingsButton.setButtonText (kGear);

    prevButton.setTooltip ("Previous preset (factory, then user presets)");
    nextButton.setTooltip ("Next preset (factory, then user presets)");
    presetButton.setTooltip ("Open the preset browser");
    saveButton.setTooltip ("Save, save as, module presets and copy or paste a module");
    slotA.setTooltip ("Compare: setting A");
    slotB.setTooltip ("Compare: setting B");
    copyAB.setTooltip ("Copy the active setting to the other slot");
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Shift+Z or Ctrl+Y)");
    historyButton.setTooltip ("Undo history: jump to any step");
    smartStartButton.setTooltip ("Smart Start: a starting point from a short listen to your source");
    spotlightButton.setTooltip ("Spotlight: dim everything except the controls the preset highlights");
    editSpotlightButton.setTooltip ("Edit Spotlight: click controls to add them to or remove them from the Spotlight");
    hubButton.setTooltip ("Instance Hub: every Northline instance in this session");
    settingsButton.setTooltip ("Settings");

    spotlightButton.setClickingTogglesState (true);
    spotlightButton.setColour (juce::TextButton::buttonOnColourId, colours::focus.darker (0.3f));
    editSpotlightButton.setClickingTogglesState (true);
    editSpotlightButton.setColour (juce::TextButton::buttonOnColourId, colours::amber); // an edit mode: transient, amber

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    presetButton.onClick = [this] { if (onOpenPresetBrowser) onOpenPresetBrowser(); else showPresetMenu(); };
    saveButton.onClick = [this] { showSaveMenu(); };
    slotA.onClick = [this] { proc.selectSlot (0); refresh(); };
    slotB.onClick = [this] { proc.selectSlot (1); refresh(); };
    copyAB.onClick = [this] { proc.copyActiveToOther(); refresh(); };
    undoButton.onClick = [this] { proc.undo(); refresh(); };
    redoButton.onClick = [this] { proc.redo(); refresh(); };
    historyButton.onClick = [this] { showHistoryMenu(); };
    smartStartButton.onClick = [this] { if (onSmartStart) onSmartStart(); };
    spotlightButton.onClick = [this] { if (onFocusChanged) onFocusChanged(); };
    editSpotlightButton.onClick = [this] { setSpotlightEditMode (editSpotlightButton.getToggleState()); };
    hubButton.onClick = [this] { if (onOpenHub) onOpenHub(); };
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    scale.onChange = [this]
    {
        const int i = scale.getSelectedItemIndex();
        if (i >= 0) { proc.setUiScale (kScales[i]); if (onScaleChanged) onScaleChanged(); }
    };
    refresh();
}

TopBar::~TopBar()
{
    editLayer.reset();
}

void TopBar::refreshTransient()
{
    compareButton.refresh();
}

void TopBar::refresh()
{
    presetButton.setButtonText (proc.getPresetName().isEmpty() ? juce::String ("Init") : proc.getPresetName());
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    copyAB.setButtonText (proc.getActiveSlot() == 0 ? "A>B" : "B>A");
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    historyButton.setEnabled (proc.canUndo() || proc.canRedo());
    // A size on the list selects its entry; any other size (the window was resized freely) shows its percentage.
    const float current = shownScale;
    int preset = -1;
    for (int i = 0; i < 5; ++i)
        if (std::abs (kScales[i] - current) < 0.01f) preset = i;
    if (preset >= 0)
    {
        if (scale.getSelectedItemIndex() != preset) scale.setSelectedItemIndex (preset, juce::dontSendNotification);
    }
    else if (scale.getText() != scalePercentText (current))
    {
        scale.setSelectedId (0, juce::dontSendNotification);
        scale.setText (scalePercentText (current), juce::dontSendNotification);
    }

    const auto latency = "Lat " + juce::String (proc.getStrip().getLatencySamples()) + " smp";
    if (latency != latencyText)
    {
        latencyText = latency;
        repaint (latencyBounds);
    }
}

void TopBar::setCpu (float percent)
{
    const auto t = "CPU " + juce::String (percent, 2) + "%";
    if (t != cpuText) { cpuText = t; repaint (cpuBounds); }
}

bool TopBar::isFocusMode() const { return spotlightButton.getToggleState(); }
bool TopBar::isSpotlightEditMode() const { return editSpotlightButton.getToggleState(); }

void TopBar::setSpotlightEditMode (bool on)
{
    const bool changed = on != isSpotlightEditMode();
    editSpotlightButton.setToggleState (on, juce::dontSendNotification);
    setEditLayerVisible (on);
    if (changed && onSpotlightEditChanged) onSpotlightEditChanged (on);
}

void TopBar::setEditLayerVisible (bool on)
{
    if (! on)
    {
        editLayer.reset();
        return;
    }
    auto* parent = getParentComponent();
    if (parent == nullptr) return;
    if (editLayer == nullptr)
    {
        editLayer = std::make_unique<SpotlightEditLayer> (proc);
        parent->addAndMakeVisible (*editLayer);
    }
    editLayer->setBounds (0, getBottom(), parent->getWidth(), juce::jmax (0, parent->getHeight() - getBottom()));
    editLayer->toFront (false);
}

bool TopBar::handleKeyPress (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && isSpotlightEditMode())
    {
        setSpotlightEditMode (false);
        return true;
    }
    const bool cmd = key.getModifiers().isCommandDown();
    const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    if (cmd && c == 'z')
    {
        if (key.getModifiers().isShiftDown()) { if (proc.canRedo()) proc.redo(); }
        else if (proc.canUndo()) proc.undo();
        refresh();
        return true;
    }
    if (cmd && c == 'y')
    {
        if (proc.canRedo()) proc.redo();
        refresh();
        return true;
    }
    return false;
}

//==============================================================================
void TopBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panelHeader);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    auto r = getLocalBounds().reduced (12, 0);
    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (13.0f, true));
    g.drawText ("NORTHLINE", r.removeFromTop (r.getHeight() / 2 + 2).removeFromLeft (100), juce::Justification::bottomLeft);
    g.setColour (colours::eq);
    g.setFont (NorthlineLookAndFeel::font (11.0f, true));
    g.drawText ("CHANNEL", r.removeFromLeft (100), juce::Justification::topLeft);

    g.setColour (colours::dim);
    const auto readoutFont = NorthlineLookAndFeel::font (12.0f);
    figures::draw (g, latencyText, latencyBounds.toFloat(), juce::Justification::centredRight, readoutFont);
    figures::draw (g, cpuText, cpuBounds.toFloat(), juce::Justification::centredRight, readoutFont);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (8, 7);
    r.removeFromLeft (104); // wordmark, painted in paint()

    const auto place = [&r] (juce::Component& c, int w, int gapAfter)
    {
        c.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gapAfter);
    };
    place (prevButton, 22, 2);
    place (presetButton, 120, 2);
    place (nextButton, 22, 10);
    place (saveButton, 46, 10);
    place (slotA, 24, 2);
    place (slotB, 24, 2);
    place (copyAB, 36, 10);
    place (undoButton, 28, 2);
    place (redoButton, 28, 2);
    place (historyButton, 46, 10);
    place (smartStartButton, 92, 10);
    place (spotlightButton, 88, 2);
    place (editSpotlightButton, 94, 10);
    place (compareButton, 74, 10);
    place (*quality, 84, 0);

    scale.setBounds (r.removeFromRight (52));
    r.removeFromRight (8);
    settingsButton.setBounds (r.removeFromRight (30));
    r.removeFromRight (4);
    hubButton.setBounds (r.removeFromRight (40));
    r.removeFromRight (10);
    cpuBounds = r.removeFromRight (62);
    latencyBounds = r.removeFromRight (74);
    if (editLayer != nullptr) setEditLayerVisible (true);
}

//==============================================================================
void TopBar::stepPreset (int delta)
{
    const auto library = presetLibrary (userPresetFolder());
    if (library.empty()) return;
    const int n = (int) library.size();
    const auto current = proc.getPresetName();
    int pos = -1;
    for (int i = 0; i < n && pos < 0; ++i)
        if (library[(size_t) i].name == current) pos = i;
    const int target = pos < 0 ? (delta > 0 ? 0 : n - 1) : (pos + delta + n) % n;

    const auto& e = library[(size_t) target];
    if (e.isFactory) proc.loadFactoryPreset (e.factoryIndex);
    else proc.loadUserPreset (e.file);
    refresh();
}

void TopBar::showPresetMenu()
{
    const auto library = presetLibrary (userPresetFolder());
    juce::PopupMenu menu;
    juce::StringArray categories;
    for (const auto& e : library)
        if (e.isFactory) categories.addIfNotAlreadyThere (e.category);
    for (const auto& cat : categories)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) library.size(); ++i)
            if (library[(size_t) i].isFactory && library[(size_t) i].category == cat) sub.addItem (i + 1, library[(size_t) i].name);
        menu.addSubMenu (cat, sub);
    }
    menu.addSeparator();
    menu.addSectionHeader ("User");
    bool any = false;
    for (int i = 0; i < (int) library.size(); ++i)
        if (! library[(size_t) i].isFactory) { menu.addItem (i + 1, library[(size_t) i].name); any = true; }
    if (! any) menu.addItem (-1, "(no user presets yet)", false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this, library] (int result)
    {
        if (result <= 0 || result > (int) library.size()) return;
        const auto& e = library[(size_t) result - 1];
        if (e.isFactory) proc.loadFactoryPreset (e.factoryIndex);
        else proc.loadUserPreset (e.file);
        refresh();
    });
}

void TopBar::showSaveMenu()
{
    juce::PopupMenu menu, saveModule, copyMenu;
    menu.addItem (1, "Save");
    menu.addItem (2, "Save as...");
    menu.addSeparator();
    for (int m = 0; m < numModules; ++m)
    {
        saveModule.addItem (100 + m, moduleName (m));
        copyMenu.addItem (300 + m, moduleName (m));
    }
    menu.addSubMenu ("Save module preset", saveModule);
    menu.addItem (200, "Load module preset...");
    menu.addSeparator();
    menu.addSubMenu ("Copy module", copyMenu);
    menu.addItem (400, "Paste module");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&saveButton), [this] (int r)
    {
        if (r == 1) saveCurrentPreset();
        else if (r == 2) saveUserPresetAs();
        else if (r == 200) loadModulePresetFrom();
        else if (r == 400) pasteModule();
        else if (r >= 100 && r < 100 + numModules) saveModulePresetAs (r - 100);
        else if (r >= 300 && r < 300 + numModules) copyModule (r - 300);
    });
}

void TopBar::showHistoryMenu()
{
    const auto undo = proc.getUndoHistory();
    const auto redo = proc.getRedoHistory();
    const int total = (int) (undo.size() + redo.size());
    juce::PopupMenu menu;
    if (total == 0) menu.addItem (-1, "(no history yet)", false);
    for (int i = total - 1; i >= 0; --i)
    {
        const auto label = i < (int) undo.size() ? undo[(size_t) i] : redo[(size_t) (i - (int) undo.size())];
        menu.addItem (i + 1, label, true, i == (int) undo.size() - 1);
    }
    menu.addSeparator();
    menu.addItem (-2, "Reset all settings");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyButton), [this] (int result)
    {
        if (result == -2) proc.resetToDefaults();
        else if (result > 0) proc.undoTo (result - 1);
        refresh();
    });
}

void TopBar::saveCurrentPreset()
{
    // Overwrites the user preset that carries the loaded name, keeping its metadata; otherwise it is a save as.
    const auto current = proc.getPresetName();
    for (const auto& e : presetLibrary (userPresetFolder()))
    {
        if (e.isFactory || e.name != current) continue;
        PresetMeta meta;
        juce::String name;
        if (readUserPresetMeta (e.file, name, meta)) proc.saveUserPreset (e.file, current, meta);
        refresh();
        return;
    }
    saveUserPresetAs();
}

void TopBar::saveUserPresetAs()
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

void TopBar::copyModule (int moduleId)
{
    juce::SystemClipboard::copyTextToClipboard (proc.getModuleXml (moduleId, moduleName (moduleId)));
}

void TopBar::pasteModule()
{
    proc.pasteModuleXml (juce::SystemClipboard::getTextFromClipboard());
    refresh();
}

void TopBar::saveModulePresetAs (int moduleId)
{
    moduleFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save module preset", moduleFolder().getChildFile (juce::String (moduleName (moduleId)) + ".nlmodule"), "*.nlmodule");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, moduleId] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("nlmodule")) f = f.withFileExtension ("nlmodule");
                              proc.saveModulePreset (f, moduleId, f.getFileNameWithoutExtension());
                          });
}

void TopBar::loadModulePresetFrom()
{
    moduleFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Load module preset", moduleFolder(), "*.nlmodule");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile()) proc.loadModulePreset (fc.getResult());
                              refresh();
                          });
}
} // namespace nl

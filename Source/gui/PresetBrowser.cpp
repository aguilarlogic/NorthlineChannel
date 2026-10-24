#include "PresetBrowser.h"

namespace nl
{
// The browser works on a copy of the library: the list shows the rows of queryPresets() over it.
class PresetBrowser::Impl : public juce::ListBoxModel
{
public:
    Impl (PresetBrowser& o, NorthlineProcessor& p) : owner (o), proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &search, &categoryBox, &familyBox, &folderBox, &sortBox, &favOnly,
                                                                &saveModeButton, &list, &detailName, &detailMeta, &detailNote,
                                                                &detailSpot, &loadButton, &favButton, &trashButton, &folderName,
                                                                &newFolderButton, &renameFolderButton, &deleteFolderButton, &saveName,
                                                                &saveCategory, &saveTags, &saveNote, &saveFolder, &saveButton, &cancelButton, &status })
            owner.addAndMakeVisible (c);

        search.setTextToShowWhenEmpty ("Search name, category, tags, notes", colours::faint);
        search.onTextChange = [this] { applyFilter(); };

        categoryBox.addItem ("All categories", 1);
        familyBox.addItem ("All families", 1);
        familyBox.addItem (kStarting, 2);
        familyBox.addItem (kCharacter, 3);
        familyBox.setSelectedId (1, juce::dontSendNotification); // every combo shows a value, never a blank
        sortBox.addItem ("Library order", 1);
        sortBox.addItem ("Name", 2);
        sortBox.addItem ("Category", 3);
        sortBox.addItem ("Favourites first", 4);
        sortBox.setSelectedId (1, juce::dontSendNotification);
        folderBox.addItem ("All presets", 1);
        folderBox.addItem ("Root (user presets)", 2);
        categoryBox.onChange = familyBox.onChange = folderBox.onChange = sortBox.onChange = [this] { applyFilter(); };

        favOnly.setClickingTogglesState (true);
        favOnly.setButtonText ("Favourites only");
        favOnly.onClick = [this] { applyFilter(); };
        saveModeButton.setButtonText ("Save preset...");
        saveModeButton.onClick = [this] { toggleSave (! saveName.isVisible()); };

        list.setModel (this);
        list.setRowHeight (42);
        list.setMultipleSelectionEnabled (false);
        list.setWantsKeyboardFocus (true);

        detailName.setFont (NorthlineLookAndFeel::font (16.0f, true));
        detailMeta.setFont (NorthlineLookAndFeel::font (12.0f));
        detailMeta.setColour (juce::Label::textColourId, colours::dim);
        detailSpot.setFont (NorthlineLookAndFeel::font (12.0f));
        detailSpot.setColour (juce::Label::textColourId, colours::dim);
        detailNote.setMultiLine (true, true);
        detailNote.setReadOnly (true);
        detailNote.setCaretVisible (false);
        detailNote.setFont (NorthlineLookAndFeel::font (13.0f));

        loadButton.setButtonText ("Load");
        favButton.setButtonText ("Favourite");
        trashButton.setButtonText ("Move to trash");
        loadButton.onClick = [this] { loadSelected(); };
        favButton.onClick = [this] { toggleFavourite(); };
        trashButton.onClick = [this] { trashSelected(); };

        folderName.setTextToShowWhenEmpty ("Folder name", colours::faint);
        newFolderButton.setButtonText ("New folder");
        renameFolderButton.setButtonText ("Rename folder");
        deleteFolderButton.setButtonText ("Delete folder");
        newFolderButton.onClick = [this] { folderAction (0); };
        renameFolderButton.onClick = [this] { folderAction (1); };
        deleteFolderButton.onClick = [this] { folderAction (2); };

        saveName.setTextToShowWhenEmpty ("Preset name", colours::faint);
        saveCategory.setTextToShowWhenEmpty ("Category", colours::faint);
        saveTags.setTextToShowWhenEmpty ("Tags, comma separated: vocal, warm, tape", colours::faint);
        saveNote.setTextToShowWhenEmpty ("Author note: what to adjust first", colours::faint);
        saveNote.setMultiLine (true, true);
        saveButton.setButtonText ("Save");
        cancelButton.setButtonText ("Cancel");
        saveButton.onClick = [this] { savePreset(); };
        cancelButton.onClick = [this] { toggleSave (false); };

        status.setFont (NorthlineLookAndFeel::font (12.0f));
        status.setColour (juce::Label::textColourId, colours::dim);

        setSaveVisible (false); // no resize here: the browser's impl is not assigned yet
        refreshLibrary();
    }

    //==========================================================================
    ~Impl() override { list.setModel (nullptr); }

    void refreshLibrary()
    {
        all = presetLibrary (userPresetFolder());
        favs = loadFavourites (favouritesFile());
        for (auto& e : all) e.favourite = favs.count (e.key) > 0;

        // Category and folder choices come from the library itself.
        const auto selectedCategory = categoryBox.getText();
        categoryBox.clear (juce::dontSendNotification);
        categoryBox.addItem ("All categories", 1);
        juce::StringArray categories;
        for (const auto& e : all) categories.addIfNotAlreadyThere (e.category);
        categories.sort (true);
        for (int i = 0; i < categories.size(); ++i) categoryBox.addItem (categories[i], i + 2);
        categoryBox.setText (selectedCategory, juce::dontSendNotification);
        if (categoryBox.getSelectedId() == 0) categoryBox.setSelectedId (1, juce::dontSendNotification);

        folders.clear();
        for (const auto& f : userPresetFolders (userPresetFolder())) folders.add (f);
        folderBox.clear (juce::dontSendNotification);
        folderBox.addItem ("All presets", 1);
        folderBox.addItem ("Root (user presets)", 2);
        for (int i = 0; i < folders.size(); ++i) folderBox.addItem (folders[i], 3 + i);
        folderBox.setSelectedId (1, juce::dontSendNotification);

        saveFolder.clear (juce::dontSendNotification);
        saveFolder.addItem ("Root", 1);
        for (int i = 0; i < folders.size(); ++i) saveFolder.addItem (folders[i], 2 + i);
        saveFolder.setSelectedId (1, juce::dontSendNotification);

        applyFilter();
    }

    void applyFilter()
    {
        PresetQuery q;
        q.text = search.getText();
        if (categoryBox.getSelectedId() > 1) q.category = categoryBox.getText();
        if (familyBox.getSelectedId() == 2) q.family = kStarting;
        if (familyBox.getSelectedId() == 3) q.family = kCharacter;
        const int folderId = folderBox.getSelectedId();
        if (folderId == 2) q.folder = "";
        else if (folderId >= 3 && folderId - 3 < folders.size()) q.folder = folders[folderId - 3];
        q.favouritesOnly = favOnly.getToggleState();
        switch (sortBox.getSelectedId())
        {
            case 2: q.sort = PresetQuery::Sort::Name; break;
            case 3: q.sort = PresetQuery::Sort::Category; break;
            case 4: q.sort = PresetQuery::Sort::Favourites; break;
            default: q.sort = PresetQuery::Sort::Library; break;
        }

        const int previous = list.getSelectedRow();
        shown = queryPresets (all, q);
        list.updateContent();
        if (! shown.empty())
            list.selectRow (juce::jlimit (0, (int) shown.size() - 1, previous < 0 ? 0 : previous), false, true);
        else
            list.deselectAllRows();
        updateDetail();
    }

    //==========================================================================
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size()) return;
        const auto& e = all[(size_t) shown[(size_t) row]];
        g.fillAll (selected ? colours::track : colours::panel);
        g.setColour (colours::text);
        g.setFont (NorthlineLookAndFeel::font (14.0f, true));
        g.drawText ((e.favourite ? juce::String::fromUTF8 ("\xe2\x98\x85  ") : juce::String()) + e.name,
                    juce::Rectangle<int> (10, 4, w - 20, h / 2 - 2), juce::Justification::centredLeft, true);
        g.setColour (colours::dim);
        g.setFont (NorthlineLookAndFeel::font (12.0f));
        const auto where = e.isFactory ? e.family : (e.folder.isEmpty() ? juce::String ("Root") : e.folder);
        g.drawText (e.category + "  |  " + where, juce::Rectangle<int> (10, h / 2, w - 20, h - h / 2 - 3), juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int) override { updateDetail(); }
    void returnKeyPressed (int) override { loadSelected(); }
    void deleteKeyPressed (int) override { trashSelected(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }

    const PresetEntry* selectedEntry() const
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) shown.size()) return nullptr;
        return &all[(size_t) shown[(size_t) row]];
    }

    void updateDetail()
    {
        const auto* e = selectedEntry();
        favButton.setEnabled (e != nullptr);
        loadButton.setEnabled (e != nullptr);
        trashButton.setEnabled (e != nullptr && ! e->isFactory);
        if (e == nullptr)
        {
            detailName.setText ("No preset selected", juce::dontSendNotification);
            detailMeta.setText ({}, juce::dontSendNotification);
            detailNote.setText ({}, false);
            detailSpot.setText ({}, juce::dontSendNotification);
            return;
        }
        detailName.setText (e->name, juce::dontSendNotification);
        juce::StringArray meta { e->category };
        if (e->isFactory) meta.add (e->family);
        else meta.add (e->folder.isEmpty() ? juce::String ("Root") : e->folder);
        if (! e->tags.isEmpty()) meta.add (e->tags.joinIntoString (", "));
        detailMeta.setText (meta.joinIntoString ("   |   "), juce::dontSendNotification);
        detailNote.setText (e->note, false);

        juce::String spot;
        if (e->isFactory)
        {
            juce::StringArray names;
            for (int i : factoryPresets()[(size_t) e->factoryIndex].focus) names.add (paramDef (i).name);
            spot = "Spotlight: " + names.joinIntoString (", ");
        }
        else
        {
            spot = "Spotlight is saved with this preset.";
        }
        detailSpot.setText (spot, juce::dontSendNotification);
        favButton.setButtonText (favs.count (e->key) ? "Unfavourite" : "Favourite");
    }

    //==========================================================================
    void setSaveVisible (bool on)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &saveName, &saveCategory, &saveTags, &saveNote, &saveFolder, &saveButton, &cancelButton })
            c->setVisible (on);
        if (on)
        {
            saveName.setText (proc.getPresetName() == "Init" ? juce::String() : proc.getPresetName(), false);
            saveName.grabKeyboardFocus();
        }
    }

    void toggleSave (bool on)
    {
        setSaveVisible (on);
        owner.resized();
    }

    void loadSelected()
    {
        const auto* e = selectedEntry();
        if (e == nullptr) return;
        const auto name = e->name;
        if (e->isFactory) proc.loadFactoryPreset (e->factoryIndex);
        else proc.loadUserPreset (e->file);
        if (owner.onPresetLoaded) owner.onPresetLoaded();
        setStatus ("Loaded " + name);
    }

    void toggleFavourite()
    {
        const auto* e = selectedEntry();
        if (e == nullptr) return;
        const auto key = e->key;
        if (favs.count (key)) favs.erase (key);
        else favs.insert (key);
        saveFavourites (favouritesFile(), favs);
        for (auto& x : all) x.favourite = favs.count (x.key) > 0;
        applyFilter();
    }

    void trashSelected()
    {
        const auto* e = selectedEntry();
        if (e == nullptr) return;
        if (e->isFactory) { setStatus ("Factory presets cannot be deleted."); return; }
        const auto file = e->file;
        if (file.moveToTrash()) setStatus ("Moved " + file.getFileNameWithoutExtension() + " to the trash.");
        else setStatus ("Could not move the preset to the trash.");
        refreshLibrary();
    }

    void savePreset()
    {
        const auto name = saveName.getText().trim();
        if (name.isEmpty()) { setStatus ("Give the preset a name first."); return; }

        PresetMeta meta;
        meta.category = saveCategory.getText().trim();
        for (auto tag : juce::StringArray::fromTokens (saveTags.getText(), ",", ""))
            if (tag.trim().isNotEmpty()) meta.tags.add (tag.trim());
        meta.note = saveNote.getText().trim();

        auto dir = userPresetFolder();
        if (saveFolder.getSelectedId() > 1) dir = dir.getChildFile (folders[saveFolder.getSelectedId() - 2]);
        const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".nlpreset");
        if (file.existsAsFile()) { setStatus ("A preset named " + name + " is already in that folder. Choose another name."); return; }

        if (proc.saveUserPreset (file, name, meta))
        {
            setStatus ("Saved " + name);
            toggleSave (false);
            refreshLibrary();
        }
        else
        {
            setStatus ("Could not write the preset file.");
        }
    }

    // 0 = new folder, 1 = rename the selected folder to the name typed, 2 = move the selected folder to the trash.
    void folderAction (int action)
    {
        const auto typed = juce::File::createLegalFileName (folderName.getText().trim());
        const int id = folderBox.getSelectedId();
        const bool hasSelection = id >= 3 && id - 3 < folders.size();
        const auto selected = hasSelection ? userPresetFolder().getChildFile (folders[id - 3]) : juce::File();

        if (action == 0)
        {
            if (typed.isEmpty()) { setStatus ("Type a folder name first."); return; }
            if (! userPresetFolder().getChildFile (typed).createDirectory().wasOk()) { setStatus ("Could not create that folder."); return; }
            setStatus ("Created folder " + typed);
        }
        else if (! hasSelection)
        {
            setStatus ("Select a user folder in the folder list first.");
            return;
        }
        else if (action == 1)
        {
            if (typed.isEmpty() || typed.contains ("/")) { setStatus ("Type a new folder name first."); return; }
            if (! selected.moveFileTo (selected.getSiblingFile (typed))) { setStatus ("Could not rename the folder."); return; }
            setStatus ("Renamed the folder to " + typed);
        }
        else
        {
            if (! selected.moveToTrash()) { setStatus ("Could not move the folder to the trash."); return; }
            setStatus ("Moved the folder and its presets to the trash.");
        }
        refreshLibrary();
    }

    void setStatus (const juce::String& text) { status.setText (text, juce::dontSendNotification); }

    //==========================================================================
    void layout (juce::Rectangle<int> r)
    {
        auto top = r.removeFromTop (34);
        saveModeButton.setBounds (top.removeFromRight (120));
        top.removeFromRight (8);
        favOnly.setBounds (top.removeFromRight (130));
        top.removeFromRight (8);
        sortBox.setBounds (top.removeFromRight (150));
        top.removeFromRight (8);
        folderBox.setBounds (top.removeFromRight (170));
        top.removeFromRight (8);
        familyBox.setBounds (top.removeFromRight (140));
        top.removeFromRight (8);
        categoryBox.setBounds (top.removeFromRight (160));
        top.removeFromRight (8);
        search.setBounds (top);
        r.removeFromTop (10);

        status.setBounds (r.removeFromBottom (22));
        r.removeFromBottom (6);

        list.setBounds (r.removeFromLeft (juce::jmax (260, r.getWidth() * 42 / 100)));
        r.removeFromLeft (12);

        detailName.setBounds (r.removeFromTop (28));
        detailMeta.setBounds (r.removeFromTop (20));
        detailSpot.setBounds (r.removeFromTop (20));
        detailNote.setBounds (r.removeFromTop (juce::jmin (110, r.getHeight() / 3)));
        r.removeFromTop (8);

        auto buttonRow = r.removeFromTop (28);
        loadButton.setBounds (buttonRow.removeFromLeft (100));
        buttonRow.removeFromLeft (6);
        favButton.setBounds (buttonRow.removeFromLeft (120));
        buttonRow.removeFromLeft (6);
        trashButton.setBounds (buttonRow.removeFromLeft (140));
        r.removeFromTop (14);

        auto folderRow = r.removeFromTop (28);
        folderName.setBounds (folderRow.removeFromLeft (juce::jmax (120, folderRow.getWidth() / 3)));
        folderRow.removeFromLeft (6);
        newFolderButton.setBounds (folderRow.removeFromLeft (100));
        folderRow.removeFromLeft (6);
        renameFolderButton.setBounds (folderRow.removeFromLeft (120));
        folderRow.removeFromLeft (6);
        deleteFolderButton.setBounds (folderRow.removeFromLeft (130));

        if (saveName.isVisible())
        {
            r.removeFromTop (14);
            saveName.setBounds (r.removeFromTop (28));
            r.removeFromTop (6);
            auto row = r.removeFromTop (28);
            saveCategory.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
            row.removeFromLeft (6);
            saveTags.setBounds (row);
            r.removeFromTop (6);
            saveNote.setBounds (r.removeFromTop (juce::jmin (90, r.getHeight() / 3)));
            r.removeFromTop (6);
            auto actions = r.removeFromTop (28);
            saveFolder.setBounds (actions.removeFromLeft (200));
            actions.removeFromLeft (6);
            saveButton.setBounds (actions.removeFromLeft (100));
            actions.removeFromLeft (6);
            cancelButton.setBounds (actions.removeFromLeft (100));
        }
    }

    PresetBrowser& owner;
    NorthlineProcessor& proc;

    static constexpr const char* kStarting = "Starting points";
    static constexpr const char* kCharacter = "Character";

    juce::Label detailName, detailMeta, detailSpot, status;
    juce::TextEditor search, detailNote, folderName, saveName, saveCategory, saveTags, saveNote;
    juce::ComboBox categoryBox, familyBox, folderBox, sortBox, saveFolder;
    juce::TextButton favOnly, saveModeButton, loadButton, favButton, trashButton;
    juce::TextButton newFolderButton, renameFolderButton, deleteFolderButton, saveButton, cancelButton;
    juce::ListBox list;

    std::vector<PresetEntry> all;
    std::vector<int> shown;
    std::set<juce::String> favs;
    juce::StringArray folders;
};

//==============================================================================
PresetBrowser::PresetBrowser (NorthlineProcessor& p) : impl (std::make_unique<Impl> (*this, p))
{
    setWantsKeyboardFocus (true);
}

PresetBrowser::~PresetBrowser() = default;

void PresetBrowser::resized() { impl->layout (getLocalBounds()); }

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose) onClose();
        return true;
    }
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
    {
        impl->toggleFavourite();
        return true;
    }
    return false;
}

void PresetBrowser::refreshLibrary() { impl->refreshLibrary(); }

void PresetBrowser::showSavePanel()
{
    impl->toggleSave (true);
}
} // namespace nl

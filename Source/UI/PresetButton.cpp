//
//  PresetButton.cpp
//  AB Reference (from Tonbox)
//

#include "PresetButton.h"
#include "../Engine/PresetFile.h"

namespace
{
    constexpr int saveId = 1, loadId = 2, revealId = 3, emptyId = 4, firstPresetId = 100;

    juce::Array<juce::File> sortedChildren (const juce::File& folder, int whatToFind, const juce::String& pattern)
    {
        auto children = folder.findChildFiles (whatToFind, false, pattern);
        std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });
        return children;
    }

    /** Subfolders first, as submenus, then the folder's presets. Every preset
        lands in `files`, its menu id being firstPresetId + its index. */
    void addFolder (juce::PopupMenu& menu, const juce::File& folder, juce::Array<juce::File>& files, const juce::File& current)
    {
        for (const auto& sub : sortedChildren (folder, juce::File::findDirectories, "*"))
        {
            juce::PopupMenu subMenu;
            addFolder (subMenu, sub, files, current);

            if (subMenu.getNumItems() > 0)
                menu.addSubMenu (sub.getFileName(), subMenu, true, nullptr, current.isAChildOf (sub));
        }

        for (const auto& file : sortedChildren (folder, juce::File::findFiles, juce::String ("*") + PresetFile::extension))
        {
            files.add (file);
            menu.addItem (firstPresetId + files.size() - 1, file.getFileNameWithoutExtension(), true, file == current);
        }
    }

    void showError (const juce::String& title, const juce::Result& result)
    {
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, result.getErrorMessage());
    }
}

PresetButton::PresetButton (juce::AudioProcessor& p)
    : FlatToggle ("Presets"), processor (p)
{
    // Lit while the menu is open, not on every click.
    setClickingTogglesState (false);
    setTooltip ("Load and save presets");
}

juce::File PresetButton::folder()
{
    const auto presets = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                             .getChildFile ("Library/Audio/Presets")
                             .getChildFile (JucePlugin_Manufacturer)
                             .getChildFile (JucePlugin_Name);
    presets.createDirectory();
    return presets;
}

juce::Result PresetButton::save (juce::AudioProcessor& p, const juce::File& file)
{
    juce::MemoryBlock state;
    p.getStateInformation (state);

    const auto text = PresetFile::write (state, file.getFileNameWithoutExtension());

    if (text.isEmpty())
        return juce::Result::fail ("The plugin's settings could not be read.");

    // replaceWithText writes a temporary file and moves it into place.
    // createOutputStream would append to a preset already there.
    if (! file.getParentDirectory().createDirectory()
        || ! file.replaceWithText (text, false, false, "\n"))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result PresetButton::load (juce::AudioProcessor& p, const juce::File& file)
{
    juce::MemoryBlock block;

    if (! file.loadFileAsData (block))
        return juce::Result::fail ("Could not read " + file.getFullPathName());

    const auto preset = PresetFile::read (block.toString());

    if (! preset.ok())
        return juce::Result::fail (preset.error);

    p.setStateInformation (preset.pluginState.getData(), (int) preset.pluginState.getSize());
    p.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    return juce::Result::ok();
}

void PresetButton::clicked()
{
    juce::PopupMenu menu;
    menu.addItem (saveId, "Save Preset...");
    menu.addItem (loadId, "Load Preset...");
    menu.addItem (revealId, "Show in Finder");
    menu.addSeparator();

    // Read afresh each time: presets saved by another instance appear
    // without reopening the plugin.
    auto files = std::make_shared<juce::Array<juce::File>>();
    addFolder (menu, folder(), *files, lastFile);

    if (files->isEmpty())
        menu.addItem (emptyId, "No presets yet", false);

    setToggleState (true, juce::dontSendNotification);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<PresetButton> (this), files] (int result)
    {
        if (safe == nullptr)
            return;

        safe->setToggleState (false, juce::dontSendNotification);

        if (result == saveId)        safe->choose (true);
        else if (result == loadId)   safe->choose (false);
        else if (result == revealId) folder().revealToUser();
        else if (result >= firstPresetId && result - firstPresetId < files->size())
            safe->loadFile ((*files)[result - firstPresetId]);
    });
}

void PresetButton::choose (bool saving)
{
    // A plain name, never one with a dot in it: the macOS save panel selects
    // only the part before the last dot, so "Kit" + extension would survive
    // every rename as "<new name>" + extension.
    auto name = currentName != nullptr ? currentName() : lastFile.getFileNameWithoutExtension();
    name = juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String ("Preset"));

    const auto start = saving ? (lastFile.existsAsFile() ? lastFile.getParentDirectory() : folder()).getChildFile (name)
                              : (lastFile.existsAsFile() ? lastFile.getParentDirectory() : folder());

    chooser = std::make_unique<juce::FileChooser> (saving ? "Save Preset" : "Load Preset", start,
                                                   juce::String ("*") + PresetFile::extension);

    const auto flags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting
                              : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [safe = juce::Component::SafePointer<PresetButton> (this), saving] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (safe == nullptr || file == juce::File())
            return;

        if (saving)
            safe->saveFile (file.withFileExtension (PresetFile::extension));
        else
            safe->loadFile (file);
    });
}

void PresetButton::loadFile (const juce::File& file)
{
    const auto result = load (processor, file);

    if (result.wasOk())
        lastFile = file;

    showError ("Load Preset", result);
}

void PresetButton::saveFile (const juce::File& file)
{
    if (onSaving != nullptr)
        onSaving (file.getFileNameWithoutExtension());

    const auto result = save (processor, file);

    if (result.wasOk())
        lastFile = file;

    showError ("Save Preset", result);
}

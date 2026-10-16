//
//  PresetButton.h
//  AB Reference (from Tonbox)
//
//  The plugin's own preset menu, beside the Dark switch. Logic has a preset
//  menu of its own for every AU; a VST3 host such as Bitwig has none, so
//  without this the plugin could neither load nor save a setting there.
//
//  The menu lists ~/Library/Audio/Presets/<company>/<plugin>/ with each
//  subfolder as a submenu, and saves into it in the plugin's own file type
//  (Engine/PresetFile.h).
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "FlatToggle.h"

class PresetButton : public FlatToggle
{
public:
    explicit PresetButton (juce::AudioProcessor&);

    /** The name Save Preset suggests. Unset: the preset last loaded or saved. */
    std::function<juce::String()> currentName;

    /** Called with the preset's name just before its state is taken, so a
        plugin that keeps a name in its state can store it there. */
    std::function<void (const juce::String&)> onSaving;

    /** ~/Library/Audio/Presets/<company>/<plugin>/, created if it is missing. */
    static juce::File folder();

    /** The plugin's state as a preset file named after the file. */
    static juce::Result save (juce::AudioProcessor&, const juce::File&);

    /** A preset file of this plugin into its state. */
    static juce::Result load (juce::AudioProcessor&, const juce::File&);

private:
    void clicked() override;
    void choose (bool saving);
    void loadFile (const juce::File&);
    void saveFile (const juce::File&);

    juce::AudioProcessor& processor;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetButton)
};

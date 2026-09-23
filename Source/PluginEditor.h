//
//  PluginEditor.h
//  AB Reference
//
//  The panel. It has one job the mockup makes obvious and one it does not.
//
//  The obvious one: put the switch where the hand already is, big enough to hit
//  without looking, because it will be hit a few hundred times in an evening.
//  The two buttons are the indicator *and* the control - the mockup had a pair
//  of state lamps at the top and a second pair of buttons at the bottom, and
//  two controls for one piece of state is one more than anybody can keep track
//  of while listening.
//
//  The less obvious one: show the numbers the plugin is acting on, not just the
//  numbers it measured. A level match you cannot see is a level match you
//  cannot trust, and the first question anyone asks of a tool like this is
//  "what did it just do to the reference?". So the match amount, the trim and
//  their sum are all on the panel, and the reference's own loudness sits next
//  to the loudness it is being played at.
//
//  Nothing here touches the audio thread. Values arrive as atomics that the
//  processor publishes, pulled by a 30 Hz timer.
//

#pragma once

#include <array>

#include "PluginProcessor.h"
#include "ReferenceSlotButton.h"
#include "WaveformDisplay.h"

class ABReferenceEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit ABReferenceEditor (ABReferenceProcessor&);
    ~ABReferenceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;

    void chooseReferenceFile (int slot);
    void selectSlot (int slot);
    void refreshSlots();
    void setAB (bool wantB);

    /** One parameter write, done the way a host expects to see it: a gesture
        around a normalised value. Both halves of that are easy to get wrong -
        writing raw seconds into setValueNotifyingHost is silent and wrong - so
        there is one place that does it. */
    void setParameter (const char* id, float plainValue);
    void beginGesture (const char* id);
    void endGesture (const char* id);

    void setLoopRegion (double startSeconds, double endSeconds);
    juce::String describeLoopRegion() const;

    void paintMeterTable (juce::Graphics&);
    juce::String describeReference() const;

    static juce::String formatLufs (float value);
    static juce::String formatDb (float value, bool withSign = false);

    ABReferenceProcessor& plugin;

    juce::TextButton aButton { "A" }, bButton { "B" };
    /** Three slots where one file button used to be, in the same row and the
        same height. See ReferenceSlotButton for why the gestures do the work
        that widgets would normally do. */
    std::array<std::unique_ptr<ReferenceSlotButton>, ABParams::numSlots> slotButtons;
    juce::TextButton resetButton { "Reset" };

    juce::ToggleButton levelMatchButton { "Level Match" };
    juce::ToggleButton monoButton { "Mono" };
    juce::ToggleButton loopButton { "Loop" };

    juce::Slider trimSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider offsetSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label trimLabel { {}, "Trim" };
    juce::Label offsetLabel { {}, "Offset" };
    juce::Label matchLabel;
    juce::Label loopRegionLabel;
    juce::Label infoLabel;
    juce::Label statusLabel;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<ButtonAttachment> levelMatchAttachment, monoAttachment, loopAttachment;
    std::unique_ptr<SliderAttachment> trimAttachment, offsetAttachment;

    WaveformDisplay waveform;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> meterArea;

    bool dragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABReferenceEditor)
};

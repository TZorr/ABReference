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
//  The look is Kitbox's and Rackbox's (since 0.4): a light body, recessed grey
//  sections, and one near-black display with orange type for everything that
//  is a reading rather than a control. The switch is two pads on the left, the
//  display with the reference's waveform takes the rest of the width beside
//  them, and the numbers and the controls sit in two sections underneath. The
//  waveform is still the element whose usefulness grows with its size - a
//  finished master drawn 76 pixels tall is a bar - and a plugin window has far
//  more width to give than height.
//
//  Nothing here touches the audio thread. Values arrive as atomics that the
//  processor publishes, pulled by a 30 Hz timer.
//

#pragma once

#include <array>

#include "PluginProcessor.h"
#include "ReferenceSlotButton.h"
#include "BalanceDisplay.h"
#include "WaveformDisplay.h"
#include "UI/ABPad.h"
#include "UI/ChoiceButtons.h"
#include "UI/FlatToggle.h"
#include "UI/PresetButton.h"
#include "UI/Knob.h"

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

    /** Light or dark panel, saved for the user (not the session). */
    void setDarkMode (bool shouldBeDark);

    /** Waveform or balance in the display; kept in the session's state. */
    void setDisplayView (int view);
    int getDisplayView() const;
    void refreshBalance();

    /** One parameter write, done the way a host expects to see it: a gesture
        around a normalised value. Both halves of that are easy to get wrong -
        writing raw seconds into setValueNotifyingHost is silent and wrong - so
        there is one place that does it. */
    void setParameter (const char* id, float plainValue);
    void beginGesture (const char* id);
    void endGesture (const char* id);

    void setLoopRegion (double startSeconds, double endSeconds);
    juce::String describeLoopRegion() const;

    void paintWordmark (juce::Graphics&);
    void paintDisplay (juce::Graphics&);
    void paintSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
    void paintMeterTable (juce::Graphics&);
    void paintMatchReadout (juce::Graphics&);
    void paintFooter (juce::Graphics&);
    void refreshSwitchCaptions();
    void setMonitor (int mode);
    juce::String describeReference() const;
    juce::String describeLoop() const;

    static juce::String formatLufs (float value);
    static juce::String formatDb (float value, bool withSign = false);
    static juce::String formatTime (double seconds);

    ABReferenceProcessor& plugin;

    ABPad aPad { "A", "Input", Palette::lamp }, bPad { "B", "Reference", Palette::accentDark };

    /** Three slots where one file button used to be, in the same row and the
        same height. See ReferenceSlotButton for why the gestures do the work
        that widgets would normally do. */
    std::array<std::unique_ptr<ReferenceSlotButton>, ABParams::numSlots> slotButtons;
    FlatToggle loadButton { "+ Load" };
    FlatToggle resetButton { "Reset" };
    FlatToggle darkButton { "Dark" };
    PresetButton presetButton { plugin };
    bool shownDark = false;

    FlatToggle levelMatchButton { "Level Match" };
    FlatToggle loopButton { "Loop" };
    ChoiceButtons monitorButtons;
    ChoiceButtons viewButtons;
    int shownView = -1;

    Knob trimKnob { "Trim", true };
    Knob offsetKnob { "Offset", true };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ButtonAttachment> levelMatchAttachment, loopAttachment;

    WaveformDisplay waveform;
    BalanceDisplay balance;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Painted areas, set in resized().
    juce::Rectangle<int> wordmarkArea, displayArea, loudnessArea, matchArea,
                         meterTableArea, matchReadoutArea, footerArea;

    // What the display and the footer say, cached by the timer so paint does
    // not rebuild strings, and so a repaint happens only when one changed.
    juce::String displayTitle, displayInfo, displayLoop, displayTime, statusText;
    bool statusWarns = false;
    bool transportRunning = false;

    bool dragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABReferenceEditor)
};

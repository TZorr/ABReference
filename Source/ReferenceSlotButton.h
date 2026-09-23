//
//  ReferenceSlotButton.h
//  AB Reference
//
//  One reference slot, in a row of three, in the 26 px that the single file
//  button used to have all to itself.
//
//  Three slots in one row means roughly 136 px each, which is enough for a
//  number and a truncated filename and nothing else. So the gestures carry
//  what widgets normally would: click selects, double-click loads, right-click
//  opens the short menu, and a file dropped on a slot goes into *that* slot.
//  That last one is why this is a Component with its own drag target rather
//  than three plain buttons - a drop has to be able to name its destination,
//  and the only place that name exists is under the pointer.
//
//  The selected slot is drawn in the same orange as the B button, because it is
//  the same statement: this is what you hear when you are not hearing A.
//

#pragma once

#include "Palette.h"
#include "ReferenceClip.h"

class ReferenceSlotButton : public juce::Component,
                            public juce::FileDragAndDropTarget
{
public:
    explicit ReferenceSlotButton (int slotIndex);

    /** Null for an empty slot. */
    void setClip (ReferenceClip::Ptr newClip);
    void setSelected (bool shouldBeSelected);
    void setLoading (bool isLoading);

    bool isEmpty() const noexcept { return clip == nullptr; }

    std::function<void()> onSelect;        // click on a loaded slot
    std::function<void()> onLoadRequested; // click on an empty slot, or double-click
    std::function<void()> onClearRequested;
    std::function<void()> onRevealRequested;
    std::function<void (const juce::StringArray&)> onFilesDropped;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void showMenu();

    const int slot;
    ReferenceClip::Ptr clip;
    bool selected = false;
    bool loading  = false;
    bool hovered  = false;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceSlotButton)
};

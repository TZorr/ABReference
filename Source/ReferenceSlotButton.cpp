//
//  ReferenceSlotButton.cpp
//  AB Reference
//

#include "ReferenceSlotButton.h"

#include "TextUtf8.h"

ReferenceSlotButton::ReferenceSlotButton (int slotIndex)
    : slot (slotIndex)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ReferenceSlotButton::setClip (ReferenceClip::Ptr newClip)
{
    if (clip == newClip)
        return;

    clip = std::move (newClip);
    repaint();
}

void ReferenceSlotButton::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void ReferenceSlotButton::setLoading (bool isLoading)
{
    if (loading == isLoading)
        return;

    loading = isLoading;
    repaint();
}

//==============================================================================
void ReferenceSlotButton::paint (juce::Graphics& g)
{
    // Rackbox's flat button: lit in the accent when selected, a darker grey
    // under the mouse, an ink outline while a file is dragged over it.
    const auto bounds = getLocalBounds().toFloat();
    const bool empty  = clip == nullptr;

    g.setColour (selected ? Palette::accent : (hovered ? Palette::buttonHover : Palette::button));
    g.fillRoundedRectangle (bounds, 5.0f);

    if (dragOver)
    {
        g.setColour (Palette::ink);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 5.0f, 2.0f);
    }

    const auto ink    = selected ? Palette::pad : Palette::ink;
    const auto inkDim = selected ? Palette::pad.withAlpha (0.6f) : Palette::inkDim;

    auto area = getLocalBounds().reduced (9, 0);

    // The number is always visible, loaded or not: it is what the 1/2/3 keys
    // and the automation lane refer to, so it has to be readable even when the
    // slot is empty and there is no name to hang it on.
    g.setColour (ink);
    g.setFont (Palette::mono (10.5f, true));
    g.drawText (juce::String (slot + 1), area.removeFromLeft (10), juce::Justification::centredLeft);

    area.removeFromLeft (6);

    if (loading || empty)
    {
        g.setColour (inkDim);
        g.setFont (Palette::label (9.5f));
        g.drawText (loading ? "LOADING ..." : "EMPTY", area, juce::Justification::centredLeft);
        return;
    }

    // Mixed case, unlike every other button: a file name is information, and
    // upper case would make two takes of the same master look alike.
    //
    // Dropping the extension rather than the middle of the name: at this width
    // the informative part of "TheBiggerLights_master_v3.wav" is the front, and
    // every candidate file has one of four extensions anyway.
    g.setColour (ink);
    g.setFont (Palette::label (10.5f).withExtraKerningFactor (0.02f));
    g.drawText (clip->file.getFileNameWithoutExtension(),
                area, juce::Justification::centredLeft, true);
}

//==============================================================================
void ReferenceSlotButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (clip == nullptr)
    {
        if (onLoadRequested) onLoadRequested();
        return;
    }

    if (onSelect) onSelect();
}

void ReferenceSlotButton::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onLoadRequested) onLoadRequested();
}

void ReferenceSlotButton::mouseEnter (const juce::MouseEvent&) { hovered = true;  repaint(); }
void ReferenceSlotButton::mouseExit  (const juce::MouseEvent&) { hovered = false; repaint(); }

void ReferenceSlotButton::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, clip == nullptr ? "Load reference ..." : "Replace reference ...");
    menu.addItem (2, "Clear", clip != nullptr);
    menu.addSeparator();
    menu.addItem (3, "Reveal in Finder", clip != nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this] (int result)
                        {
                            if (result == 1 && onLoadRequested)   onLoadRequested();
                            if (result == 2 && onClearRequested)  onClearRequested();
                            if (result == 3 && onRevealRequested) onRevealRequested();
                        });
}

//==============================================================================
bool ReferenceSlotButton::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! files.isEmpty();
}

void ReferenceSlotButton::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void ReferenceSlotButton::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void ReferenceSlotButton::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

    if (onFilesDropped) onFilesDropped (files);
}

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
    const auto bounds = getLocalBounds().toFloat();
    const bool empty  = clip == nullptr;

    g.setColour (selected ? ABLook::accentB.withAlpha (0.22f)
                          : ABLook::panel.brighter (hovered ? 0.12f : 0.0f));
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (dragOver ? ABLook::accentB
                          : (selected ? ABLook::accentB : ABLook::panel.brighter (0.25f)));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, dragOver || selected ? 1.6f : 1.0f);

    auto area = getLocalBounds().reduced (7, 0);

    // The number is always visible, loaded or not: it is what the 1/2/3 keys
    // and the automation lane refer to, so it has to be readable even when the
    // slot is empty and there is no name to hang it on.
    auto badge = area.removeFromLeft (14);
    g.setColour (selected ? ABLook::accentB : ABLook::dimText);
    g.setFont (ABLook::uiFont (11.0f, juce::Font::bold));
    g.drawText (juce::String (slot + 1), badge, juce::Justification::centredLeft);

    area.removeFromLeft (4);

    if (loading)
    {
        g.setColour (ABLook::dimText);
        g.setFont (ABLook::uiFont (12.0f));
        g.drawText ("loading ...", area, juce::Justification::centredLeft);
        return;
    }

    if (empty)
    {
        g.setColour (ABLook::dimText.withAlpha (0.7f));
        g.setFont (ABLook::uiFont (12.0f, juce::Font::italic));
        g.drawText ("empty", area, juce::Justification::centredLeft);
        return;
    }

    g.setColour (selected ? ABLook::text : ABLook::text.withAlpha (0.85f));
    g.setFont (ABLook::uiFont (12.0f));

    // Dropping the extension rather than the middle of the name: at this width
    // the informative part of "TheBiggerLights_master_v3.wav" is the front, and
    // every candidate file has one of four extensions anyway.
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

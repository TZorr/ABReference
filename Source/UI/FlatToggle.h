//
//  FlatToggle.h
//  AB Reference (copied from Rackbox, which copied it from Kitbox)
//
//  Kitbox's flat button, as a toggle: a rounded rectangle that fills with the
//  accent while it is on. Used for each block's Bypass, where "on" means the
//  block is out of the chain - the lit state is the one worth seeing.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.h"

class FlatToggle : public juce::ToggleButton
{
public:
    explicit FlatToggle (const juce::String& text) : juce::ToggleButton (text) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto lit = getToggleState();

        g.setColour (lit ? Palette::accent : (highlighted || down ? Palette::buttonHover : Palette::button));
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (lit ? Palette::pad : Palette::ink);
        g.setFont (Palette::label (9.5f));
        g.drawText (getButtonText().toUpperCase(), bounds, juce::Justification::centred);
    }
};

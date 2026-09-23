//
//  Palette.h
//  AB Reference
//
//  The panel's colours and fonts, in one place because there is now more than
//  one file painting with them. Two files each holding their own copy of
//  0xff17181c is how a panel ends up with two nearly-identical greys and nobody
//  able to say which one is the real background.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ABLook
{
    inline const juce::Colour background   { 0xff17181c };
    inline const juce::Colour panel        { 0xff202228 };
    inline const juce::Colour text         { 0xffe6e8ee };
    inline const juce::Colour dimText      { 0xff8f96a6 };
    inline const juce::Colour accentA      { 0xff4a86ff };
    inline const juce::Colour accentB      { 0xffff9d3d };
    inline const juce::Colour warning      { 0xffff5c5c };

    inline juce::Font uiFont (float height, int style = juce::Font::plain)
    {
        return juce::Font (juce::FontOptions (height, style));
    }

    inline juce::Font numberFont (float height)
    {
        // Readouts are compared row against row, so the digits have to line up.
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height, juce::Font::plain));
    }
}

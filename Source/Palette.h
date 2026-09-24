//
//  Palette.h
//  AB Reference
//
//  The panel's colours, fonts and corner radii, in one place because there is
//  now more than one file painting with them. Two files each holding their own
//  copy of 0xff17181c is how a panel ends up with two nearly-identical greys and
//  nobody able to say which one is the real background.
//
//  Three surface levels, darkest to lightest: the window, the cards that group
//  the controls, and the controls themselves. Each step is small - the panel is
//  looked at in a dark studio for hours - but every surface sits on exactly one
//  level, so the grouping reads without a single dividing line.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ABLook
{
    inline const juce::Colour background   { 0xff121317 };   // the window
    inline const juce::Colour card         { 0xff1a1c21 };   // a group of controls
    inline const juce::Colour panel        { 0xff23262d };   // a control on a card
    inline const juce::Colour outline      { 0xff2e323b };   // the edge of either
    inline const juce::Colour text         { 0xffe6e8ee };
    inline const juce::Colour dimText      { 0xff8f96a6 };
    inline const juce::Colour accentA      { 0xff4a86ff };
    inline const juce::Colour accentB      { 0xffff9d3d };
    inline const juce::Colour warning      { 0xffff5c5c };
    inline const juce::Colour transportLamp { 0xff3ecf8e };  // the transport dot, and nothing else

    inline constexpr float cardRadius    = 10.0f;
    inline constexpr float controlRadius = 7.0f;

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

//
//  Palette.h
//  AB Reference (copied from Rackbox, which copied it from Kitbox)
//
//  Every colour on the panel, and the fonts.
//
//  The Kitbox / Rackbox palette, unchanged: a warm light-grey body, slightly
//  darker recessed sections, one orange accent, a near-black display with
//  orange type. Flat throughout: no gradients, no shadows, no bevels. Depth is
//  said with two greys, not with light.
//
//  Before 0.4 this panel was dark with a blue A and an orange B. A has no
//  colour of its own now: the switch is two pads, and the one that is lit is
//  the one you hear. Orange means B, and "on", everywhere else.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    inline const juce::Colour body        { 0xffd4d0c7 };
    inline const juce::Colour section     { 0xffc7c2b8 };
    inline const juce::Colour sectionLine { 0xffb3ada2 };
    inline const juce::Colour ink         { 0xff2e2b27 };
    inline const juce::Colour inkDim      { 0xff6f6a61 };
    inline const juce::Colour accent      { 0xfff28a1e };

    inline const juce::Colour knobFace    { 0xffe6e2da };
    inline const juce::Colour knobEdge    { 0xff9a948a };
    inline const juce::Colour knobTrack   { 0xffaaa498 };

    inline const juce::Colour display     { 0xff1d1a17 };
    inline const juce::Colour displayText { 0xffff8c2b };
    inline const juce::Colour displayDim  { 0xff8f5f33 };
    inline const juce::Colour displayGrid { 0xff2e2925 };

    inline const juce::Colour pad         { 0xff2b2926 };
    inline const juce::Colour padHover    { 0xff36332f };
    inline const juce::Colour padText     { 0xffe9e5dd };
    inline const juce::Colour padTextDim  { 0xff8c877e };
    inline const juce::Colour warning     { 0xffe04a4f };

    inline const juce::Colour button      { 0xffbdb7ac };
    inline const juce::Colour buttonHover { 0xffafa89c };

    /** Green is the live signal and nothing else: the transport lamp, and the
        strip along the top of A, your own chain. It cannot be mistaken for B,
        for "on", or for a warning. */
    inline const juce::Colour lamp        { 0xff62b96a };

    /** The strip along the top of B: the accent, a shade darker, so it still
        reads as a strip when B is lit and the pad itself turns orange. */
    inline const juce::Colour accentDark  { 0xffb0681e };

    inline juce::Font label (float height = 10.5f)
    {
        return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.08f);
    }

    inline juce::Font mono (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }
}

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
//  Since 0.4.1 a dark variant of the same panel can be switched on (setDark),
//  as on Kitbox and Tonbox; the choice is kept per user, not per session (see
//  PluginEditor.cpp).
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    // The colours below are the light panel. They are not const: setDark()
    // swaps the whole set, and since every component reads them while it
    // paints, a repaint is all a switch needs (the waveform's cached picture
    // is the one exception; the editor invalidates it).
    inline juce::Colour body        { 0xffd4d0c7 };
    inline juce::Colour section     { 0xffc7c2b8 };
    inline juce::Colour sectionLine { 0xffb3ada2 };
    inline juce::Colour ink         { 0xff2e2b27 };
    inline juce::Colour inkDim      { 0xff6f6a61 };
    inline juce::Colour accent      { 0xfff28a1e };

    inline juce::Colour knobFace    { 0xffe6e2da };
    inline juce::Colour knobEdge    { 0xff9a948a };
    inline juce::Colour knobTrack   { 0xffaaa498 };

    inline juce::Colour display     { 0xff1d1a17 };
    inline juce::Colour displayText { 0xffff8c2b };
    inline juce::Colour displayDim  { 0xff8f5f33 };
    inline juce::Colour displayGrid { 0xff2e2925 };

    inline juce::Colour pad         { 0xff2b2926 };
    inline juce::Colour padHover    { 0xff36332f };
    inline juce::Colour padText     { 0xffe9e5dd };
    inline juce::Colour padTextDim  { 0xff8c877e };
    inline juce::Colour warning     { 0xffe04a4f };

    inline juce::Colour button      { 0xffbdb7ac };
    inline juce::Colour buttonHover { 0xffafa89c };

    /** Green is the live signal and nothing else: the transport lamp, and the
        strip along the top of A, your own chain. It cannot be mistaken for B,
        for "on", or for a warning. */
    inline juce::Colour lamp        { 0xff62b96a };

    /** The strip along the top of B: the accent, a shade darker, so it still
        reads as a strip when B is lit and the pad itself turns orange. */
    inline juce::Colour accentDark  { 0xffb0681e };

    inline bool dark = false;

    /** Light or dark panel - Kitbox's dark colours. The layout, the orange,
        the green lamp and B's strip stay; the greys turn round: a near-black
        body, slightly lighter sections, light type. Process-wide, so every
        open AB Reference window follows (each repaints when it notices). */
    inline void setDark (bool shouldBeDark)
    {
        dark = shouldBeDark;

        const auto set = [] (juce::Colour& c, juce::uint32 light, juce::uint32 darkValue)
        {
            c = juce::Colour (dark ? darkValue : light);
        };

        set (body,        0xffd4d0c7, 0xff161513);
        set (section,     0xffc7c2b8, 0xff22201d);
        set (sectionLine, 0xffb3ada2, 0xff2e2b27);
        set (ink,         0xff2e2b27, 0xffece7de);
        set (inkDim,      0xff6f6a61, 0xff9d978d);
        set (accent,      0xfff28a1e, 0xfff28a1e);

        set (knobFace,    0xffe6e2da, 0xff36332e);
        set (knobEdge,    0xff9a948a, 0xff55504a);
        set (knobTrack,   0xffaaa498, 0xff3f3b36);

        set (display,     0xff1d1a17, 0xff0c0b0a);
        set (displayText, 0xffff8c2b, 0xffff8c2b);
        set (displayDim,  0xff8f5f33, 0xffa87043);
        set (displayGrid, 0xff2e2925, 0xff24201c);

        set (pad,         0xff2b2926, 0xff2c2a27);
        set (padHover,    0xff36332f, 0xff37342f);
        set (padText,     0xffe9e5dd, 0xffece7de);
        set (padTextDim,  0xff8c877e, 0xff9d978d);
        set (warning,     0xffe04a4f, 0xffe04a4f);

        set (button,      0xffbdb7ac, 0xff2f2c28);
        set (buttonHover, 0xffafa89c, 0xff3a3631);
    }

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

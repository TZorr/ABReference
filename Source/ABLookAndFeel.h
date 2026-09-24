//
//  ABLookAndFeel.h
//  AB Reference
//
//  How the stock JUCE widgets are drawn on this panel.
//
//  JUCE's defaults are built to be legible on any background, which is not the
//  same as belonging on this one: a square checkbox, a text box with a white
//  frame around it and a slider thumb the size of a coin all read as borrowed.
//  Everything here draws from Palette.h, so a colour changed there changes it on
//  every control at once, and nothing in this file knows what any control does.
//
//  The one exception is the A/B switch, which is a TextButton marked with the
//  `abSwitch` property. It gets the accent glow and the caption line under the
//  letter; every other TextButton gets the quiet outline style, because there is
//  exactly one control on the panel that should look like it wants to be hit.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ABLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ABLookAndFeel();

    /** Property names on a TextButton's getProperties(). */
    static inline const juce::Identifier abSwitch { "abSwitch" };
    static inline const juce::Identifier caption  { "caption" };

    /** The A/B switch is drawn this far inside its bounds, because its glow has
        to fit inside them - a component cannot paint outside itself. The editor
        lays the switch out this much larger so the visible edges line up. */
    static constexpr int switchInset = 7;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isMouseOverButton, bool isButtonDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool isMouseOverButton, bool isButtonDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    void drawLabel (juce::Graphics&, juce::Label&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABLookAndFeel)
};

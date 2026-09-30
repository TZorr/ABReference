//
//  ABPad.h
//  AB Reference
//
//  One side of the switch, drawn as a Kitbox pad: a dark tile with a coloured
//  strip along the top, a big letter, and underneath it what that side is -
//  "Your chain", or the selected reference by name.
//
//  The pad is the indicator and the control at once. The one that is lit is the
//  one you hear, which is the only state the switch has, so there is nothing
//  else on the panel saying it a second time.
//
//  It holds no parameter. The editor sets it lit or not from the A/B parameter
//  and hears the click through onClick.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ABPad : public juce::Component
{
public:
    ABPad (const juce::String& letter, const juce::String& caption, juce::Colour edge);

    std::function<void()> onClick;

    void setLit (bool shouldBeLit);
    void setText (const juce::String& newText);
    void setCorner (const juce::String& newCorner);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    juce::String letter, caption, name, corner;
    juce::Colour edge;
    bool lit = false;
};

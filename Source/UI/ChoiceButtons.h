//
//  ChoiceButtons.h
//  AB Reference (copied from Rackbox, which copied it from Kitbox)
//
//  A row of flat buttons of which exactly one is lit: Kitbox's BYPASS button,
//  repeated, as a stepped selector.
//
//  It replaced two stepped knobs (a block's Type and Subtype). A knob makes you
//  turn through the options to see them; a row shows every option at once and
//  is one click from any of them, which is the better shape for a choice of
//  three or four names. The component holds no parameter of its own: the panel
//  tells it what to show (setLabels, setSelected) and hears the click through
//  onSelect, so the parameter stays the single source of truth.
//
//  Labels are kept as given and upper-cased only when drawn, so the panel can
//  read back "Ping-Pong" rather than "PING-PONG".
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ChoiceButtons : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void setLabels (const juce::StringArray& newLabels);
    void setSelected (int index);

    juce::String getSelectedText() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> buttonBounds (int index) const;
    int indexAt (juce::Point<int>) const;

    juce::StringArray labels;
    int selected = -1, hovered = -1;

    static constexpr float gap = 4.0f;
};

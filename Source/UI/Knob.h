//
//  Knob.h
//  AB Reference (copied from Rackbox, which copied it from Kitbox)
//
//  The only rotary control on the panel: a flat knob with its name and its
//  value printed underneath.
//
//  The value is always shown, not only while dragging. Two rows of knobs whose
//  settings can only be read by touching them is a panel that cannot be read
//  from a screenshot, or at a glance mid-take.
//
//  Bipolar knobs - pan, tune, fine, filter envelope amount - draw their arc
//  from twelve o'clock, so "nothing" looks like nothing. Double-click returns a
//  knob to its default.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class Knob : public juce::Slider
{
public:
    Knob (const juce::String& labelText, bool isBipolar = false);
    ~Knob() override;

    /** Binds to a parameter, replacing any previous binding. */
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId);

    void paint (juce::Graphics&) override;

    /** The name printed under the knob; a block's A and B are renamed by the effect loaded. */
    void setLabel (const juce::String& newLabel);

    static constexpr int labelHeight = 26;   // name + value under the knob

    /** A click on the value under the knob opens a text field for typing one.
        AB Reference needs it for Offset: +/- 5 s on a knob is 45 ms a pixel,
        and the value you want to reach is a host's latency error, to the
        tenth of a millisecond. */
    void setTypeable (bool shouldBeTypeable) { typeable = shouldBeTypeable; }

    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> valueArea() const;
    void showValueEditor();
    void commitValueEditor();

    bool typeable = false;
    std::unique_ptr<juce::TextEditor> valueEditor;

    juce::String label;
    bool bipolar;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

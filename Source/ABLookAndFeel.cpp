//
//  ABLookAndFeel.cpp
//  AB Reference
//

#include "ABLookAndFeel.h"

#include "Palette.h"

using namespace ABLook;

namespace
{
    // Fades out within ABLookAndFeel::switchInset, so the glow is not cut off
    // by the button's own edge.
    constexpr int switchGlowRadius = 10;

    /** Dark ink on a light accent, white on a dark one. The orange B is light
        enough that white on it drops below comfortable contrast; the blue A is
        not. Worked out from the colour rather than written per button, so a
        palette change cannot quietly put white text on yellow. */
    juce::Colour inkOn (juce::Colour accent)
    {
        return accent.getPerceivedBrightness() > 0.6f ? background : juce::Colours::white;
    }
}

//==============================================================================
ABLookAndFeel::ABLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);

    setColour (juce::Label::textColourId, text);

    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);

    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accentB.withAlpha (0.35f));

    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accentB.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, accentB);
    setColour (juce::CaretComponent::caretColourId, accentB);

    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentB.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);

    setColour (juce::TooltipWindow::backgroundColourId, card);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

//==============================================================================
void ABLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                          const juce::Colour&, bool isMouseOverButton, bool isButtonDown)
{
    if (button.getProperties()[abSwitch])
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced ((float) switchInset);
        const auto accent = button.findColour (juce::TextButton::buttonOnColourId);

        juce::Path shape;
        shape.addRoundedRectangle (bounds, cardRadius);

        if (button.getToggleState())
        {
            juce::DropShadow (accent.withAlpha (0.45f), switchGlowRadius, {}).drawForPath (g, shape);

            g.setGradientFill (juce::ColourGradient (accent.brighter (0.12f), bounds.getTopLeft(),
                                                     accent.darker (0.12f), bounds.getBottomLeft(), false));
            g.fillPath (shape);
        }
        else
        {
            g.setColour (panel.brighter (isMouseOverButton ? 0.08f : 0.0f));
            g.fillPath (shape);

            g.setColour (outline);
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        if (isButtonDown)
        {
            g.setColour (juce::Colours::black.withAlpha (0.12f));
            g.fillPath (shape);
        }

        return;
    }

    // Everything else is a quiet outline that fills in under the pointer. The
    // only other TextButton is Reset, and a reset should not look inviting.
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    if (isMouseOverButton || isButtonDown)
    {
        g.setColour (panel.brighter (isButtonDown ? 0.15f : 0.05f));
        g.fillRoundedRectangle (bounds, controlRadius);
    }

    g.setColour (outline.brighter (0.25f));
    g.drawRoundedRectangle (bounds, controlRadius, 1.0f);
}

void ABLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    if (button.getProperties()[abSwitch])
    {
        const auto accent = button.findColour (juce::TextButton::buttonOnColourId);
        const bool on = button.getToggleState();
        const auto ink = on ? inkOn (accent) : accent;

        auto area = button.getLocalBounds().toFloat().reduced ((float) switchInset + 6.0f);
        const auto captionText = button.getProperties()[caption].toString();

        // The caption says what the letter means right now - "your chain", or
        // the name of the reference B will play - so the switch answers the
        // question "which one am I hearing" without a glance anywhere else.
        if (captionText.isNotEmpty())
        {
            g.setColour (on ? ink.withAlpha (0.78f) : dimText);
            g.setFont (uiFont (11.5f));
            g.drawFittedText (captionText, area.removeFromBottom (16.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.9f);
        }

        g.setColour (ink);
        g.setFont (uiFont (30.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), area, juce::Justification::centred);
        return;
    }

    g.setColour (button.isEnabled() ? text : dimText);
    g.setFont (uiFont (13.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void ABLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                      bool isMouseOverButton, bool)
{
    // A switch rather than a checkbox. The three toggles on this panel are all
    // modes that stay on - Level Match, Mono, Loop - and a switch is the shape
    // that says "this changes what you hear until you change it back".
    const auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    constexpr float trackWidth = 32.0f, trackHeight = 18.0f;

    const juce::Rectangle<float> track (bounds.getX() + 1.0f, bounds.getCentreY() - trackHeight * 0.5f,
                                        trackWidth, trackHeight);

    g.setColour (on ? accentB.brighter (isMouseOverButton ? 0.08f : 0.0f)
                    : panel.brighter (isMouseOverButton ? 0.1f : 0.0f));
    g.fillRoundedRectangle (track, trackHeight * 0.5f);

    if (! on)
    {
        g.setColour (outline.brighter (0.25f));
        g.drawRoundedRectangle (track.reduced (0.5f), trackHeight * 0.5f - 0.5f, 1.0f);
    }

    const float knob = trackHeight - 6.0f;
    const float knobX = on ? track.getRight() - 3.0f - knob : track.getX() + 3.0f;

    g.setColour (on ? juce::Colours::white : dimText);
    g.fillEllipse (knobX, track.getCentreY() - knob * 0.5f, knob, knob);

    g.setColour (button.isEnabled() ? text : dimText);
    g.setFont (uiFont (13.5f));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (trackWidth + 10.0f),
                juce::Justification::centredLeft, true);
}

//==============================================================================
void ABLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float minSliderPos, float maxSliderPos,
                                      juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (! slider.isHorizontal())
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                          minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const float centreY = (float) y + (float) height * 0.5f;
    const juce::Rectangle<float> track ((float) x, centreY - 2.0f, (float) width, 4.0f);

    g.setColour (panel.brighter (0.18f));
    g.fillRoundedRectangle (track, 2.0f);

    // Filled from zero, not from the left end. Both sliders here are offsets
    // from a neutral middle - dB of trim, ms of shift - and a bar growing from
    // the left would draw "0.0 dB" as half full.
    const float originX = (float) slider.getPositionOfValue (slider.getRange().clipValue (0.0));
    const float from = juce::jmin (originX, sliderPos);
    const float to   = juce::jmax (originX, sliderPos);

    g.setColour (accentB);
    g.fillRoundedRectangle (from, track.getY(), juce::jmax (0.0f, to - from), track.getHeight(), 2.0f);

    g.setColour (dimText.withAlpha (0.6f));
    g.fillRect (originX - 0.5f, centreY - 5.0f, 1.0f, 10.0f);

    const float radius = (float) getSliderThumbRadius (slider);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (sliderPos - radius, centreY - radius + 1.0f, radius * 2.0f, radius * 2.0f);

    g.setColour (text);
    g.fillEllipse (sliderPos - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
}

//==============================================================================
void ABLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // A slider's value box is drawn as a rounded well with the number in it,
    // in the same monospaced face as every other readout on the panel. Any
    // other label is left to the stock drawing.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr)
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    g.setColour (panel);
    g.fillRoundedRectangle (label.getLocalBounds().toFloat(), controlRadius);

    if (! label.isBeingEdited())
    {
        g.setColour (text);
        g.setFont (numberFont (13.0f));
        g.drawText (label.getText(), label.getLocalBounds().reduced (6, 0),
                    juce::Justification::centred, false);
    }
}

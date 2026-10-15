//
//  ABPad.cpp
//  AB Reference
//

#include "ABPad.h"
#include "Palette.h"

ABPad::ABPad (const juce::String& letterText, const juce::String& captionText, juce::Colour edgeColour)
    : letter (letterText), caption (captionText.toUpperCase()), edge (edgeColour)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ABPad::setLit (bool shouldBeLit)
{
    if (lit == shouldBeLit)
        return;

    lit = shouldBeLit;
    repaint();
}

void ABPad::setText (const juce::String& newText)
{
    if (name == newText)
        return;

    name = newText;
    repaint();
}

void ABPad::setCorner (const juce::String& newCorner)
{
    if (corner == newCorner)
        return;

    corner = newCorner;
    repaint();
}

void ABPad::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 5.0f;

    g.setColour (lit ? Palette::accent : (isMouseOver() ? Palette::padHover : Palette::pad));
    g.fillRoundedRectangle (bounds, radius);

    // The strip along the top, clipped to the rounded corners, as on Kitbox.
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path outline;
        outline.addRoundedRectangle (bounds, radius);
        g.reduceClipRegion (outline);
        g.setColour (edge);
        g.fillRect (bounds.withHeight (4.0f));
    }

    const auto ink    = lit ? Palette::pad : Palette::padText;
    const auto inkDim = lit ? Palette::pad.withAlpha (0.6f) : Palette::padTextDim;

    auto area = bounds.reduced (12.0f, 10.0f);
    area.removeFromTop (4.0f);

    g.setColour (inkDim);
    g.setFont (Palette::mono (10.0f));
    g.drawText (corner, area.withHeight (12.0f), juce::Justification::topRight);

    // 40 points on a pad of the old height, smaller on the compact one, so the
    // letter never runs into the caption under it.
    const float letterSize = juce::jmin (40.0f, bounds.getHeight() * 0.32f);

    g.setColour (ink);
    g.setFont (juce::Font (juce::FontOptions (letterSize, juce::Font::bold)));
    g.drawText (letter, area.withTrimmedTop (8.0f).withHeight (letterSize), juce::Justification::topLeft);

    auto nameRow = area.removeFromBottom (13.0f);
    area.removeFromBottom (7.0f);

    g.setColour (inkDim);
    g.setFont (Palette::label (9.0f));
    g.drawText (caption, area.removeFromBottom (11.0f), juce::Justification::centredLeft);

    g.setColour (ink);
    g.setFont (Palette::label (11.0f).withExtraKerningFactor (0.04f));
    g.drawFittedText (name, nameRow.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}

void ABPad::mouseDown (const juce::MouseEvent&)
{
    if (onClick != nullptr)
        onClick();
}

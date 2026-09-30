//
//  ChoiceButtons.cpp
//  AB Reference (copied from Rackbox, which copied it from Kitbox)
//

#include "ChoiceButtons.h"
#include "Palette.h"

void ChoiceButtons::setLabels (const juce::StringArray& newLabels)
{
    if (newLabels == labels)
        return;

    labels = newLabels;
    hovered = -1;
    repaint();
}

void ChoiceButtons::setSelected (int index)
{
    if (index == selected)
        return;

    selected = index;
    repaint();
}

juce::String ChoiceButtons::getSelectedText() const
{
    return juce::isPositiveAndBelow (selected, labels.size()) ? labels[selected] : juce::String();
}

juce::Rectangle<float> ChoiceButtons::buttonBounds (int index) const
{
    const auto count = juce::jmax (1, labels.size());
    const auto bounds = getLocalBounds().toFloat();
    const auto width = (bounds.getWidth() - gap * (float) (count - 1)) / (float) count;

    return { bounds.getX() + (float) index * (width + gap), bounds.getY(), width, bounds.getHeight() };
}

int ChoiceButtons::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (buttonBounds (i).contains (p.toFloat()))
            return i;

    return -1;
}

void ChoiceButtons::paint (juce::Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto bounds = buttonBounds (i);
        const auto lit = i == selected;

        g.setColour (lit ? Palette::accent : (i == hovered ? Palette::buttonHover : Palette::button));
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (lit ? Palette::pad : Palette::ink);
        g.setFont (Palette::label (9.5f));
        g.drawFittedText (labels[i].toUpperCase(), bounds.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
}

void ChoiceButtons::mouseDown (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());

    if (index >= 0 && onSelect)
        onSelect (index);
}

void ChoiceButtons::mouseMove (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void ChoiceButtons::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

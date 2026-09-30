//
//  Knob.cpp
//  AB Reference (copied from Rackbox, which copied it from Kitbox)
//

#include "Knob.h"
#include "Palette.h"

Knob::Knob (const juce::String& labelText, bool isBipolar)
    : label (labelText.toUpperCase()), bipolar (isBipolar)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                         juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity (220);
    setVelocityBasedMode (false);
}

Knob::~Knob()
{
    valueEditor.reset();
    attachment.reset();
}

juce::Rectangle<int> Knob::valueArea() const
{
    return getLocalBounds().removeFromBottom (labelHeight - 12);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (typeable && ! e.mods.isPopupMenu() && valueArea().contains (e.getPosition()))
    {
        showValueEditor();
        return;
    }

    juce::Slider::mouseDown (e);
}

void Knob::showValueEditor()
{
    // The number as it reads, without the unit: getValueFromText strips a
    // suffix, but a field that opens as "0.0 ms" invites typing after the "ms".
    auto text = getTextFromValue (getValue());

    if (getTextValueSuffix().isNotEmpty())
        text = text.upToLastOccurrenceOf (getTextValueSuffix(), false, false);

    valueEditor = std::make_unique<juce::TextEditor>();
    valueEditor->setJustification (juce::Justification::centred);
    valueEditor->setFont (Palette::mono (10.5f));
    valueEditor->setColour (juce::TextEditor::backgroundColourId, Palette::knobFace);
    valueEditor->setColour (juce::TextEditor::textColourId, Palette::ink);
    valueEditor->setColour (juce::TextEditor::outlineColourId, Palette::accent);
    valueEditor->setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
    valueEditor->setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.35f));
    valueEditor->setIndents (2, 1);
    valueEditor->setText (text.trim(), false);
    valueEditor->setBounds (valueArea().withSizeKeepingCentre (juce::jmin (getWidth(), 72), 16));

    valueEditor->onReturnKey = [this] { commitValueEditor(); };
    valueEditor->onFocusLost = [this] { commitValueEditor(); };
    valueEditor->onEscapeKey = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)]
        {
            if (safe != nullptr)
                safe->valueEditor.reset();
        });
    };

    addAndMakeVisible (*valueEditor);
    valueEditor->grabKeyboardFocus();
    valueEditor->selectAll();
}

void Knob::commitValueEditor()
{
    if (valueEditor == nullptr)
        return;

    const auto typed = valueEditor->getText().trim();

    if (typed.isNotEmpty())
    {
        // A drag notification around the change, so the attachment wraps it in
        // one host gesture - the same thing Slider's own text box does.
        const juce::Slider::ScopedDragNotification gesture (*this);
        setValue (getValueFromText (typed), juce::sendNotificationSync);
    }

    // Deleted after this callback returns: this runs inside the editor's own
    // Return and focus handlers.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)]
    {
        if (safe != nullptr)
            safe->valueEditor.reset();
    });
}

void Knob::setLabel (const juce::String& newLabel)
{
    label = newLabel.toUpperCase();
    repaint();
}

void Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, *this);

    if (auto* parameter = state.getParameter (parameterId))
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto textArea = bounds.removeFromBottom ((float) labelHeight);

    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    const auto centre   = bounds.getCentre();
    const auto ringRadius = diameter * 0.5f - 1.5f;
    const auto faceRadius = ringRadius - 5.0f;

    const auto& rotary = getRotaryParameters();
    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle = rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
    const auto zeroAngle = bipolar ? (rotary.startAngleRadians + rotary.endAngleRadians) * 0.5f
                                   : rotary.startAngleRadians;

    // The ring: full travel in the track colour, the set range in orange.
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                             rotary.startAngleRadians, rotary.endAngleRadians, true);
        g.setColour (Palette::knobTrack);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::abs (angle - zeroAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                 juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
            g.setColour (Palette::accent);
            g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // The face: one flat disc and a hairline.
    const auto hover = isMouseOverOrDragging();
    g.setColour (hover ? Palette::knobFace.brighter (0.06f) : Palette::knobFace);
    g.fillEllipse (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f);
    g.setColour (Palette::knobEdge);
    g.drawEllipse (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f, 1.0f);

    // The pointer.
    {
        const auto inner = juce::Point<float> (centre.x + faceRadius * 0.25f * std::sin (angle),
                                               centre.y - faceRadius * 0.25f * std::cos (angle));
        const auto outer = juce::Point<float> (centre.x + faceRadius * 0.82f * std::sin (angle),
                                               centre.y - faceRadius * 0.82f * std::cos (angle));
        g.setColour (Palette::ink);
        g.drawLine ({ inner, outer }, 2.5f);
    }

    // Name and value.
    g.setColour (Palette::inkDim);
    g.setFont (Palette::label (9.5f));
    g.drawFittedText (label, textArea.removeFromTop (12.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);

    g.setColour (isEnabled() ? Palette::ink : Palette::inkDim);
    g.setFont (Palette::mono (10.5f));
    g.drawFittedText (getTextFromValue (getValue()), textArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

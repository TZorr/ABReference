//
//  BalanceDisplay.cpp
//  AB Reference
//

#include "BalanceDisplay.h"
#include "UI/Palette.h"

#include <algorithm>

namespace
{
    constexpr float labelGutter  = 30.0f;   // dB labels on the left
    constexpr float axisHeight   = 13.0f;   // frequency labels underneath

    bool sameBands (const Balance::Bands& x, const Balance::Bands& y)
    {
        for (size_t b = 0; b < x.size(); ++b)
            if (std::abs (x[b] - y[b]) > 0.01f)
                return false;

        return true;
    }
}

//==============================================================================
void BalanceDisplay::smoothAcrossBands (Balance::Bands& db)
{
    // Two passes of 1-2-1, which is 1-4-6-4-1: about two bands either side,
    // gentle enough that a real bump an octave wide survives. At the ends the
    // missing neighbour is left out and the rest renormalised.
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto source = db;
        const int last = (int) db.size() - 1;

        for (int b = 0; b <= last; ++b)
        {
            float sum = 2.0f * source[(size_t) b], weight = 2.0f;

            if (b > 0)    { sum += source[(size_t) (b - 1)]; weight += 1.0f; }
            if (b < last) { sum += source[(size_t) (b + 1)]; weight += 1.0f; }

            db[(size_t) b] = sum / weight;
        }
    }
}

BalanceDisplay::Points BalanceDisplay::reversed (const Points& points)
{
    Points result;
    std::reverse_copy (points.begin(), points.end(), result.begin());
    return result;
}

void BalanceDisplay::addCurve (juce::Path& path, const Points& points, bool continuePath)
{
    // Catmull-Rom through every band centre, as cubic Beziers: it passes
    // through each point, so the curve still says what the band says, only
    // without the corners between them.
    if (continuePath)
        path.lineTo (points[0]);
    else
        path.startNewSubPath (points[0]);

    const int last = (int) points.size() - 1;

    for (int i = 0; i < last; ++i)
    {
        const auto& p0 = points[(size_t) juce::jmax (0, i - 1)];
        const auto& p1 = points[(size_t) i];
        const auto& p2 = points[(size_t) (i + 1)];
        const auto& p3 = points[(size_t) juce::jmin (last, i + 2)];

        path.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
}

//==============================================================================
BalanceDisplay::BalanceDisplay()
{
    setOpaque (true);

    // For the cursor only; there is nothing to click. A file dropped here
    // still reaches the editor, which is the drop target above this.
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void BalanceDisplay::setCorridor (const Balance::Corridor& corridor, const juce::String& emptyMessage)
{
    const bool changed = corridor.windows != reference.windows
                      || ! sameBands (corridor.median, reference.median)
                      || ! sameBands (corridor.low, reference.low)
                      || ! sameBands (corridor.high, reference.high)
                      || emptyMessage != message;

    if (! changed)
        return;

    reference = corridor;
    message = emptyMessage;
    repaint();
}

void BalanceDisplay::setA (const Balance::Bands& balanceDb, bool valid, bool holding)
{
    if (valid == aValid && holding == aHolding && (! valid || sameBands (balanceDb, a)))
        return;

    a = balanceDb;
    aValid = valid;
    aHolding = holding;
    repaint();
}

//==============================================================================
juce::Rectangle<float> BalanceDisplay::plotArea() const
{
    return getLocalBounds().toFloat()
               .withTrimmedLeft (labelGutter)
               .withTrimmedBottom (axisHeight)
               .withTrimmedTop (2.0f)
               .withTrimmedRight (2.0f);
}

float BalanceDisplay::xFor (float erb) const
{
    // Half a band of margin at each end, so the outermost bands are not drawn
    // on the frame.
    const float half  = Balance::bandSpacing() * 0.5f;
    const float first = Balance::bandCentreErb (0) - half;
    const float last  = Balance::bandCentreErb (Balance::numBands - 1) + half;

    const auto plot = plotArea();
    return plot.getX() + (erb - first) / (last - first) * plot.getWidth();
}

float BalanceDisplay::yFor (float db) const
{
    const auto plot = plotArea();
    const float clamped = juce::jlimit (-rangeDb, rangeDb, db);
    return plot.getCentreY() - clamped / rangeDb * plot.getHeight() * 0.5f;
}

//==============================================================================
void BalanceDisplay::paint (juce::Graphics& g)
{
    g.fillAll (Palette::display);

    const auto plot = plotArea();

    // ---- Grid and labels.
    g.setFont (Palette::mono (9.0f));

    static const std::pair<float, const char*> marks[]
    {
        { 50.0f, "" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
        { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }
    };

    for (const auto& [hz, text] : marks)
    {
        const float x = xFor (Balance::erbNumber (hz));

        g.setColour (Palette::displayGrid);
        g.fillRect (x - 0.5f, plot.getY(), 1.0f, plot.getHeight());

        g.setColour (Palette::displayDim);
        g.drawText (text, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, axisHeight - 1.0f),
                    juce::Justification::centred, false);
    }

    for (const float db : { -6.0f, 0.0f, 6.0f })
    {
        const float y = yFor (db);

        g.setColour (db == 0.0f ? Palette::displayDim.withAlpha (0.8f) : Palette::displayGrid);
        g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f);

        g.setColour (Palette::displayDim);
        g.drawText (db > 0.0f ? "+6" : (db < 0.0f ? "-6" : "0"),
                    juce::Rectangle<float> (0.0f, y - 6.0f, labelGutter - 6.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    if (! reference.isValid())
    {
        g.setColour (Palette::displayDim);
        g.setFont (Palette::mono (10.5f));
        g.drawText (message, plot, juce::Justification::centred, true);
        return;
    }

    // ---- Everything relative to the reference's median, smoothed across
    // neighbouring bands for the eye. Only the drawing is smoothed; the
    // measurement is not.
    Balance::Bands upperDb, lowerDb, aDb;

    for (size_t i = 0; i < upperDb.size(); ++i)
    {
        upperDb[i] = reference.high[i] - reference.median[i];
        lowerDb[i] = reference.low[i]  - reference.median[i];
        aDb[i]     = a[i] - reference.median[i];
    }

    smoothAcrossBands (upperDb);
    smoothAcrossBands (lowerDb);
    smoothAcrossBands (aDb);

    const auto pointsFor = [this] (const Balance::Bands& db)
    {
        Points points;

        for (int b = 0; b < Balance::numBands; ++b)
            points[(size_t) b] = { xFor (Balance::bandCentreErb (b)), yFor (db[(size_t) b]) };

        return points;
    };

    const auto upper = pointsFor (upperDb);
    const auto lower = pointsFor (lowerDb);

    // ---- The reference's corridor: 10th to 90th percentile around its median.
    {
        juce::Path band;
        addCurve (band, upper, false);
        addCurve (band, reversed (lower), true);
        band.closeSubPath();

        g.setColour (Palette::displayText.withAlpha (0.17f));
        g.fillPath (band);

        juce::Path edges;
        addCurve (edges, upper, false);
        addCurve (edges, lower, false);

        g.setColour (Palette::displayDim);
        g.strokePath (edges, juce::PathStrokeType (1.0f));
    }

    // ---- A, against the median, with what lies outside the corridor in red.
    if (aValid)
    {
        Balance::Bands insideDb;

        for (size_t i = 0; i < aDb.size(); ++i)
            insideDb[i] = juce::jlimit (lowerDb[i], upperDb[i], aDb[i]);

        const auto curve  = pointsFor (aDb);
        const auto inside = pointsFor (insideDb);

        juce::Path outside;
        addCurve (outside, curve, false);
        addCurve (outside, reversed (inside), true);
        outside.closeSubPath();

        const float strength = aHolding ? 0.45f : 1.0f;

        g.setColour (Palette::warning.withAlpha (0.45f * strength));
        g.fillPath (outside);

        juce::Path line;
        addCurve (line, curve, false);

        g.setColour (Palette::lamp.withAlpha (strength));
        g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    paintCursor (g, upperDb, lowerDb, aDb);

    // ---- Legend, top right.
    {
        auto legend = plot.reduced (6.0f, 3.0f).removeFromTop (12.0f);
        g.setFont (Palette::mono (9.0f));

        const auto drawKey = [&] (juce::Colour colour, bool filled, const juce::String& text)
        {
            const float width = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            auto label = legend.removeFromRight (width);
            g.setColour (Palette::displayDim);
            g.drawText (text, label, juce::Justification::centredLeft, false);

            legend.removeFromRight (4.0f);
            auto swatch = legend.removeFromRight (12.0f).withSizeKeepingCentre (12.0f, filled ? 7.0f : 2.0f);
            g.setColour (colour);
            g.fillRect (swatch);
            legend.removeFromRight (12.0f);
        };

        drawKey (Palette::displayText.withAlpha (0.45f), true, "REFERENCE 10-90%");
        drawKey (Palette::lamp.withAlpha (aValid && ! aHolding ? 1.0f : 0.45f), false,
                 aValid ? (aHolding ? "A (HOLD)" : "A") : "A (NO SIGNAL)");
    }
}

//==============================================================================
void BalanceDisplay::mouseMove (const juce::MouseEvent& event)
{
    hoverX = event.position.x;
    repaint();
}

void BalanceDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

void BalanceDisplay::paintCursor (juce::Graphics& g, const Balance::Bands& upperDb,
                                  const Balance::Bands& lowerDb, const Balance::Bands& aDb)
{
    const auto plot = plotArea();

    if (hoverX < plot.getX() || hoverX > plot.getRight())
        return;

    // Back from the x axis to the ERB scale, then to a fractional band, and
    // the curves read there the way they are drawn: between band centres,
    // linearly.
    const float half  = Balance::bandSpacing() * 0.5f;
    const float first = Balance::bandCentreErb (0) - half;
    const float last  = Balance::bandCentreErb (Balance::numBands - 1) + half;
    const float erb   = first + (hoverX - plot.getX()) / plot.getWidth() * (last - first);

    const float position = juce::jlimit (0.0f, (float) (Balance::numBands - 1),
                                         (erb - Balance::bandCentreErb (0)) / Balance::bandSpacing());
    const int below = juce::jmin ((int) position, Balance::numBands - 2);
    const float fraction = position - (float) below;

    const auto at = [below, fraction] (const Balance::Bands& db)
    {
        return db[(size_t) below] + (db[(size_t) (below + 1)] - db[(size_t) below]) * fraction;
    };

    const float hz = Balance::hzFromErbNumber (erb);

    g.setColour (Palette::displayText.withAlpha (0.55f));
    g.fillRect (hoverX - 0.5f, plot.getY(), 1.0f, plot.getHeight());

    juce::String text = hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                      : hz < 10000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                                      : juce::String (hz / 1000.0f, 1) + " kHz";

    const auto signedDb = [] (float db)
    {
        return (db >= 0.0f ? "+" : "") + juce::String (db, 1);
    };

    if (aValid)
    {
        const float aHere = at (aDb);
        text << "   A " << signedDb (aHere) << " dB";

        g.setColour (Palette::lamp);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ hoverX, yFor (aHere) }));
    }

    text << "   REF " << signedDb (at (lowerDb)) << " .. " << signedDb (at (upperDb)) << " dB";

    // The readout along the bottom of the plot, beside the cursor, and on its
    // left once the cursor is in the right half - so it never runs off the
    // edge or into the legend at the top.
    g.setFont (Palette::mono (10.0f));
    const float width = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f;
    const bool leftOfCursor = hoverX > plot.getCentreX();

    juce::Rectangle<float> box (leftOfCursor ? hoverX - 6.0f - width : hoverX + 6.0f,
                                plot.getBottom() - 19.0f, width, 16.0f);

    g.setColour (Palette::display.withAlpha (0.9f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (Palette::displayText);
    g.drawText (text, box, juce::Justification::centred, false);
}

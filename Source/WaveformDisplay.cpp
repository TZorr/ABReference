//
//  WaveformDisplay.cpp
//  AB Reference
//

#include "WaveformDisplay.h"

#include "Palette.h"
#include "ParameterIds.h"
#include "TextUtf8.h"

using namespace ABLook;

namespace
{
    constexpr float cornerRadius = 4.0f;
}

//==============================================================================
WaveformDisplay::WaveformDisplay()
{
    setMouseCursor (juce::MouseCursor::IBeamCursor);
}

//==============================================================================
void WaveformDisplay::setClip (ReferenceClip::Ptr newClip)
{
    if (clip == newClip)
        return;

    clip = std::move (newClip);
    cacheValid = false;
    repaint();
}

void WaveformDisplay::setRegion (double startSeconds, double endSeconds)
{
    // The mouse wins while it is down. These values arrive from the parameters
    // via a 30 Hz timer, so during a drag they are always one tick behind the
    // pointer, and letting them through would drag the selection backwards.
    if (drag != Drag::none)
        return;

    if (juce::approximatelyEqual (startSeconds, regionStart)
        && juce::approximatelyEqual (endSeconds, regionEnd))
        return;

    regionStart = startSeconds;
    regionEnd   = endSeconds;
    repaint();
}

void WaveformDisplay::setPlayhead (double seconds, bool running)
{
    if (juce::approximatelyEqual (seconds, playhead) && running == transportRunning)
        return;

    playhead = seconds;
    transportRunning = running;
    repaint();
}

//==============================================================================
bool WaveformDisplay::hasRegion() const
{
    return clip != nullptr
             && ABParams::hasLoopRegion ((float) regionStart, (float) regionEnd);
}

double WaveformDisplay::secondsAt (int x) const
{
    if (clip == nullptr || getWidth() <= 0)
        return 0.0;

    const double proportion = juce::jlimit (0.0, 1.0, (double) x / (double) getWidth());
    return proportion * clip->getLengthSeconds();
}

float WaveformDisplay::xFor (double seconds) const
{
    if (clip == nullptr || clip->getLengthSeconds() <= 0.0)
        return 0.0f;

    return (float) (seconds / clip->getLengthSeconds() * (double) getWidth());
}

//==============================================================================
void WaveformDisplay::resized()
{
    cacheValid = false;
}

void WaveformDisplay::rebuildCache()
{
    cacheValid = true;

    if (clip == nullptr || getWidth() <= 0 || getHeight() <= 0)
    {
        cache = {};
        return;
    }

    cache = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);

    juce::Graphics g (cache);

    const int width  = getWidth();
    const int height = getHeight();
    const float middle = (float) height * 0.5f;
    const float halfHeight = middle - 4.0f;

    g.setColour (accentB.withAlpha (0.85f));

    // One column of pixels per column of pixels, each summarising however many
    // buckets land under it. Going the other way - one line per bucket - draws
    // 2048 lines into 460 pixels and produces a solid block.
    for (int x = 0; x < width; ++x)
    {
        const int firstBucket = x * ReferenceClip::numWaveformBuckets / width;
        const int lastBucket  = juce::jmax (firstBucket + 1,
                                            (x + 1) * ReferenceClip::numWaveformBuckets / width);

        float low = 0.0f, high = 0.0f;

        for (int bucket = firstBucket; bucket < lastBucket; ++bucket)
        {
            low  = juce::jmin (low,  clip->waveMin[(size_t) bucket]);
            high = juce::jmax (high, clip->waveMax[(size_t) bucket]);
        }

        // A column with signal in it but less than half a pixel of it still gets
        // a pixel. Otherwise quiet passages read as gaps in the file rather than
        // as quiet passages.
        const float top    = middle - high * halfHeight;
        const float bottom = middle - low  * halfHeight;

        g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
    }
}

//==============================================================================
void WaveformDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (panel);
    g.fillRoundedRectangle (bounds, cornerRadius);

    if (clip == nullptr)
    {
        paintEmpty (g);
        return;
    }

    if (! cacheValid)
        rebuildCache();

    if (cache.isValid())
        g.drawImageAt (cache, 0, 0);

    // Everything outside the region is veiled rather than hidden: it is still
    // the file, it is just not what is playing, and being able to see the shape
    // of the rest is how you find the next section to loop.
    if (hasRegion())
    {
        const float left  = xFor (regionStart);
        const float right = xFor (regionEnd);

        g.setColour (panel.withAlpha (0.78f));
        g.fillRect (0.0f, 0.0f, left, (float) getHeight());
        g.fillRect (right, 0.0f, (float) getWidth() - right, (float) getHeight());

        // Neutral, not accentA. Blue means A everywhere else on this panel, and
        // the loop region is a fact about B - borrowing the other side's colour
        // for it would be the panel telling a small lie every time you look at it.
        g.setColour (text.withAlpha (0.06f));
        g.fillRect (left, 0.0f, right - left, (float) getHeight());

        g.setColour (text.withAlpha (0.9f));

        for (const float edge : { left, right })
        {
            g.fillRect (edge - 1.0f, 0.0f, 2.0f, (float) getHeight());

            // Tabs, so the edges read as something to take hold of rather than
            // as two lines that happen to be there.
            g.fillRect (edge - 2.0f, 0.0f, 4.0f, 7.0f);
            g.fillRect (edge - 2.0f, (float) getHeight() - 7.0f, 4.0f, 7.0f);
        }
    }

    // The playhead, thinner and dimmer than the region edges: the edges are the
    // thing you take hold of, this is only information, and it has motion to
    // carry attention without needing brightness as well. Dimmer again when the
    // transport is stopped, because a bright line sitting still looks like a
    // position and a dim one looks like the last position, which is what it is.
    const float playheadX = xFor (playhead);

    if (playheadX >= 0.0f && playheadX <= (float) getWidth())
    {
        g.setColour (text.withAlpha (transportRunning ? 0.55f : 0.22f));
        g.fillRect (playheadX, 0.0f, 1.0f, (float) getHeight());
    }

    g.setColour (background.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
}

void WaveformDisplay::paintEmpty (juce::Graphics& g)
{
    g.setColour (dimText);
    g.setFont (uiFont (12.0f));
    // Not "no reference loaded" - the info line directly below already says
    // that, and the button below that says it a third time. What nothing else on
    // the panel says is what this strip is for once a file is in it.
    g.drawText (utf8 ("Drag across the waveform to loop a section"),
                getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
WaveformDisplay::Drag WaveformDisplay::whatIsUnder (int x) const
{
    if (! hasRegion())
        return Drag::creating;

    if (std::abs ((float) x - xFor (regionStart)) <= (float) edgeGrabPixels)
        return Drag::movingStart;

    if (std::abs ((float) x - xFor (regionEnd)) <= (float) edgeGrabPixels)
        return Drag::movingEnd;

    return Drag::creating;
}

void WaveformDisplay::mouseMove (const juce::MouseEvent& event)
{
    const bool onEdge = hasRegion() && whatIsUnder (event.x) != Drag::creating;

    setMouseCursor (onEdge ? juce::MouseCursor::LeftRightResizeCursor
                           : juce::MouseCursor::IBeamCursor);
}

void WaveformDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (clip == nullptr)
        return;

    drag = whatIsUnder (event.x);
    dragStartX = event.x;
    dragMoved = false;

    // The anchor is the edge that stays put. Grabbing one edge anchors the other
    // one, which is what makes dragging an edge past its partner turn into a
    // selection the other way round rather than a region with negative length.
    // Every case named rather than a default, so that adding a drag kind later
    // is a compiler error here instead of a silent fall into "anchor where the
    // mouse is" - which would look almost right and be wrong at the edges.
    switch (drag)
    {
        case Drag::movingStart: dragAnchor = regionEnd;   break;
        case Drag::movingEnd:   dragAnchor = regionStart; break;
        case Drag::none:
        case Drag::creating:    dragAnchor = secondsAt (event.x); break;
    }

    if (onDragStarted != nullptr)
        onDragStarted();
}

void WaveformDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (drag == Drag::none || clip == nullptr)
        return;

    if (std::abs (event.x - dragStartX) >= dragThresholdPixels)
        dragMoved = true;

    if (! dragMoved)
        return;

    const double here = secondsAt (event.x);

    regionStart = juce::jmin (dragAnchor, here);
    regionEnd   = juce::jmax (dragAnchor, here);

    publishRegion();
    repaint();
}

void WaveformDisplay::mouseUp (const juce::MouseEvent&)
{
    const bool wasDragging = drag != Drag::none;
    const bool moved = dragMoved;

    drag = Drag::none;
    dragMoved = false;

    if (! wasDragging)
        return;

    // A press that never moved is a click, and a click on the waveform means
    // "all of it" - which is also the only way back to the whole file without
    // having to drag a selection over its entire width.
    if (! moved)
    {
        regionStart = 0.0;
        regionEnd   = 0.0;
        repaint();

        if (onRegionCleared != nullptr)
            onRegionCleared();
    }

    if (onDragEnded != nullptr)
        onDragEnded (moved);
}

void WaveformDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    // The first click of the pair has already cleared it. This exists so that
    // the gesture people reach for out of habit does not set a tiny region.
    regionStart = 0.0;
    regionEnd   = 0.0;
    repaint();

    if (onRegionCleared != nullptr)
        onRegionCleared();
}

void WaveformDisplay::publishRegion()
{
    if (onRegionChanged != nullptr)
        onRegionChanged (regionStart, regionEnd);
}

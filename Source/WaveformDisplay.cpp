//
//  WaveformDisplay.cpp
//  AB Reference
//

#include "WaveformDisplay.h"

#include "ParameterIds.h"
#include "TextUtf8.h"
#include "UI/Palette.h"

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

void WaveformDisplay::setActive (bool isActive)
{
    if (active == isActive)
        return;

    active = isActive;
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
    const float halfHeight = middle - 6.0f;

    // Scaled to the clip's own loudest RMS rather than to full scale, so that
    // the loudest stretch of any reference fills most of the strip. The picture
    // is for finding sections, not for reading levels - the meters below it do
    // that - and on a finished master the sections are close together: the
    // reference this was tuned on sits within a dB of -10.5 dBFS RMS for six of
    // its eight minutes. Drawn against full scale, that loudest RMS reached 40%
    // of the height and a dB was three pixels; drawn against itself, a dB is a
    // tenth of the strip and the break in the middle is plainly a break.
    //
    // The peaks share the scale and are clipped at the edge. That makes the
    // faint outline mostly a backdrop on a loud master, which is honest: it is
    // what a limiter does to peaks.
    float loudestRms = 0.0f;

    for (int bucket = 0; bucket < ReferenceClip::numWaveformBuckets; ++bucket)
        loudestRms = juce::jmax (loudestRms, clip->waveRms[(size_t) bucket]);

    const float scale = 0.8f * halfHeight / juce::jmax (loudestRms, 1.0e-4f);

    const auto peakColour = Palette::displayDim.withAlpha (0.55f);
    const auto rmsColour  = Palette::displayText;

    g.setColour (Palette::displayGrid);
    g.fillRect (0.0f, middle - 0.5f, (float) width, 1.0f);

    // One column of pixels per column of pixels, each summarising however many
    // buckets land under it. Going the other way - one line per bucket - draws
    // 2048 lines into 600 pixels and produces a solid block.
    for (int x = 0; x < width; ++x)
    {
        const int firstBucket = x * ReferenceClip::numWaveformBuckets / width;
        const int lastBucket  = juce::jmax (firstBucket + 1,
                                            (x + 1) * ReferenceClip::numWaveformBuckets / width);

        float low = 0.0f, high = 0.0f, meanSquare = 0.0f;

        for (int bucket = firstBucket; bucket < lastBucket; ++bucket)
        {
            low  = juce::jmin (low,  clip->waveMin[(size_t) bucket]);
            high = juce::jmax (high, clip->waveMax[(size_t) bucket]);

            const float rms = clip->waveRms[(size_t) bucket];
            meanSquare += rms * rms;
        }

        // Buckets hold near enough the same number of samples each, so the
        // mean of their squares is the square of the column's RMS.
        const float rms = std::sqrt (meanSquare / (float) (lastBucket - firstBucket));

        // Peaks first, faint: the outline says where the limiter was working.
        // The RMS over it, solid: that is the part that moves between a verse
        // and a chorus, and the part the eye should land on. Both get at least
        // a pixel wherever there is signal, so quiet passages read as quiet
        // rather than as gaps in the file.
        const float top    = middle - juce::jmin (high * scale, halfHeight);
        const float bottom = middle + juce::jmin (-low * scale, halfHeight);

        g.setColour (peakColour);
        g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));

        const float rmsHalf = juce::jmax (0.5f, rms * scale);

        g.setColour (rmsColour);
        g.fillRect ((float) x, middle - rmsHalf, 1.0f, rmsHalf * 2.0f);
    }
}

//==============================================================================
void WaveformDisplay::paint (juce::Graphics& g)
{
    // Square and borderless: the strip sits inside the panel's display, which
    // draws the rounded frame, the file name and the lines of text around it.
    g.fillAll (Palette::display);

    if (clip == nullptr)
    {
        paintEmpty (g);
        return;
    }

    if (! cacheValid)
        rebuildCache();

    // The region behind the waveform, a shade lighter than the display, so the
    // part that is playing is lit from behind as well as drawn brighter.
    const bool region = hasRegion();
    const float left  = region ? xFor (regionStart) : 0.0f;
    const float right = region ? xFor (regionEnd)   : 0.0f;

    if (region)
    {
        g.setColour (Palette::displayGrid);
        g.fillRect (left, 0.0f, right - left, (float) getHeight());
    }

    // Dimmed while A is playing. The waveform is B, and at full strength it is
    // the brightest thing on the panel whichever side you are listening to;
    // this way the panel's brightest shape changes when the sound does.
    if (cache.isValid())
    {
        g.setOpacity (active ? 1.0f : 0.45f);
        g.drawImageAt (cache, 0, 0);
        g.setOpacity (1.0f);
    }

    // Everything outside the region is veiled rather than hidden: it is still
    // the file, it is just not what is playing, and being able to see the shape
    // of the rest is how you find the next section to loop. The veil is the
    // display colour, so the orange outside turns to the display's dim orange.
    if (region)
    {
        g.setColour (Palette::display.withAlpha (0.6f));
        g.fillRect (0.0f, 0.0f, left, (float) getHeight());
        g.fillRect (right, 0.0f, (float) getWidth() - right, (float) getHeight());

        g.setColour (Palette::accent);

        for (const float edge : { left, right })
        {
            g.fillRect (edge - 0.75f, 0.0f, 1.5f, (float) getHeight());

            // Handles, so the edges read as something to take hold of rather
            // than as two lines that happen to be there.
            g.fillRoundedRectangle (edge - 3.0f, 0.0f, 6.0f, 7.0f, 1.0f);
            g.fillRoundedRectangle (edge - 3.0f, (float) getHeight() - 7.0f, 6.0f, 7.0f, 1.0f);
        }
    }

    // The playhead in the pad colour, as on Kitbox's sample display: not
    // orange, so it never reads as a region edge. Dimmer when the transport is
    // stopped, because a bright line sitting still looks like a position and a
    // dim one looks like the last position, which is what it is.
    const float playheadX = xFor (playhead);

    if (playheadX >= 0.0f && playheadX <= (float) getWidth())
    {
        g.setColour (Palette::padText.withAlpha (transportRunning ? 0.9f : 0.3f));
        g.fillRect (playheadX - 0.75f, 0.0f, 1.5f, (float) getHeight());
    }
}

void WaveformDisplay::paintEmpty (juce::Graphics& g)
{
    g.setColour (Palette::displayDim);
    g.setFont (Palette::mono (10.5f));
    // Not "no reference loaded" - the line above already says that. What
    // nothing else on the panel says is what this strip is for once a file is
    // in it.
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

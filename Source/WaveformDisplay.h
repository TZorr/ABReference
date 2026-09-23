//
//  WaveformDisplay.h
//  AB Reference
//
//  The reference, drawn, with the loop region you can drag out on top of it.
//
//  It exists as its own component rather than as another paint method on the
//  editor for the usual reason - the editor already has a personality and this
//  is a second one - and for one specific one: this is the only part of the
//  panel that owns a mouse gesture with state in it. A drag that can be a new
//  selection, a nudge of either edge, or a click that clears everything is a
//  small state machine, and small state machines spread across a class that also
//  lays out fifteen other controls are how a click ends up doing two things.
//
//  It knows nothing about parameters. It reports what the mouse did and is told
//  what to show; the editor is the only thing that touches APVTS. That is what
//  makes the region survivable as automation, state, and undo without this file
//  having an opinion about any of them.
//
//  What it draws is the clip's own min/max envelope, built once on the loader
//  thread. This component never reads the audio buffer - at 30 Hz that would be
//  a walk over up to half a gigabyte per second for a picture that has not
//  changed since the file was loaded.
//

#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ReferenceClip.h"

class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay();

    //==============================================================================
    // Message thread. All of these are cheap and safe to call every timer tick.

    /** Takes its own reference to the clip, so what is on screen cannot be freed
        underneath it by a load that happens mid-paint. */
    void setClip (ReferenceClip::Ptr newClip);

    /** Seconds into the reference. A span shorter than ABParams::minLoopSeconds
        draws as "no region", which is what both parameters at zero produce.
        Ignored while the mouse is dragging, so the 30 Hz round trip through the
        parameters cannot stutter the selection being drawn. */
    void setRegion (double startSeconds, double endSeconds);

    void setPlayhead (double seconds, bool transportRunning);

    //==============================================================================
    // Reported to the editor, which owns the parameters.

    /** A drag has begun. The editor opens one change gesture for the whole of
        it, so a host's undo sees one edit rather than sixty. */
    std::function<void()> onDragStarted;
    std::function<void (double startSeconds, double endSeconds)> onRegionChanged;
    /** The drag is over. True when it left a region behind, false when it was a
        click that cleared one - the editor turns Loop on for the first and not
        for the second, so that a stray click cannot switch the plugin's audio
        path on behind your back. */
    std::function<void (bool regionWasSet)> onDragEnded;

    /** Back to the whole file. Always arrives inside a drag, so the editor does
        not open a gesture of its own for it. */
    std::function<void()> onRegionCleared;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Drag { none, creating, movingStart, movingEnd };

    void rebuildCache();
    void paintEmpty (juce::Graphics&);

    double secondsAt (int x) const;
    float  xFor (double seconds) const;
    bool   hasRegion() const;
    Drag   whatIsUnder (int x) const;

    void publishRegion();

    ReferenceClip::Ptr clip;

    // The waveform is painted once into here and blitted afterwards. The
    // playhead moves 30 times a second; redrawing 2048 buckets behind it every
    // time would be the most expensive thing in the plugin, and it would be
    // expensive to draw something that has not changed.
    juce::Image cache;
    bool cacheValid = false;

    double regionStart = 0.0, regionEnd = 0.0;   // seconds
    double playhead = 0.0;
    bool   transportRunning = false;

    Drag drag = Drag::none;
    double dragAnchor = 0.0;     // seconds, the edge the drag started from
    int    dragStartX = 0;
    bool   dragMoved = false;

    // Within this many pixels of an edge, the drag grabs that edge instead of
    // starting a new selection. Five is about a fingertip's worth of aim at this
    // size, and small enough not to swallow a genuine new selection begun next
    // to an existing one.
    static constexpr int edgeGrabPixels = 5;

    // A press that moves less than this is a click, not a selection. Without it
    // every click would set a region a few milliseconds long.
    static constexpr int dragThresholdPixels = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};

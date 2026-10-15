//
//  BalanceDisplay.h
//  AB Reference
//
//  A's spectral balance against the reference's, drawn where the waveform is
//  when the display is switched to Balance.
//
//  Everything is relative to the reference's median, so the reference itself
//  is the zero line and the question the picture answers is the one that gets
//  asked at a mastering desk: where is my mix different from the record I am
//  holding it against, at the same loudness?
//
//  The orange band is the reference's own 10-90 % range over its 3 s windows -
//  how much its balance moves between sections. It is narrow in the mids and
//  opens out at both ends, which is where the tub shape comes from: a few dB
//  more sub bass than the median is still inside what this record does, a few
//  dB more at 3 kHz is not. Where A leaves the band, the gap is filled red.
//
//  The frequency axis is the ERB scale the bands are spaced on, so every band
//  gets the same width on screen - the ear's view of the spectrum, with more
//  room for the mids than a log axis gives them.
//
//  Hovering shows a cursor with the numbers under it: the frequency, A against
//  the reference's median, and the reference's own 10-90 % range there - all
//  read from the curves as drawn.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BalanceAnalyser.h"

class BalanceDisplay : public juce::Component
{
public:
    BalanceDisplay();

    /** The reference's corridor; an invalid one shows `emptyMessage` instead. */
    void setCorridor (const Balance::Corridor& corridor, const juce::String& emptyMessage);

    /** A's balance. `holding` dims it: A has been silent for a while and this is
        the last reading, not a live one. */
    void setA (const Balance::Bands& balanceDb, bool valid, bool holding);

    void paint (juce::Graphics&) override;

    // The cursor: a vertical line at the mouse and a readout of the frequency,
    // A's deviation and the reference's range there.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** The plotted range either side of the reference's median. */
    static constexpr float rangeDb = 12.0f;

private:
    using Points = std::array<juce::Point<float>, Balance::numBands>;

    static void smoothAcrossBands (Balance::Bands& db);
    static Points reversed (const Points& points);
    static void addCurve (juce::Path& path, const Points& points, bool continuePath);

    juce::Rectangle<float> plotArea() const;
    float xFor (float erb) const;
    float yFor (float db) const;

    Balance::Corridor reference;
    juce::String message;

    Balance::Bands a {};
    bool aValid = false, aHolding = false;

    /** x of the cursor in local coordinates, negative when there is none. */
    float hoverX = -1.0f;

    void paintCursor (juce::Graphics&, const Balance::Bands& upperDb,
                      const Balance::Bands& lowerDb, const Balance::Bands& aDb);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BalanceDisplay)
};

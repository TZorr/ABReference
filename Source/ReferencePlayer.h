//
//  ReferencePlayer.h
//  AB Reference
//
//  Holds the clip the audio thread is playing, and hands over a new one
//  without either thread waiting for the other or freeing anything the
//  other might still be reading.
//
//  **The audio thread never frees, and the message thread never frees
//  anything the audio thread can still reach.**
//
//  How:
//
//    - `pending` is a single atomic slot. The message thread puts a clip in
//      it with the reference count already raised; the audio thread takes
//      it with an exchange. Exactly one exchange can win a given pointer,
//      so a clip replaced before it was picked up is freed by the thread
//      that still owns it, and a picked-up clip is never freed by anyone
//      but the audio thread's successor.
//    - `active` is a raw pointer owned solely by the audio thread - nothing
//      else reads or writes it, so it needs no synchronisation.
//    - the outgoing clip goes into one of eight retire slots, emptied by a
//      timer on the message thread. Only the audio thread fills a slot and
//      only the message thread empties one, so "I saw a free slot" is a
//      promise that survives until used.
//    - if all eight are full, the audio thread declines the swap and keeps
//      playing what it has - loading a new reference just takes a few
//      milliseconds longer. Nothing here allocates, frees, blocks, or
//      drops a sample.
//
//  It also owns the loop region: the mapping stays a pure function of the
//  timeline position, no state carried between blocks, so the same
//  position always produces the same samples - what keeps the null test in
//  the README meaningful.
//
//  **The seam.** A loop wrap is a step in the signal, and a step is a
//  click. The fix is an overlap-add: the region's last 10 ms are mixed,
//  equal-power, onto its first 10 ms, and the repeat runs from there. Over
//  a selection G of length L with a fade of F samples:
//
//      period P = L - F
//      c in [0, F)  ->  G[c] * sin(t) + G[c + P] * cos(t),  t = (c + 0.5)/F * pi/2
//      c in [F, P)  ->  G[c]
//
//  continuous at both ends of the blend, so no step remains anywhere in
//  the cycle. Two consequences: **a repeat is 10 ms shorter than the
//  selection** (the price of a crossfade reading nothing outside it - a
//  region dragged from sample zero, the default whole-file loop, has
//  nothing before it to blend with instead); and **the first pass is
//  clean**, since only repeats cross a seam.
//
//  Equal power rather than linear (see ABEngine.h): the two sides of a
//  seam are uncorrelated programme, so their powers add and a linear fade
//  dips about 3 dB - over 10 ms, a click.
//
//  The JUCE tutorial's ReferenceCountedArray-plus-timer pattern has the
//  message thread assign to a shared ReferenceCountedObjectPtr while the
//  audio thread copy-constructs from it - a data race on the pointer
//  itself, working in practice only because the assignment happens to
//  compile to a single store. An explicit atomic costs nothing and is
//  actually true.
//
#pragma once

#include <array>
#include <atomic>

#include "ReferenceClip.h"

class ReferencePlayer
{
public:
    /** Which part of the reference repeats.

        `enabled` is the Loop toggle. A region shorter than one sample - which is
        what both parameters sitting at zero produces - means the whole file, so
        Loop on its own behaves exactly as it did before regions existed. */
    struct LoopRegion
    {
        bool enabled = false;
        int  start   = 0;   // samples into the clip
        int  end     = 0;   // exclusive
    };

    ReferencePlayer() = default;
    ~ReferencePlayer();

    //==============================================================================
    // Message thread

    /** Hands a clip over to the audio thread. Pass nullptr to clear.
        Returns immediately; the swap itself happens on the next audio block. */
    void publish (ReferenceClip::Ptr newClip);

    /** Frees anything the audio thread has finished with. Call from a timer. */
    void collectRetiredClips();

    /** Frees everything, including what the audio thread is holding. Only legal
        where the host guarantees processBlock has stopped: releaseResources and
        the destructor. Anywhere else this is exactly the use-after-free the
        rest of the class exists to prevent. */
    void releaseAllClips();

    //==============================================================================
    // Audio thread

    /** Takes a newly published clip if there is one and somewhere to put the
        old one. Call once at the top of processBlock, before reading. */
    void swapInPendingClip() noexcept;

    /** Fills destination with the reference from the given timeline position.
        destination must have at least two channels and numSamples samples.
        Returns false, having written silence, when no clip is loaded. */
    bool read (juce::AudioBuffer<float>& destination,
               int64_t startSample,
               int numSamples,
               const LoopRegion& loop) noexcept;

    /** Audio thread's view of what is loaded. Null until the first swap. */
    const ReferenceClip* getActiveClip() const noexcept { return active; }

    //==============================================================================
    // The position mapping, exposed so that whoever wants to draw a playhead uses
    // the same arithmetic the audio thread reads with rather than a second copy
    // of it that can drift out of agreement with it.

    /** Clamps a region against a clip length, and turns a degenerate one into
        the whole file. */
    static LoopRegion resolveRegion (const LoopRegion& requested, int length) noexcept;

    /** The clip sample that a timeline position reads. With looping off this is
        the position itself, which may well be outside the clip - that is the
        caller's answer to "is the reference silent here?". */
    static int64_t localSampleFor (int64_t position, int length,
                                   double sampleRate, const LoopRegion& loop) noexcept;

    /** How long the crossfade at the seam actually is, in samples. Zero when the
        region is too short to hide one. */
    static int crossfadeSamplesFor (int regionLength, double sampleRate) noexcept;

    /** How long a repeat actually lasts: the selection, less the crossfade. */
    static int loopPeriodFor (int regionLength, int fade) noexcept;

private:
    void readStraight (juce::AudioBuffer<float>& destination,
                       const juce::AudioBuffer<float>& source,
                       int channels,
                       int64_t startSample,
                       int numSamples,
                       int length) noexcept;

    void readLooped (juce::AudioBuffer<float>& destination,
                     const juce::AudioBuffer<float>& source,
                     int channels,
                     int64_t startSample,
                     int numSamples,
                     int length,
                     const LoopRegion& loop) noexcept;

    // 10 ms at the loop seam. Slightly longer than the A/B switch's 8 ms because
    // this one is not a switch: nobody is listening across it for a difference,
    // so there is no comparison for it to blur. It is the shortest fade that
    // reliably hides a full-scale step on dense material, and it is a perceptual
    // choice rather than a performance one, so it is not a parameter.
    static constexpr double loopCrossfadeSeconds = 0.010;

    static constexpr int numRetireSlots = 8;

    std::atomic<ReferenceClip*> pending { nullptr };
    std::array<std::atomic<ReferenceClip*>, numRetireSlots> retired {};

    ReferenceClip* active = nullptr;   // audio thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferencePlayer)
};

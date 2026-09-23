//
//  ReferenceBus.h
//  AB Reference
//
//  Three references, one output, and the two things that only become problems
//  once there is more than one reference.
//
//  **Each slot needs its own gain.** Level matching moves a reference to sit
//  where A sits, and every reference has a different loudness of its own, so
//  every slot has a different match. That forces the gain to be applied per
//  slot *before* the slots are mixed: a single gain applied after the mix would
//  be right for at most one of the two slots involved in a switch, and audibly
//  wrong for the other for the length of the crossfade. This is the whole
//  reason this class exists rather than the processor simply picking one of
//  three players.
//
//  **Switching slots is a switch, so it fades.** Same 8 ms, same equal-power
//  law, same argument as the A/B button: two references are uncorrelated
//  programme material, their powers add rather than their amplitudes, and a
//  linear fade therefore dips about 3 dB in the middle - heard not as a dip but
//  as a click, and as the tool doing something to the material.
//
//  What this class is not: it does not know about A. It hands ABEngine one
//  stereo bus that is already at the right level, and ABEngine decides how much
//  of it you hear.
//

#pragma once

#include <array>

#include "ParameterIds.h"
#include "ReferencePlayer.h"

class ReferenceBus
{
public:
    ReferenceBus() = default;

    /** Allocates the two scratch buffers a crossfade needs. Never on the audio
        thread. */
    void prepare (double sampleRate, int maximumBlockSize);

    /** Jumps to the given slot with no fade. prepareToPlay only. */
    void reset (int slot) noexcept;

    //==============================================================================
    // Message thread

    void publish (int slot, ReferenceClip::Ptr clip);
    void collectRetiredClips();
    void releaseAllClips();

    //==============================================================================
    // Audio thread

    /** Call once at the top of processBlock, before reading. */
    void swapInPendingClips() noexcept;

    /** Which slot should be heard. A change starts an 8 ms crossfade. */
    void setSlot (int slot) noexcept;

    /** Linear gain for one slot. Applied to that slot's audio only.
        `audible` is false while the output is A: the gains then jump to their
        targets instead of ramping, so that selecting a slot and pressing B
        lands at the matched level rather than sliding into it over 100 ms. */
    void setSlotGain (int slot, float linearGain, bool audible) noexcept;

    /** Reads the active slot - both slots while a crossfade is running - into
        `destination`, which must have two channels and at least numSamples.
        `regionFor` supplies each slot's own loop region. */
    void read (juce::AudioBuffer<float>& destination,
               int64_t startSample,
               int numSamples,
               const std::array<ReferencePlayer::LoopRegion, ABParams::numSlots>& regions) noexcept;

    /** Audio thread's view. During a crossfade this is the slot being faded
        *to* - the one the panel is already showing. */
    int getSlot() const noexcept { return targetSlot; }

    const ReferenceClip* getActiveClip (int slot) const noexcept
    {
        return players[(size_t) juce::jlimit (0, ABParams::numSlots - 1, slot)].getActiveClip();
    }

    bool isCrossfading() const noexcept { return fadeRemaining > 0; }

private:
    // The same 8 ms as ABEngine, for the same reason. Not shared as a constant
    // because they are two independent perceptual choices that happen to agree.
    static constexpr double crossfadeSeconds = 0.008;
    static constexpr double gainSmoothingSeconds = 0.1;

    std::array<ReferencePlayer, ABParams::numSlots> players;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, ABParams::numSlots> gains;

    juce::AudioBuffer<float> scratchOut, scratchIn;

    int   currentSlot   = 0;
    int   targetSlot    = 0;
    int   fadeLength    = 1;
    int   fadeRemaining = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceBus)
};

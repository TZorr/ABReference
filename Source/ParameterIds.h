//
//  ParameterIds.h
//  AB Reference
//
//  Every parameter id, range and default in one place, because a parameter id
//  is a contract with the host: once a session has been saved against it, it
//  can never be changed without silently breaking that session. Scattering the
//  strings across the processor and the editor is how a rename gets applied in
//  one of the two places.
//
//  The version suffix on the state tree is separate from the plugin version on
//  purpose. It only moves when the *shape* of the state changes, which lets a
//  0.2 build read a 0.1 session as long as nothing structural moved.
//

#pragma once

#include <juce_core/juce_core.h>

namespace ABParams
{
    // Parameter ids. Hosts store these; treat them as immutable.
    inline constexpr const char* ab         = "ab";
    inline constexpr const char* levelMatch = "levelMatch";
    inline constexpr const char* trimDb     = "trimDb";
    inline constexpr const char* offsetMs   = "offsetMs";
    inline constexpr const char* monoSum    = "monoSum";
    inline constexpr const char* loop       = "loop";
    inline constexpr const char* refSlot    = "refSlot";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int version = 1;

    /** Three references, switchable without reloading. Three rather than two
        because two is just the A/B switch again, and rather than four because
        four does not fit one row of this panel at a readable size. */
    inline constexpr int numSlots = 3;

    /** The loop region is per slot, so these are indexed rather than single.
        Six parameters instead of swapping one pair's values on every slot
        change: a slot change can arrive on the audio thread, where writing a
        parameter is illegal, and routing it through the message thread would
        leave a window in which the new reference plays with the old one's
        region. Six independent parameters have no such window: the audio
        thread simply reads the pair belonging to whichever slot is active.
        Like the single pair before them these are not automatable - a loop
        region is drawn on a waveform, not ridden on a fader - but they are
        parameters so that the host's undo and the drag gestures keep working. */
    inline const char* loopStartSecFor (int slot) noexcept
    {
        static const char* ids[] { "loopStartSec1", "loopStartSec2", "loopStartSec3" };
        return ids[(size_t) juce::jlimit (0, numSlots - 1, slot)];
    }

    inline const char* loopEndSecFor (int slot) noexcept
    {
        static const char* ids[] { "loopEndSec1", "loopEndSec2", "loopEndSec3" };
        return ids[(size_t) juce::jlimit (0, numSlots - 1, slot)];
    }

    // Trim is deliberately narrow. This is a comparison aid, not a fader: if
    // you need more than 12 dB to line two masters up, one of them is not a
    // master and the number is telling you something useful.
    inline constexpr float trimMinDb = -12.0f;
    inline constexpr float trimMaxDb =  12.0f;

    // The same 12 dB ceiling clamps automatic level matching, for the same
    // reason plus one more: a reference pushed up 20 dB would clip, and this
    // plugin does not put a limiter in front of a reference.
    inline constexpr float matchLimitDb = 12.0f;

    // +/- 5 seconds covers both intended uses - lining the reference up with a
    // song that does not start at bar 1, and dialling out a host's plugin
    // delay compensation error, which is never more than a few thousand
    // samples.
    inline constexpr float offsetMinMs = -5000.0f;
    inline constexpr float offsetMaxMs =  5000.0f;

    // The loop region, in seconds into the reference. Seconds rather than a
    // fraction of the file because that is what the panel, the waveform and the
    // offset control all already think in, and because a fraction silently means
    // something different the moment a different reference is loaded.
    //
    // The range is the loader's own 30 minute ceiling: a region can never point
    // past a file the loader would have refused.
    inline constexpr float loopMaxSeconds = 30.0f * 60.0f;

    // A region counts as set only when it is at least this long. Both values at
    // zero - the default, and what every session saved before this existed
    // restores to - therefore means "the whole file", which is exactly what Loop
    // did on its own.
    inline constexpr float minLoopSeconds = 0.05f;

    inline bool hasLoopRegion (float startSec, float endSec) noexcept
    {
        return endSec - startSec >= minLoopSeconds;
    }

    // State-tree property names for things that are state but not parameters.
    // The reference path is not a parameter because it is not automatable, not
    // interpolatable, and not a number.
    inline constexpr const char* stateTreeType   = "ABReferenceState";
    inline constexpr const char* propStateVersion = "stateVersion";

    /** One path per slot. `propReferencePath` is the pre-slots single-reference
        name, still read so that a session saved before this existed restores
        its reference into slot 1 rather than losing it. */
    inline constexpr const char* propReferencePath = "referencePath";

    inline const char* propReferencePathFor (int slot) noexcept
    {
        static const char* keys[] { "referencePath1", "referencePath2", "referencePath3" };
        return keys[(size_t) juce::jlimit (0, numSlots - 1, slot)];
    }
}

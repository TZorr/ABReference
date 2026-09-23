//
//  ReferenceBus.cpp
//  AB Reference
//

#include "ReferenceBus.h"

void ReferenceBus::prepare (double sampleRate, int maximumBlockSize)
{
    const int blockSize = juce::jmax (1, maximumBlockSize);

    scratchOut.setSize (2, blockSize, false, true, true);
    scratchIn .setSize (2, blockSize, false, true, true);
    scratchOut.clear();
    scratchIn.clear();

    fadeLength = juce::jmax (1, (int) std::lround (sampleRate * crossfadeSeconds));

    for (auto& g : gains)
        g.reset (sampleRate, gainSmoothingSeconds);

    reset (targetSlot);
}

void ReferenceBus::reset (int slot) noexcept
{
    currentSlot = targetSlot = juce::jlimit (0, ABParams::numSlots - 1, slot);
    fadeRemaining = 0;

    for (auto& g : gains)
        g.setCurrentAndTargetValue (g.getTargetValue());
}

//==============================================================================
void ReferenceBus::publish (int slot, ReferenceClip::Ptr clip)
{
    players[(size_t) juce::jlimit (0, ABParams::numSlots - 1, slot)].publish (std::move (clip));
}

void ReferenceBus::collectRetiredClips()
{
    for (auto& p : players)
        p.collectRetiredClips();
}

void ReferenceBus::releaseAllClips()
{
    for (auto& p : players)
        p.releaseAllClips();
}

//==============================================================================
void ReferenceBus::swapInPendingClips() noexcept
{
    for (auto& p : players)
        p.swapInPendingClip();
}

void ReferenceBus::setSlot (int slot) noexcept
{
    const int wanted = juce::jlimit (0, ABParams::numSlots - 1, slot);

    if (wanted == targetSlot)
        return;

    // A switch arriving mid-fade re-aims at the new slot from wherever the fade
    // has got to, rather than restarting it. Restarting would make a fast
    // 1-2-3 walk sound like three full crossfades stacked on top of each other.
    if (fadeRemaining > 0)
        currentSlot = targetSlot;

    targetSlot    = wanted;
    fadeRemaining = fadeLength;
}

void ReferenceBus::setSlotGain (int slot, float linearGain, bool audible) noexcept
{
    auto& g = gains[(size_t) juce::jlimit (0, ABParams::numSlots - 1, slot)];

    g.setTargetValue (linearGain);

    if (! audible)
        g.setCurrentAndTargetValue (linearGain);
}

//==============================================================================
void ReferenceBus::read (juce::AudioBuffer<float>& destination,
                         int64_t startSample,
                         int numSamples,
                         const std::array<ReferencePlayer::LoopRegion, ABParams::numSlots>& regions) noexcept
{
    if (numSamples <= 0)
        return;

    if (numSamples > scratchOut.getNumSamples())
    {
        // A block bigger than promised. Silence beats reallocating underneath
        // the audio thread.
        destination.clear (0, numSamples);
        return;
    }

    const size_t target = (size_t) targetSlot;

    // The common case by a very long way: no fade running, one slot to read.
    if (fadeRemaining <= 0)
    {
        players[target].read (destination, startSample, numSamples, regions[target]);

        auto& g = gains[target];

        for (int n = 0; n < numSamples; ++n)
        {
            const float gain = g.getNextValue();

            for (int ch = 0; ch < destination.getNumChannels(); ++ch)
                destination.getWritePointer (ch)[n] *= gain;
        }

        currentSlot = targetSlot;
        return;
    }

    const size_t outgoing = (size_t) currentSlot;

    players[outgoing].read (scratchOut, startSample, numSamples, regions[outgoing]);
    players[target]  .read (scratchIn,  startSample, numSamples, regions[target]);

    float* out[2] { destination.getWritePointer (0), destination.getWritePointer (1) };
    const float* fromA[2] { scratchOut.getReadPointer (0), scratchOut.getReadPointer (1) };
    const float* fromB[2] { scratchIn .getReadPointer (0), scratchIn .getReadPointer (1) };

    auto& gOut = gains[outgoing];
    auto& gIn  = gains[target];

    for (int n = 0; n < numSamples; ++n)
    {
        // Each slot carries its own gain across the fade. This is the line the
        // whole class exists for - one gain here would be the outgoing
        // reference's or the incoming one's, and wrong for the other.
        const float levelOut = gOut.getNextValue();
        const float levelIn  = gIn .getNextValue();

        // Equal power: position runs 0 -> 1 across the fade, and cos/sin keep
        // the sum of powers at one throughout.
        const float position = 1.0f - (float) fadeRemaining / (float) fadeLength;
        const float angle    = position * juce::MathConstants<float>::halfPi;
        const float fadeOut  = std::cos (angle);
        const float fadeIn   = std::sin (angle);

        for (int ch = 0; ch < 2; ++ch)
            out[ch][n] = fromA[ch][n] * levelOut * fadeOut
                       + fromB[ch][n] * levelIn  * fadeIn;

        if (fadeRemaining > 0)
            --fadeRemaining;
    }

    if (fadeRemaining <= 0)
        currentSlot = targetSlot;
}

//
//  ReferencePlayer.cpp
//  AB Reference
//

#include "ReferencePlayer.h"

ReferencePlayer::~ReferencePlayer()
{
    releaseAllClips();
}

//==============================================================================
void ReferencePlayer::publish (ReferenceClip::Ptr newClip)
{
    auto* raw = newClip.get();

    // The raw pointer is about to own a count of its own, held on behalf of
    // whichever thread eventually takes it out of the slot.
    if (raw != nullptr)
        raw->incReferenceCount();

    auto* neverCollected = pending.exchange (raw, std::memory_order_acq_rel);

    // A non-null return means the audio thread never saw that clip: its own
    // exchange would have taken it and left null behind. So this thread still
    // owns it and is the one that has to drop it.
    if (neverCollected != nullptr)
        neverCollected->decReferenceCount();
}

void ReferencePlayer::collectRetiredClips()
{
    for (auto& slot : retired)
        if (auto* clip = slot.exchange (nullptr, std::memory_order_acquire))
            clip->decReferenceCount();
}

void ReferencePlayer::releaseAllClips()
{
    if (active != nullptr)
    {
        active->decReferenceCount();
        active = nullptr;
    }

    if (auto* clip = pending.exchange (nullptr, std::memory_order_acq_rel))
        clip->decReferenceCount();

    collectRetiredClips();
}

//==============================================================================
void ReferencePlayer::swapInPendingClip() noexcept
{
    // Fast path: no read-modify-write on the overwhelmingly common block where
    // nothing has changed.
    if (pending.load (std::memory_order_acquire) == nullptr)
        return;

    int slot = -1;

    if (active != nullptr)
    {
        for (int i = 0; i < numRetireSlots; ++i)
        {
            if (retired[(size_t) i].load (std::memory_order_relaxed) == nullptr)
            {
                slot = i;
                break;
            }
        }

        // Every slot full means the collector timer has not run since the last
        // eight swaps - possible only if somebody is loading references faster
        // than 3 Hz. Keep playing what we have and try again next block.
        if (slot < 0)
            return;
    }

    auto* incoming = pending.exchange (nullptr, std::memory_order_acq_rel);

    if (incoming == nullptr)
        return;

    if (active != nullptr)
    {
        // From here the message thread may free it, and this thread must not
        // look at it again. It does not: `active` is overwritten on the next
        // line and nothing else holds a copy.
        retired[(size_t) slot].store (active, std::memory_order_release);
    }

    active = incoming;
}

namespace
{
    /** Positive remainder, for positions before the timeline's zero. C's % keeps
        the sign of the dividend, so -1 % 100 is -1, and reading sample -1 of a
        buffer is exactly the bug this exists to not have. */
    int64_t wrapMod (int64_t value, int64_t modulus) noexcept
    {
        const int64_t remainder = value % modulus;
        return remainder < 0 ? remainder + modulus : remainder;
    }
}

//==============================================================================
ReferencePlayer::LoopRegion ReferencePlayer::resolveRegion (const LoopRegion& requested,
                                                            int length) noexcept
{
    LoopRegion resolved;
    resolved.enabled = requested.enabled;

    // No region, or a nonsensical one, is the whole file - which makes Loop with
    // nothing selected mean what it has always meant.
    if (length <= 0 || requested.end - requested.start < 1)
    {
        resolved.start = 0;
        resolved.end   = juce::jmax (0, length);
        return resolved;
    }

    // Clamped against the clip rather than trusted, because the region is stored
    // in seconds and survives a reference being swapped for a shorter one.
    resolved.start = juce::jlimit (0, length - 1, requested.start);
    resolved.end   = juce::jlimit (resolved.start + 1, length, requested.end);
    return resolved;
}

int64_t ReferencePlayer::localSampleFor (int64_t position, int length,
                                         double sampleRate, const LoopRegion& loop) noexcept
{
    if (! loop.enabled || length <= 0)
        return position;

    const auto region = resolveRegion (loop, length);
    const int regionLength = region.end - region.start;

    if (regionLength <= 0)
        return position;

    const int period = loopPeriodFor (regionLength, crossfadeSamplesFor (regionLength, sampleRate));

    if (period <= 0)
        return position;

    return region.start + wrapMod (position, (int64_t) period);
}

int ReferencePlayer::crossfadeSamplesFor (int regionLength, double sampleRate) noexcept
{
    if (regionLength <= 0 || sampleRate <= 0.0)
        return 0;

    const int wanted = (int) std::llround (loopCrossfadeSeconds * sampleRate);

    // A quarter of the region at most. Without that ceiling a one-second loop
    // would be fine and a 30 ms one would be two thirds crossfade, which is not
    // a loop any more - it is a tremolo.
    return juce::jlimit (0, regionLength / 4, wanted);
}

int ReferencePlayer::loopPeriodFor (int regionLength, int fade) noexcept
{
    return juce::jmax (0, regionLength - fade);
}

//==============================================================================
bool ReferencePlayer::read (juce::AudioBuffer<float>& destination,
                            int64_t startSample,
                            int numSamples,
                            const LoopRegion& loop) noexcept
{
    destination.clear (0, numSamples);

    if (active == nullptr)
        return false;

    const int length = active->getNumSamples();

    if (length <= 0)
        return false;

    const auto& source = active->audio;
    const int channels = juce::jmin (destination.getNumChannels(), source.getNumChannels());

    if (loop.enabled)
        readLooped (destination, source, channels, startSample, numSamples, length, loop);
    else
        readStraight (destination, source, channels, startSample, numSamples, length);

    return true;
}

void ReferencePlayer::readStraight (juce::AudioBuffer<float>& destination,
                                    const juce::AudioBuffer<float>& source,
                                    int channels,
                                    int64_t startSample,
                                    int numSamples,
                                    int length) noexcept
{
    int written = 0;
    int64_t position = startSample;

    // Walked in runs rather than per sample. The interesting part is not speed,
    // it is that the three cases - before the file starts, inside it, past its
    // end - are handled as spans, so a reference that begins ten seconds into
    // the timeline costs one silent memset rather than 480000 branches.
    while (written < numSamples)
    {
        const int64_t local = position;

        if (local < 0)
        {
            // Before the reference begins: leave the cleared silence in place
            // and jump to where it does begin, or to the end of this block.
            const int64_t gap = juce::jmin ((int64_t) (numSamples - written), -local);
            written  += (int) gap;
            position += gap;
            continue;
        }

        if (local >= (int64_t) length)
            break;   // past the end, not looping: the remainder stays silent

        const int run = (int) juce::jmin ((int64_t) (numSamples - written),
                                          (int64_t) length - local);

        for (int ch = 0; ch < channels; ++ch)
            destination.copyFrom (ch, written, source, ch, (int) local, run);

        written  += run;
        position += run;
    }
}

void ReferencePlayer::readLooped (juce::AudioBuffer<float>& destination,
                                  const juce::AudioBuffer<float>& source,
                                  int channels,
                                  int64_t startSample,
                                  int numSamples,
                                  int length,
                                  const LoopRegion& loop) noexcept
{
    const auto region = resolveRegion (loop, length);
    const int regionStart  = region.start;
    const int regionLength = region.end - region.start;

    if (regionLength <= 0)
        return;

    const int fade = crossfadeSamplesFor (regionLength, active->playbackSampleRate);
    const int period = loopPeriodFor (regionLength, fade);

    if (period <= 0)
        return;

    int written = 0;
    int64_t position = startSample;

    while (written < numSamples)
    {
        const int c = (int) wrapMod (position, (int64_t) period);
        const int remaining = numSamples - written;

        // The first time through, the region is played exactly as it was
        // selected - no blend on material that has nothing before it to blend
        // with. Every repeat after that crosses the seam. Both are decided from
        // the position alone, so this stays a pure function and a host that
        // jumps, scrubs or loops gets the same samples every time.
        const bool firstPass = position >= 0 && position < (int64_t) period;

        if (firstPass || c >= fade)
        {
            const int run = juce::jmin (remaining, period - c);

            for (int ch = 0; ch < channels; ++ch)
                destination.copyFrom (ch, written, source, ch, regionStart + c, run);

            written  += run;
            position += run;
            continue;
        }

        // The seam: the region's head, mixed with its own tail. At most `fade`
        // samples per repeat - 480 at 48 kHz - go through this slower path. See
        // the header for why the blend is shaped this way.
        const int run = juce::jmin (remaining, fade - c);

        for (int n = 0; n < run; ++n)
        {
            const int head = c + n;              // into the region's first `fade` samples
            const int tail = head + period;      // and its last `fade`, which end at the region's end

            const float t = ((float) head + 0.5f) / (float) fade;
            const float inGain  = std::sin (t * juce::MathConstants<float>::halfPi);
            const float outGain = std::cos (t * juce::MathConstants<float>::halfPi);

            for (int ch = 0; ch < channels; ++ch)
                destination.setSample (ch, written + n,
                                       source.getSample (ch, regionStart + head) * inGain
                                         + source.getSample (ch, regionStart + tail) * outGain);
        }

        written  += run;
        position += run;
    }
}

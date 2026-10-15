//
//  ReferenceClip.h
//  AB Reference
//
//  One loaded reference, ready to play: stereo, already at the session's sample
//  rate, and already measured.
//
//  Measuring the file once at load rather than live while it plays is not an
//  optimisation, it is the difference between a stable match and a wandering
//  one. The reference is a finished master; its integrated loudness is a
//  property of the file, a single number that does not change. Deriving the
//  level match from a live meter would mean matching against whichever four
//  seconds of the reference happen to be under the playhead, so the reference
//  would get louder in the verses and quieter in the choruses - and the
//  listener would hear that as the plugin doing something to the material.
//
//  This is a ReferenceCountedObject because ownership genuinely is shared for a
//  moment: during a swap the audio thread still holds the outgoing clip while
//  the message thread has already moved on. See ReferencePlayer for how that
//  moment is closed without either thread waiting for the other.
//

#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "BalanceAnalyser.h"
#include "LoudnessMeter.h"

class ReferenceClip : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<ReferenceClip>;

    ReferenceClip() = default;

    /** Always exactly two channels, always at playbackSampleRate. Everything
        downstream may rely on both of those without checking. */
    juce::AudioBuffer<float> audio;

    double playbackSampleRate = 0.0;   // the session rate this was resampled to
    double sourceSampleRate   = 0.0;   // what the file was, for the UI to report
    int    sourceChannels     = 0;     // before folding to stereo, for the UI to report

    juce::File   file;
    juce::String displayName;

    /** Measured over the whole file, after the fold to stereo and after
        resampling - so these are the numbers for the audio that will actually
        be played, not for the file as it sat on disk. */
    float integratedLufs = kLoudnessSilence;
    float truePeakDb     = -200.0f;

    /** The same file folded to mono, (L + R) / 2 measured as one channel - what
        a mono output plays. The level match on a mono bus is made against this
        one, not the stereo figure: folding loses up to 3 dB on a wide master
        and nothing on a narrow one, so one fixed correction would be right for
        neither. */
    float integratedLufsMono = kLoudnessSilence;

    /** A min/max envelope of the whole file, and an RMS envelope inside it,
        for the panel to draw without ever touching the audio buffer. Both
        channels go into the same arrays: at the size this is drawn a second lane
        would be half the height and would say nothing the combined envelope
        does not.

        The RMS is there because the peaks alone say almost nothing about a
        finished master. A limiter holds every peak within a dB of full scale,
        so the peak envelope of a loud record is a solid bar from the first bar
        to the last, and the quiet breakdown is exactly as tall as the drop.
        The RMS is what moves between sections.

        Fixed size rather than sized to the display, because it is built on the
        loader thread and the panel it feeds may not exist yet - and 2048 buckets
        is 24 KB, which is nothing next to the audio it summarises and still more
        resolution than the 600 pixel strip can show. */
    static constexpr int numWaveformBuckets = 2048;

    std::array<float, numWaveformBuckets> waveMin {};
    std::array<float, numWaveformBuckets> waveMax {};
    std::array<float, numWaveformBuckets> waveRms {};

    /** The reference's spectral balance: the 10-90 % corridor of its 3 s
        windows on the ERB scale, at equal loudness. What A is compared with
        in the Balance view; measured on the loader thread with the rest. */
    Balance::Corridor balance;

    int    getNumSamples()  const noexcept { return audio.getNumSamples(); }
    double getLengthSeconds() const noexcept
    {
        return playbackSampleRate > 0.0 ? (double) audio.getNumSamples() / playbackSampleRate : 0.0;
    }

    bool wasResampled() const noexcept
    {
        return std::abs (sourceSampleRate - playbackSampleRate) > 0.5;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceClip)
};

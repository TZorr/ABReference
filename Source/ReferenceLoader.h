//
//  ReferenceLoader.h
//  AB Reference
//
//  Everything that touches the disk, on a thread that is allowed to.
//
//  The rule this file exists to enforce is the blunt one from the brief: never
//  read the disk in processBlock. It is worth being precise about why, because
//  "it's slow" undersells it. A disk read is not slow on average, it is
//  *unbounded*: a spinning drive, a network volume, a file the OS decided to
//  page out, and the call that usually returns in microseconds blocks for a
//  second. The audio thread has about 2.7 ms at 128 samples and 48 kHz, and it
//  does not get an extension. So the read happens here, on a thread whose worst
//  case costs nothing but a spinner in the UI.
//
//  The same thread also resamples and measures, for the same reason and one
//  more: doing it offline means quality is free. Nothing is gained by using a
//  cheap interpolator on work that happens once.
//
//  The decoded file is kept at its *native* rate as well, which is what makes a
//  session sample-rate change cost a resample instead of a reload. It also
//  means the reference survives its file being moved, renamed, or living on a
//  drive that has since been unplugged.
//

#pragma once

#include <array>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "ParameterIds.h"

#include "ReferenceClip.h"

class ReferenceLoader : private juce::Thread,
                        private juce::AsyncUpdater
{
public:
    struct Listener
    {
        virtual ~Listener() = default;

        /** Message thread. clip is null if the load failed, in which case
            message says why in something a person can act on. */
        virtual void referenceLoadFinished (int slot,
                                            ReferenceClip::Ptr clip,
                                            const juce::File& file,
                                            const juce::String& message) = 0;

        /** Message thread. Fired when a job starts, so the UI can say so. */
        virtual void referenceLoadStarted (int slot, const juce::File& file) = 0;
    };

    explicit ReferenceLoader (Listener& listenerToUse);
    ~ReferenceLoader() override;

    /** Message thread. Starts a load into one slot; returns immediately. */
    void loadFile (int slot, const juce::File& file);

    /** Message thread. Tells the loader which rate clips must come out at.
        If the rate really changed, every occupied slot is rebuilt from audio
        already in RAM - no disk access. */
    void setPlaybackSampleRate (double newSampleRate);

    /** Message thread. Empties one slot and frees what it was holding. */
    void clear (int slot);

    /** A file filter string suitable for a FileChooser, covering every format
        the manager actually registered on this platform. */
    juce::String getFileFilter() const;

    bool isBusy() const noexcept { return busyCount.load (std::memory_order_relaxed) > 0; }

    /** Offline sample-rate conversion, including the compensation for the
        interpolator's own group delay. Public only so MeterCheck can assert
        that the compensation is right: a resampler that quietly delays the
        reference by a millisecond would be invisible in every other test, and
        would show up in use as an A/B that never quite lines up. */
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source,
                                             double sourceRate,
                                             double destinationRate);

private:
    void run() override;
    void handleAsyncUpdate() override;

    void startJob (int slot);

    struct Job
    {
        juce::File file;
        double     sampleRate = 0.0;
        bool       valid = false;
    };

    struct Outcome
    {
        int                slot = 0;
        ReferenceClip::Ptr clip;
        juce::File         file;
        juce::String       message;
        bool               valid = false;
    };

    /** The decoded file at its own rate. Loader thread only. */
    struct DecodedSource
    {
        juce::File               file;
        juce::AudioBuffer<float> audio;      // already folded to stereo
        double                   sampleRate = 0.0;
        int                      originalChannels = 0;
    };

    bool decode (const juce::File& file, DecodedSource& destination,
                 int64_t otherSlotsBytes, juce::String& error);

    /** Decoded audio currently held for every slot but this one. The budget is
        shared, so what one slot may take depends on what the others already
        have. */
    int64_t bytesHeldExcluding (int slot) const;
    static void foldToStereo (juce::AudioBuffer<float>& buffer, int sourceChannels);
    static void measure (ReferenceClip& clip);

    /** Builds the clip's min/max envelope. Separate from measure() because it
        answers a different question - measure() is the numbers the level match
        is derived from, this is a picture - and because it is the one of the
        two that could be dropped without the plugin becoming wrong. */
    static void summarise (ReferenceClip& clip);

    Listener& listener;

    juce::AudioFormatManager formatManager;

    juce::CriticalSection jobLock;

    // One pending job per slot rather than one in total. Dropping three files
    // at once has to queue three loads; with a single slot the later ones would
    // simply replace the earlier and two files would silently never arrive.
    // Repeated loads into the same slot still coalesce, which is what the
    // single-slot version did and is still the right behaviour.
    std::array<Job, ABParams::numSlots> pendingJobs;      // guarded by jobLock
    std::array<Outcome, ABParams::numSlots> finished;     // guarded by jobLock

    juce::WaitableEvent jobAvailable;
    std::atomic<int> busyCount { 0 };

    /** Per slot, and only ever populated when resampling actually happened.
        When the file's rate already matches the session's, the clip's own
        buffer *is* the native audio, and keeping a second identical copy here
        would cost a few hundred megabytes across three slots to store nothing
        new. On a later rate change the loader resamples from whichever of the
        two it has. Loader thread only. */
    std::array<std::unique_ptr<DecodedSource>, ABParams::numSlots> cachedSources;

    /** What the loader last handed out for each slot, kept so a sample-rate
        change can resample from it when there is no native cache. Loader
        thread only. */
    std::array<ReferenceClip::Ptr, ABParams::numSlots> lastClips;

    double currentPlaybackRate = 0.0;                       // message thread only
    std::array<juce::File, ABParams::numSlots> currentFiles; // message thread only

    // A 30 minute reference is already absurd, and that ceiling is per file.
    // The memory ceiling is not: three slots share one budget, because what
    // matters to the machine is the total resident audio, not how it is divided.
    // A load that would exceed it is refused with a message naming what is
    // already occupying the space, rather than being quietly truncated.
    static constexpr double maxLengthSeconds = 30.0 * 60.0;
    static constexpr int64_t maxTotalDecodedBytes = 768ll * 1024ll * 1024ll;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceLoader)
};

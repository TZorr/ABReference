//
//  ReferenceLoader.cpp
//  AB Reference
//

#include "ReferenceLoader.h"

#include "TruePeakMeter.h"

ReferenceLoader::ReferenceLoader (Listener& listenerToUse)
    : juce::Thread ("AB Reference reference loader"),
      listener (listenerToUse)
{
    // registerBasicFormats() gives WAV, AIFF and FLAC, and on macOS it also
    // registers CoreAudioFormat, which is what quietly brings MP3, M4A and AAC
    // along. That matters more than it sounds: references arrive as whatever
    // the person who sent them had, and refusing an M4A means the comparison
    // does not happen.
    formatManager.registerBasicFormats();

    startThread (juce::Thread::Priority::low);
}

ReferenceLoader::~ReferenceLoader()
{
    signalThreadShouldExit();
    jobAvailable.signal();
    stopThread (4000);

    // Cancel before the listener reference goes out of scope with us.
    cancelPendingUpdate();
}

//==============================================================================
juce::String ReferenceLoader::getFileFilter() const
{
    return formatManager.getWildcardForAllFormats();
}

void ReferenceLoader::loadFile (int slot, const juce::File& file)
{
    slot = juce::jlimit (0, ABParams::numSlots - 1, slot);
    currentFiles[(size_t) slot] = file;
    startJob (slot);
    listener.referenceLoadStarted (slot, file);
}

void ReferenceLoader::setPlaybackSampleRate (double newSampleRate)
{
    if (newSampleRate <= 0.0 || std::abs (newSampleRate - currentPlaybackRate) < 0.5)
        return;

    currentPlaybackRate = newSampleRate;

    // Every occupied slot has to be rebuilt at the new rate. Empty ones are
    // just a note for next time.
    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        if (currentFiles[(size_t) slot] == juce::File())
            continue;

        startJob (slot);
        listener.referenceLoadStarted (slot, currentFiles[(size_t) slot]);
    }
}

void ReferenceLoader::clear (int slot)
{
    slot = juce::jlimit (0, ABParams::numSlots - 1, slot);
    currentFiles[(size_t) slot] = juce::File();

    // The cached decode and the last clip belong to the loader thread, so they
    // are not freed here - reaching across to free them is exactly the kind of
    // cross-thread ownership this class exists to avoid. A job with no file is
    // posted instead, and the thread that owns them lets go when it reads it.
    {
        const juce::ScopedLock lock (jobLock);
        pendingJobs[(size_t) slot] = {};                       // drops any queued load
        pendingJobs[(size_t) slot].valid      = true;
        pendingJobs[(size_t) slot].sampleRate = currentPlaybackRate;
    }

    busyCount.fetch_add (1, std::memory_order_relaxed);
    jobAvailable.signal();

    // The audio thread hears about this immediately rather than waiting for the
    // loader: clearing a slot should silence it now, not whenever the worker
    // next wakes.
    listener.referenceLoadFinished (slot, nullptr, juce::File(), {});
}

void ReferenceLoader::startJob (int slot)
{
    if (currentPlaybackRate <= 0.0 || currentFiles[(size_t) slot] == juce::File())
        return;

    {
        const juce::ScopedLock lock (jobLock);
        pendingJobs[(size_t) slot].file       = currentFiles[(size_t) slot];
        pendingJobs[(size_t) slot].sampleRate = currentPlaybackRate;
        pendingJobs[(size_t) slot].valid      = true;
    }

    busyCount.fetch_add (1, std::memory_order_relaxed);
    jobAvailable.signal();
}

//==============================================================================
int64_t ReferenceLoader::bytesHeldExcluding (int slot) const
{
    int64_t total = 0;

    for (int i = 0; i < ABParams::numSlots; ++i)
    {
        if (i == slot)
            continue;

        if (const auto& src = cachedSources[(size_t) i])
            total += (int64_t) src->audio.getNumSamples() * src->audio.getNumChannels() * (int64_t) sizeof (float);

        if (const auto& clip = lastClips[(size_t) i])
            total += (int64_t) clip->audio.getNumSamples() * clip->audio.getNumChannels() * (int64_t) sizeof (float);
    }

    return total;
}

//==============================================================================
void ReferenceLoader::run()
{
    while (! threadShouldExit())
    {
        jobAvailable.wait (-1);

        if (threadShouldExit())
            return;

        for (int slot = 0; slot < ABParams::numSlots; ++slot)
        {
            if (threadShouldExit())
                return;

            Job job;

            {
                const juce::ScopedLock lock (jobLock);
                job = pendingJobs[(size_t) slot];
                pendingJobs[(size_t) slot].valid = false;
            }

            if (! job.valid)
                continue;

            busyCount.fetch_sub (1, std::memory_order_relaxed);

            // An empty file is clear(): let go of this slot's audio here, on
            // the thread that owns it.
            if (job.file == juce::File())
            {
                cachedSources[(size_t) slot].reset();
                lastClips[(size_t) slot] = nullptr;
                continue;
            }

            Outcome outcome;
            outcome.slot  = slot;
            outcome.file  = job.file;
            outcome.valid = true;

            // What this slot can already resample from without touching the
            // disk: the native decode when the file needed resampling, and
            // otherwise the last clip, whose buffer *is* the native audio.
            const bool haveNative = cachedSources[(size_t) slot] != nullptr
                                      && cachedSources[(size_t) slot]->file == job.file;
            const bool haveClip   = lastClips[(size_t) slot] != nullptr
                                      && lastClips[(size_t) slot]->file == job.file;

            const juce::AudioBuffer<float>* sourceAudio = nullptr;
            double sourceRate     = 0.0;   // the rate sourceAudio actually sits at
            double fileRate       = 0.0;   // what the file on disk was, for the panel to report
            int    sourceChannels = 0;

            if (haveNative)
            {
                sourceAudio    = &cachedSources[(size_t) slot]->audio;
                sourceRate     = cachedSources[(size_t) slot]->sampleRate;
                fileRate       = sourceRate;
                sourceChannels = cachedSources[(size_t) slot]->originalChannels;
            }
            else if (haveClip)
            {
                // No native cache means the previous load did not resample, so
                // that clip's buffer is the file's own audio at the file's own
                // rate. Its recorded sourceSampleRate is still the truth about
                // the file, and that is what the panel should keep showing.
                sourceAudio    = &lastClips[(size_t) slot]->audio;
                sourceRate     = lastClips[(size_t) slot]->playbackSampleRate;
                fileRate       = lastClips[(size_t) slot]->sourceSampleRate;
                sourceChannels = lastClips[(size_t) slot]->sourceChannels;
            }
            else
            {
                auto decoded = std::make_unique<DecodedSource>();
                juce::String error;

                if (! decode (job.file, *decoded, bytesHeldExcluding (slot), error))
                {
                    outcome.message = error;

                    {
                        const juce::ScopedLock lock (jobLock);
                        finished[(size_t) slot] = std::move (outcome);
                    }

                    triggerAsyncUpdate();
                    continue;
                }

                cachedSources[(size_t) slot] = std::move (decoded);
                sourceAudio    = &cachedSources[(size_t) slot]->audio;
                sourceRate     = cachedSources[(size_t) slot]->sampleRate;
                fileRate       = sourceRate;
                sourceChannels = cachedSources[(size_t) slot]->originalChannels;
            }

            if (threadShouldExit())
                return;

            const bool needsResampling = std::abs (sourceRate - job.sampleRate) >= 0.5;

            auto clip = new ReferenceClip();
            clip->file               = job.file;
            clip->displayName        = job.file.getFileName();
            clip->sourceChannels     = sourceChannels;
            clip->playbackSampleRate = job.sampleRate;
            clip->sourceSampleRate   = fileRate;

            if (needsResampling)
                clip->audio = resample (*sourceAudio, sourceRate, job.sampleRate);
            else
                clip->audio.makeCopyOf (*sourceAudio);

            // The native copy is worth keeping only when it is genuinely a
            // different set of samples from the clip. When the rates match, the
            // clip's own buffer is the native audio, and a second identical
            // copy would cost a few hundred megabytes across three slots to
            // store nothing new.
            if (! needsResampling)
                cachedSources[(size_t) slot].reset();

            measure (*clip);
            summarise (*clip);

            outcome.clip = clip;
            lastClips[(size_t) slot] = clip;

            {
                const juce::ScopedLock lock (jobLock);
                finished[(size_t) slot] = std::move (outcome);
            }

            triggerAsyncUpdate();
        }
    }
}

void ReferenceLoader::handleAsyncUpdate()
{
    // Every slot is drained on each wake-up: three files dropped at once finish
    // at three different moments, and one async update may well cover more than
    // one of them.
    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        Outcome outcome;

        {
            const juce::ScopedLock lock (jobLock);

            if (! finished[(size_t) slot].valid)
                continue;

            outcome = std::move (finished[(size_t) slot]);
            finished[(size_t) slot] = {};
        }

        listener.referenceLoadFinished (outcome.slot, outcome.clip, outcome.file, outcome.message);
    }
}

//==============================================================================
bool ReferenceLoader::decode (const juce::File& file, DecodedSource& destination,
                              int64_t otherSlotsBytes, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unreadable format: " + file.getFileName();
        return false;
    }

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "File contains no audio: " + file.getFileName();
        return false;
    }

    const double lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;

    if (lengthSeconds > maxLengthSeconds)
    {
        error = juce::String ("Reference too long (")
              + juce::String (lengthSeconds / 60.0, 1) + " min, limit 30 min)";
        return false;
    }

    const int channelsToKeep = juce::jmin (2, (int) reader->numChannels);
    const int64_t bytes = reader->lengthInSamples * channelsToKeep * (int64_t) sizeof (float);

    // The budget is shared across the slots, so what this file may take
    // depends on what the other two are already holding. Saying so is the
    // difference between a message you can act on - free a slot - and one that
    // looks like the file is simply too big.
    const auto mb = [] (int64_t b) { return juce::String ((double) b / (1024.0 * 1024.0), 0); };

    if (bytes + otherSlotsBytes > maxTotalDecodedBytes)
    {
        error = "Not enough room for " + mb (bytes) + " MB: the other slots hold "
                  + mb (otherSlotsBytes) + " MB of the " + mb (maxTotalDecodedBytes)
                  + " MB budget. Clear a slot.";
        return false;
    }

    destination.file             = file;
    destination.sampleRate       = reader->sampleRate;
    destination.originalChannels = (int) reader->numChannels;

    // Read into as many channels as the file has, up to two: reading a 5.1 file
    // into two channels via AudioFormatReader would give L and R only, which is
    // the same thing foldToStereo does deliberately below, but going through
    // foldToStereo keeps the mono case honest.
    destination.audio.setSize (juce::jmax (1, channelsToKeep), (int) reader->lengthInSamples);
    destination.audio.clear();

    if (! reader->read (&destination.audio, 0, (int) reader->lengthInSamples, 0, true, true))
    {
        error = "Could not read file: " + file.getFileName();
        return false;
    }

    foldToStereo (destination.audio, (int) reader->numChannels);
    return true;
}

void ReferenceLoader::foldToStereo (juce::AudioBuffer<float>& buffer, int sourceChannels)
{
    // The playback path is stereo, always. A mono file is duplicated rather
    // than panned or attenuated: a mono reference should sit dead centre at the
    // level it was made at, and halving it to "conserve energy" would make the
    // level match fight a 3 dB error that is not really there.
    if (buffer.getNumChannels() == 1)
    {
        buffer.setSize (2, buffer.getNumSamples(), true, true, true);
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }

    // More than two channels keeps the first two. Downmixing a surround file
    // with assumed centre and LFE coefficients would invent a stereo master
    // that nobody signed off on; the first pair at least is a real decision
    // somebody made.
    juce::ignoreUnused (sourceChannels);

    if (buffer.getNumChannels() > 2)
        buffer.setSize (2, buffer.getNumSamples(), true, true, true);
}

namespace
{
    /** The steady-state gain of a WindowedSincInterpolator run at this ratio.
        It is not 1.0: JUCE's kernel table comes out about 0.087 dB low, flat
        across frequency and across every rate pair MeterCheck sweeps.

        That number is deliberately not written down here. It belongs to JUCE's
        interpolator, not to this project, and a future version of JUCE that
        normalised its table would turn a pasted constant from a correction into
        an error of the same size in the other direction. So it is measured: a
        constant 1.0 going in has to come out as a constant 1.0, and whatever
        actually comes out is the number to divide by.

        Left uncorrected this is small enough to argue about - 0.087 dB - and it
        would not touch the level match at all, because the reference is measured
        after resampling and the match would absorb it. What it would spoil is
        the readout: a 44.1 kHz reference in a 48 kHz session would report a
        loudness 0.09 LU below what every other meter says about the same file,
        and a tool whose job is comparing numbers should not invent a discrepancy
        of its own. */
    double measureInterpolatorGain (double ratio)
    {
        constexpr int probeSamples = 8192;

        juce::AudioBuffer<float> unity (1, probeSamples);
        unity.clear();
        juce::FloatVectorOperations::fill (unity.getWritePointer (0), 1.0f, probeSamples);

        // Produce only half of what the input could feed, so the reading is
        // taken well clear of both the filter's ramp-in and the point where it
        // starts running out of input and zero-filling.
        const int produce = juce::jmax (256, (int) ((double) probeSamples / ratio) / 2);

        juce::AudioBuffer<float> output (1, produce);
        output.clear();

        juce::WindowedSincInterpolator interpolator;
        interpolator.reset();
        interpolator.process (ratio, unity.getReadPointer (0), output.getWritePointer (0),
                              produce, probeSamples, 0);

        const double gain = (double) output.getSample (0, produce / 2);

        // If this ever comes back implausible, something about the interpolator
        // has changed enough that a correction derived from it cannot be
        // trusted. Applying no correction is the safe failure.
        return (gain > 0.5 && gain < 2.0) ? gain : 1.0;
    }
}

juce::AudioBuffer<float> ReferenceLoader::resample (const juce::AudioBuffer<float>& source,
                                                    double sourceRate,
                                                    double destinationRate)
{
    const double ratio = sourceRate / destinationRate;          // input samples per output sample
    const int sourceLength = source.getNumSamples();
    const int outputLength = (int) std::llround ((double) sourceLength / ratio);

    // A windowed-sinc interpolator rather than Lagrange, because this runs once
    // on a background thread and the quality is therefore free. It does have a
    // real group delay - 100 input samples, which is 100 / ratio output samples
    // - and that delay would show up as the reference sitting a millisecond or
    // two late against the master. So the extra samples are produced and then
    // dropped, which puts output sample 0 back on input sample 0.
    const int latency = (int) std::llround (juce::WindowedSincInterpolator::getBaseLatency() / ratio);

    juce::AudioBuffer<float> destination (2, juce::jmax (1, outputLength));
    destination.clear();

    juce::AudioBuffer<float> scratch (1, juce::jmax (1, outputLength + latency));

    for (int ch = 0; ch < juce::jmin (2, source.getNumChannels()); ++ch)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.reset();

        scratch.clear();

        interpolator.process (ratio,
                              source.getReadPointer (ch),
                              scratch.getWritePointer (0),
                              outputLength + latency,
                              sourceLength,
                              0);

        destination.copyFrom (ch, 0, scratch, 0, latency, outputLength);
    }

    destination.applyGain ((float) (1.0 / measureInterpolatorGain (ratio)));

    return destination;
}

void ReferenceLoader::measure (ReferenceClip& clip)
{
    LoudnessMeter loudness;
    loudness.prepare (clip.playbackSampleRate, clip.audio.getNumChannels());
    loudness.processBuffer (clip.audio);
    clip.integratedLufs = loudness.getIntegratedLufs();

    TruePeakMeter truePeak;
    truePeak.prepare (clip.audio.getNumChannels(), 8192);
    truePeak.processBuffer (clip.audio);
    clip.truePeakDb = truePeak.getMaxTruePeakDb();
}

void ReferenceLoader::summarise (ReferenceClip& clip)
{
    // A third pass over the same buffer, on the same thread, for the same
    // reason as the first two: the panel must never walk the audio itself.
    // Peaks and RMS both: the peaks are the outline, the RMS is the body. On
    // an unmastered file the peaks alone would show the sections, but a
    // reference is a finished master, and after a limiter the peak envelope is
    // a flat bar while the RMS still rises and falls with the arrangement.
    const int length = clip.audio.getNumSamples();
    const int channels = clip.audio.getNumChannels();

    clip.waveMin.fill (0.0f);
    clip.waveMax.fill (0.0f);
    clip.waveRms.fill (0.0f);

    if (length <= 0 || channels <= 0)
        return;

    for (int bucket = 0; bucket < ReferenceClip::numWaveformBuckets; ++bucket)
    {
        // 64-bit throughout: at 30 minutes and 96 kHz the product of a bucket
        // index and the length overflows a 32-bit int well before the limit.
        const int64_t first = (int64_t) bucket * length / ReferenceClip::numWaveformBuckets;
        const int64_t last  = (int64_t) (bucket + 1) * length / ReferenceClip::numWaveformBuckets;

        // A file shorter than the bucket count leaves buckets with no samples of
        // their own. They take the one sample they sit on rather than staying
        // flat, so a very short reference draws as a shape instead of a line.
        const int start = (int) first;
        const int count = (int) juce::jmax ((int64_t) 1, last - first);

        const int available = juce::jmin (count, length - start);

        float low = 0.0f, high = 0.0f;
        double sumOfSquares = 0.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* samples = clip.audio.getReadPointer (ch, start);
            const auto range = juce::FloatVectorOperations::findMinAndMax (samples, available);

            low  = juce::jmin (low,  range.getStart());
            high = juce::jmax (high, range.getEnd());

            for (int i = 0; i < available; ++i)
                sumOfSquares += (double) samples[i] * (double) samples[i];
        }

        clip.waveMin[(size_t) bucket] = low;
        clip.waveMax[(size_t) bucket] = high;
        clip.waveRms[(size_t) bucket] = (float) std::sqrt (sumOfSquares / (double) (available * channels));
    }
}

//
//  PluginProcessor.cpp
//  AB Reference
//

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ABReferenceProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ABParams::ab, ABParams::version }, "A / B", false,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool b, int) { return b ? String ("B") : String ("A"); })));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ABParams::levelMatch, ABParams::version }, "Level Match", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ABParams::trimDb, ABParams::version }, "Reference Trim",
        NormalisableRange<float> (ABParams::trimMinDb, ABParams::trimMaxDb, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ABParams::offsetMs, ABParams::version }, "Reference Offset",
        NormalisableRange<float> (ABParams::offsetMinMs, ABParams::offsetMaxMs, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ABParams::monoSum, ABParams::version }, "Mono", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ABParams::loop, ABParams::version }, "Loop Reference", false));

    // The loop region. Not automatable: automating which part of a reference
    // repeats is not a musical gesture, and leaving them out of the host's
    // automation list keeps that list about the things worth automating. They
    // are still parameters rather than state-tree properties because the audio
    // thread reads them every block, and APVTS already provides exactly the
    // lock-free atomic that needs.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ABParams::refSlot, ABParams::version }, "Reference",
        StringArray { "1", "2", "3" }, 0));

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        const String n (slot + 1);

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ABParams::loopStartSecFor (slot), ABParams::version }, "Loop Start " + n,
            NormalisableRange<float> (0.0f, ABParams::loopMaxSeconds, 0.001f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("s").withAutomatable (false)));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ABParams::loopEndSecFor (slot), ABParams::version }, "Loop End " + n,
            NormalisableRange<float> (0.0f, ABParams::loopMaxSeconds, 0.001f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("s").withAutomatable (false)));
    }

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ABParams::bypass, ABParams::version }, "Bypass", false));

    return layout;
}

//==============================================================================
ABReferenceProcessor::ABReferenceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, juce::Identifier (ABParams::stateTreeType), createParameterLayout())
{
    abParam         = apvts.getRawParameterValue (ABParams::ab);
    levelMatchParam = apvts.getRawParameterValue (ABParams::levelMatch);
    trimParam       = apvts.getRawParameterValue (ABParams::trimDb);
    offsetParam     = apvts.getRawParameterValue (ABParams::offsetMs);
    monoParam       = apvts.getRawParameterValue (ABParams::monoSum);
    loopParam       = apvts.getRawParameterValue (ABParams::loop);
    slotParam       = apvts.getRawParameterValue (ABParams::refSlot);

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        loopStartParams[(size_t) slot] = apvts.getRawParameterValue (ABParams::loopStartSecFor (slot));
        loopEndParams[(size_t) slot]   = apvts.getRawParameterValue (ABParams::loopEndSecFor (slot));
    }

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ABParams::bypass));
    jassert (bypassParam != nullptr);

    // The only call to this in the project. See the header for why.
    setLatencySamples (0);

    // Sweeps ReferencePlayer's retire slots. Four times a second is far more
    // often than eight slots can be filled by a human loading files.
    startTimer (250);
}

ABReferenceProcessor::~ABReferenceProcessor()
{
    stopTimer();
    cancelPendingUpdate();

    // The host has stopped calling processBlock by the time a processor is
    // destroyed, so this is the one place it is legal to free what the audio
    // thread was holding.
    bus.releaseAllClips();
}

//==============================================================================
void ABReferenceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = juce::jmax (1, samplesPerBlock);

    referenceScratch.setSize (2, currentBlockSize, false, true, true);
    referenceScratch.clear();

    bus.prepare (sampleRate, currentBlockSize);
    bus.reset ((int) *slotParam);

    meterA.prepare (sampleRate, 2);
    meterB.prepare (sampleRate, 2);
    peakA.prepare (2, currentBlockSize);

    engine.prepare (sampleRate);
    engine.reset (*abParam > 0.5f);

    freeRunPosition = 0;
    heldMatchDb.fill (0.0f);

    // The loader is a message-thread object and prepareToPlay is not promised
    // to arrive on the message thread, so this is a note, not a call.
    sampleRateForLoader.store (sampleRate, std::memory_order_relaxed);
    triggerAsyncUpdate();
}

void ABReferenceProcessor::releaseResources()
{
    // Legal here for the same reason as in the destructor: the host has
    // guaranteed processBlock is not running and will not start again until
    // prepareToPlay.
    bus.releaseAllClips();
}

bool ABReferenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo only, in and out. A mastering reference is a stereo object and a
    // mono version of this tool would be answering a question nobody asked.
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void ABReferenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, bypassParam != nullptr && bypassParam->get());
}

void ABReferenceProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Hosts reach bypass by two different routes - the bypass parameter, and
    // this call - and both have to behave identically, or the plugin sounds
    // different depending on which button the host happened to wire up.
    process (buffer, true);
}

void ABReferenceProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    bus.swapInPendingClips();

    if (numSamples <= 0)
        return;

    // ---- A is metered before anything is done to the buffer, always, bypassed
    // or not. A meter that blanks out when the plugin is bypassed is a meter
    // you cannot use to decide whether to un-bypass it.
    meterA.processBlock (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), numSamples);
    peakA.processBlock  (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), numSamples);

    // A block larger than prepareToPlay promised should not be possible, and
    // every host tested honours that. But "should not be possible" is not a
    // basis for reading past the end of the reference buffer, so the case has a
    // defined answer instead of an undefined one: play A, untouched. It is the
    // same thing bypass does, and it cannot crash.
    if (numSamples > referenceScratch.getNumSamples())
    {
        jassertfalse;
        return;
    }

    // ---- Where are we on the host's timeline?
    int64_t timelineSamples = 0;
    bool playing = false;
    bool haveTransport = false;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            haveTransport = true;
            playing = position->getIsPlaying();

            if (const auto samples = position->getTimeInSamples())
                timelineSamples = *samples;
            else if (const auto seconds = position->getTimeInSeconds())
                timelineSamples = (int64_t) std::llround (*seconds * currentSampleRate);
        }
    }

    if (! haveTransport)
    {
        // Standalone, and any host that declines to say. Free-running keeps the
        // development build usable; in a DAW this branch is never taken.
        timelineSamples = freeRunPosition;
        playing = true;
    }

    freeRunPosition += numSamples;
    transportRunning.store (playing, std::memory_order_relaxed);

    // ---- What should the reference be doing?
    const bool wantB     = *abParam > 0.5f;
    const bool wantMatch = *levelMatchParam > 0.5f;
    const bool wantLoop  = *loopParam > 0.5f;
    const float trimDb   = trimParam->load();
    const float offsetMs = offsetParam->load();
    const int  slot      = juce::jlimit (0, ABParams::numSlots - 1, (int) *slotParam);

    const int64_t offsetSamples = (int64_t) std::llround ((double) offsetMs * 0.001 * currentSampleRate);
    const int64_t referenceStart = timelineSamples + offsetSamples;

    // Each slot's own region, resolved against whatever that slot holds. A
    // region drawn on one reference means nothing in another, which is why
    // there are three of them rather than one.
    std::array<ReferencePlayer::LoopRegion, ABParams::numSlots> regions {};

    for (int i = 0; i < ABParams::numSlots; ++i)
    {
        auto& region = regions[(size_t) i];
        region.enabled = wantLoop;

        const float startSec = loopStartParams[(size_t) i]->load();
        const float endSec   = loopEndParams[(size_t) i]->load();

        if (ABParams::hasLoopRegion (startSec, endSec))
        {
            region.start = (int) std::llround ((double) startSec * currentSampleRate);
            region.end   = (int) std::llround ((double) endSec   * currentSampleRate);
        }
    }

    bus.setSlot (slot);

    // ---- Level match, per slot. Every reference has its own loudness, so
    // every slot has its own match; the bus applies each one to its own audio
    // before the slots are mixed. A single gain would be right for at most one
    // of the two slots involved in a switch.
    const auto* clip = bus.getActiveClip (slot);

    float appliedMatchDb = 0.0f;
    bool  clamped = false;

    for (int i = 0; i < ABParams::numSlots; ++i)
    {
        float matchForSlot = 0.0f;

        if (wantMatch)
        {
            const auto* slotClip = bus.getActiveClip (i);
            const float aLufs = meterA.getIntegratedLufs();
            const float bLufs = slotClip != nullptr ? slotClip->integratedLufs : kLoudnessSilence;

            // Three seconds of gated material before the match is allowed to
            // move. Computed from less, it lurches every time the arrangement
            // changes, and a level that moves while you are listening is worse
            // than a level that is slightly wrong.
            if (meterA.hasIntegratedFor (3.0) && aLufs > kLoudnessSilence && bLufs > kLoudnessSilence)
            {
                const float wanted = aLufs - bLufs;
                heldMatchDb[(size_t) i] = juce::jlimit (-ABParams::matchLimitDb,
                                                        ABParams::matchLimitDb, wanted);

                if (i == slot)
                {
                    desiredMatchDb.store (wanted, std::memory_order_relaxed);
                    clamped = std::abs (wanted) > ABParams::matchLimitDb;
                }
            }

            matchForSlot = heldMatchDb[(size_t) i];
        }

        if (i == slot)
            appliedMatchDb = matchForSlot;

        // Trim is one control for the panel, so it applies to whichever slot
        // you are listening to - and to the others too, so that switching does
        // not step the level by however much trim was dialled in.
        bus.setSlotGain (i, juce::Decibels::decibelsToGain (matchForSlot + trimDb), wantB && ! bypassed);
    }

    const float totalDb = appliedMatchDb + trimDb;

    matchDb.store      (appliedMatchDb, std::memory_order_relaxed);
    totalRefDb.store   (totalDb,        std::memory_order_relaxed);
    matchClamped.store (clamped,        std::memory_order_relaxed);

    const bool referenceShouldPlay = playing && ! bypassed;

    if (referenceShouldPlay)
        bus.read (referenceScratch, referenceStart, numSamples, regions);
    else
        referenceScratch.clear (0, numSamples);

    // The position the panel draws its playhead at is the sample actually being
    // read, not the raw timeline position - inside a loop region those are two
    // different things, and the one worth showing is where in the file you are.
    const int64_t localSample = ReferencePlayer::localSampleFor (referenceStart,
                                                                 clip != nullptr ? clip->getNumSamples() : 0,
                                                                 currentSampleRate,
                                                                 regions[(size_t) slot]);

    refPosition.store (currentSampleRate > 0.0 ? (float) ((double) localSample / currentSampleRate) : 0.0f,
                       std::memory_order_relaxed);

    // ---- Metering B on the bus output, which already carries each slot's own
    // match gain. Nothing to add afterwards, and it stays correct through a
    // crossfade, where two different gains are in play at once.
    meterB.processBlock (referenceScratch.getArrayOfReadPointers(),
                         referenceScratch.getNumChannels(),
                         numSamples);

    engine.setMonoAmount (*monoParam > 0.5f ? 1.0f : 0.0f);

    // Bypass is a fade to A rather than a jump to A. It reaches bit-identical
    // passthrough 8 ms later instead of immediately, and it gets there without
    // a step discontinuity - which is the trade every bypass switch worth using
    // makes.
    engine.setTarget (wantB && ! bypassed);
    engine.process (buffer, referenceScratch, numSamples);

    abPosition.store (engine.getPosition(), std::memory_order_relaxed);
}

//==============================================================================
float ABReferenceProcessor::getBTruePeakDb() const noexcept
{
    const auto clip = getSelectedClip();

    if (clip == nullptr || clip->truePeakDb <= -200.0f)
        return -200.0f;

    return clip->truePeakDb + getTotalRefDb();
}

void ABReferenceProcessor::resetMeters()
{
    // Requests, not resets. This runs on the message thread and the meters are
    // being read - and in the loudness meter's case, walked over a thousand
    // histogram buckets - by the audio thread at the same moment. The audio
    // thread does the clearing itself on its next block.
    meterA.requestReset();
    meterB.requestReset();
    peakA.requestReset();
}

//==============================================================================
int ABReferenceProcessor::getSelectedSlot() const
{
    if (auto* p = apvts.getRawParameterValue (ABParams::refSlot))
        return juce::jlimit (0, ABParams::numSlots - 1, (int) p->load());

    return 0;
}

void ABReferenceProcessor::setSelectedSlot (int slot, bool alsoSwitchToB)
{
    slot = juce::jlimit (0, ABParams::numSlots - 1, slot);

    if (auto* p = apvts.getParameter (ABParams::refSlot))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) slot));
        p->endChangeGesture();
    }

    // Choosing a reference is already the statement that you want to hear it,
    // so the switch follows the choice rather than waiting for a second click.
    // A is one keystroke back.
    if (alsoSwitchToB)
    {
        if (auto* p = apvts.getParameter (ABParams::ab))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (1.0f);
            p->endChangeGesture();
        }
    }
}

ReferenceClip::Ptr ABReferenceProcessor::getUiClip (int slot) const
{
    return uiClips[(size_t) juce::jlimit (0, ABParams::numSlots - 1, slot)];
}

int ABReferenceProcessor::getSlotForUntargetedDrop() const
{
    for (int slot = 0; slot < ABParams::numSlots; ++slot)
        if (uiClips[(size_t) slot] == nullptr)
            return slot;

    // All three occupied: replace what is in front of you rather than picking
    // one of the other two for the user.
    return getSelectedSlot();
}

//==============================================================================
void ABReferenceProcessor::loadReference (int slot, const juce::File& file)
{
    slot = juce::jlimit (0, ABParams::numSlots - 1, slot);
    referencePaths[(size_t) slot] = file.getFullPathName();
    loader.loadFile (slot, file);
}

void ABReferenceProcessor::clearReference (int slot)
{
    slot = juce::jlimit (0, ABParams::numSlots - 1, slot);
    referencePaths[(size_t) slot].clear();
    loader.clear (slot);
}

void ABReferenceProcessor::referenceLoadStarted (int, const juce::File& file)
{
    loading = true;
    statusMessage = "Loading " + file.getFileName() + " ...";
    updateHostDisplay();
}

void ABReferenceProcessor::referenceLoadFinished (int slot,
                                                  ReferenceClip::Ptr clip,
                                                  const juce::File& file,
                                                  const juce::String& message)
{
    slot = juce::jlimit (0, ABParams::numSlots - 1, slot);

    loading = loader.isBusy();
    uiClips[(size_t) slot] = clip;

    if (clip == nullptr && file != juce::File())
    {
        // A failed restore keeps the path so the UI can offer to relocate it.
        // Logic hosts audio units in a sandboxed process, so a path that was
        // readable when the session was saved may simply not be reachable now,
        // and silently forgetting it would look like the plugin lost the
        // setting rather than the sandbox refusing the read.
        loadFailed = true;
        statusMessage = message.isNotEmpty() ? message : "Reference could not be loaded";
    }
    else
    {
        loadFailed = false;
        statusMessage = message;
    }

    // A reference that has just gone away must not leave a loop region behind
    // pointing into a file that is no longer there.
    if (clip == nullptr)
    {
        if (auto* start = apvts.getParameter (ABParams::loopStartSecFor (slot)))
            start->setValueNotifyingHost (0.0f);

        if (auto* end = apvts.getParameter (ABParams::loopEndSecFor (slot)))
            end->setValueNotifyingHost (0.0f);
    }

    // Publishing null is how "clear" reaches the audio thread.
    bus.publish (slot, clip);
    updateHostDisplay();
}

//==============================================================================
void ABReferenceProcessor::handleAsyncUpdate()
{
    if (const double rate = sampleRateForLoader.exchange (0.0, std::memory_order_relaxed); rate > 0.0)
        loader.setPlaybackSampleRate (rate);

    std::array<juce::String, ABParams::numSlots> paths;

    {
        const juce::ScopedLock lock (restoreLock);
        paths = pathsToRestore;

        for (auto& p : pathsToRestore)
            p.clear();
    }

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
        if (paths[(size_t) slot].isNotEmpty())
            loadReference (slot, juce::File (paths[(size_t) slot]));
}

void ABReferenceProcessor::timerCallback()
{
    // The only place reference audio is ever deallocated while the plugin is
    // running.
    bus.collectRetiredClips();
}

//==============================================================================
void ABReferenceProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = apvts.copyState();
    state.setProperty (ABParams::propStateVersion, ABParams::version, nullptr);

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
        state.setProperty (ABParams::propReferencePathFor (slot),
                           referencePaths[(size_t) slot], nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void ABReferenceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    if (! state.isValid())
        return;

    std::array<juce::String, ABParams::numSlots> paths;

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
        paths[(size_t) slot] = state.getProperty (ABParams::propReferencePathFor (slot),
                                                  juce::String()).toString();

    // A session saved before there were slots carries one unnumbered path. It
    // belongs in slot 1 - dropping it would look to the user like the plugin
    // lost their reference.
    if (paths[0].isEmpty())
        paths[0] = state.getProperty (ABParams::propReferencePath, juce::String()).toString();

    apvts.replaceState (state);

    // setStateInformation can arrive on any thread the host likes, and the
    // loader is a message-thread object. Leave the paths where the message
    // thread will find them rather than reaching across from here.
    bool anyPath = false;

    {
        const juce::ScopedLock lock (restoreLock);
        pathsToRestore = paths;
    }

    for (const auto& p : paths)
        anyPath = anyPath || p.isNotEmpty();

    if (anyPath)
        triggerAsyncUpdate();
}

//==============================================================================
juce::AudioProcessorEditor* ABReferenceProcessor::createEditor()
{
    return new ABReferenceEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ABReferenceProcessor();
}

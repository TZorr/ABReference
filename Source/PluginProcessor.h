//
//  PluginProcessor.h
//  AB Reference
//
//  The plugin proper: parameters, host transport, state, and the small amount
//  of glue that joins the loader, the player, the meters and the A/B engine.
//
//  Two things about this class are decisions rather than defaults.
//
//  **Reported latency is zero and never changes.** Nothing here looks ahead:
//  the meters measure what has already gone past, the crossfade is a ramp, the
//  gain is instantaneous. That is worth saying out loud because a plugin in the
//  last slot of a master chain reporting latency would make the host delay
//  every track feeding it, and a plugin that *changes* its reported latency
//  makes the host re-plan its graph mid-session - which in Logic is an audible
//  stall. setLatencySamples(0) is called once, in the constructor, and there is
//  deliberately no other call to it anywhere in this project.
//
//  **Nothing crosses a thread boundary by accident.** prepareToPlay and
//  setStateInformation can both arrive on threads JUCE makes no promises about,
//  and both of them want to talk to the loader, which is a message-thread
//  object. So they leave a note and trigger an async update instead of reaching
//  across. The audio thread talks to the rest of the world only through atomics
//  and through ReferencePlayer's exchange slots.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "ABEngine.h"
#include "LoudnessMeter.h"
#include "ParameterIds.h"
#include "ReferenceBus.h"
#include "ReferenceLoader.h"
#include "TruePeakMeter.h"

class ABReferenceProcessor : public juce::AudioProcessor,
                            private ReferenceLoader::Listener,
                            private juce::AsyncUpdater,
                            private juce::Timer
{
public:
    ABReferenceProcessor();
    ~ABReferenceProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    //==============================================================================
    // Everything below is for the editor, and is read on the message thread only.

    juce::AudioProcessorValueTreeState apvts;

    /** Message thread. Opens a file chooser's worth of work in the background. */
    void loadReference (int slot, const juce::File& file);
    void clearReference (int slot);

    /** The slot a dropped file should go to when the drop did not name one:
        the first empty slot, so dragging three files in fills 1, 2, 3, and the
        selected slot once they are all occupied. */
    int getSlotForUntargetedDrop() const;

    int  getSelectedSlot() const;

    /** True when this slot has a file assigned to it, whether or not the clip
        has arrived yet. Lets the panel tell "still decoding" apart from
        "nothing here", which look identical from the clip alone. */
    bool hasPathFor (int slot) const
    {
        return referencePaths[(size_t) juce::jlimit (0, ABParams::numSlots - 1, slot)].isNotEmpty();
    }

    void setSelectedSlot (int slot, bool alsoSwitchToB);

    juce::String getReferenceFileFilter() const { return loader.getFileFilter(); }

    /** The clip as the UI knows it - a separate reference from the audio
        thread's, held and released on the message thread. Null when that slot
        is empty. */
    ReferenceClip::Ptr getUiClip (int slot) const;

    /** The selected slot's clip, which is what the waveform, the info line and
        the B meter row are all about. */
    ReferenceClip::Ptr getSelectedClip() const { return getUiClip (getSelectedSlot()); }

    /** Parameter ids for the selected slot's own loop region. */
    const char* getLoopStartParamId() const { return ABParams::loopStartSecFor (getSelectedSlot()); }
    const char* getLoopEndParamId()   const { return ABParams::loopEndSecFor   (getSelectedSlot()); }

    juce::String getStatusMessage() const { return statusMessage; }

    /** True when the last load attempt failed. The editor colours the status
        line from this rather than from what the message says: matching on words
        inside a human-readable string is a bug waiting for the first rewording. */
    bool didLoadFail() const { return loadFailed; }
    bool isLoadingReference() const { return loading; }

    // Published by the audio thread every block.
    float getALufsShortTerm()  const noexcept { return meterA.getShortTermLufs(); }
    float getALufsIntegrated() const noexcept { return meterA.getIntegratedLufs(); }
    float getATruePeakDb()     const noexcept { return peakA.getMaxTruePeakDb(); }

    /** B's live short-term loudness. The reference bus applies each slot's own
        match gain before this is metered, so what comes back is simply what is
        heard - no correction to add, and correct through a slot crossfade where
        two different gains are in play at once. */
    float getBLufsShortTerm() const noexcept { return meterB.getShortTermLufs(); }

    /** B's true peak as heard. Taken from the whole-file measurement rather
        than from a running maximum, so it answers "will this clip if I turn it
        up" completely instead of "has it clipped since you pressed play". */
    float getBTruePeakDb() const noexcept;

    float getMatchDb()     const noexcept { return matchDb.load     (std::memory_order_relaxed); }

    /** True when level matching wanted more than the +/-12 dB it is allowed.
        Worth surfacing rather than hiding: it means B is NOT matched to A, and
        a match that silently gave up is the one thing this panel must not do.
        getDesiredMatchDb() is what it would have applied unclamped. */
    bool  isMatchClamped()   const noexcept { return matchClamped.load (std::memory_order_relaxed); }
    float getDesiredMatchDb() const noexcept { return desiredMatchDb.load (std::memory_order_relaxed); }
    float getTotalRefDb()  const noexcept { return totalRefDb.load  (std::memory_order_relaxed); }
    float getABPosition()  const noexcept { return abPosition.load  (std::memory_order_relaxed); }
    /** Where in the reference file the playhead is - the sample being read, so
        inside a loop region this stays inside the region rather than running off
        along the timeline. */
    float getReferencePositionSeconds() const noexcept { return refPosition.load (std::memory_order_relaxed); }
    bool  isTransportRunning() const noexcept { return transportRunning.load (std::memory_order_relaxed); }

    /** Clears the integrated loudness and the held true peak on both sides.
        They answer questions about one stretch of programme, so they reset
        together or the pair stops making sense. */
    void resetMeters();

private:
    //==============================================================================
    void process (juce::AudioBuffer<float>& buffer, bool bypassed);

    void referenceLoadStarted (int slot, const juce::File&) override;
    void referenceLoadFinished (int slot, ReferenceClip::Ptr, const juce::File&, const juce::String&) override;

    void handleAsyncUpdate() override;
    void timerCallback() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    ReferenceLoader loader { *this };
    ReferenceBus bus;
    ABEngine engine;

    LoudnessMeter meterA, meterB;
    TruePeakMeter peakA;

    juce::AudioBuffer<float> referenceScratch;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    // Cached parameter pointers. getRawParameterValue does a string lookup, and
    // a string lookup per block is a string lookup too many.
    std::atomic<float>* abParam         = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;
    std::atomic<float>* trimParam       = nullptr;
    std::atomic<float>* offsetParam     = nullptr;
    std::atomic<float>* monoParam       = nullptr;
    std::atomic<float>* loopParam       = nullptr;
    std::atomic<float>* slotParam        = nullptr;
    std::array<std::atomic<float>*, ABParams::numSlots> loopStartParams {};
    std::array<std::atomic<float>*, ABParams::numSlots> loopEndParams   {};
    juce::AudioParameterBool* bypassParam = nullptr;

    // Audio thread only. One held match per slot: each reference has its own
    // loudness, so each has its own match, and switching slots must not make
    // the new one inherit the old one's gain for three seconds.
    int64_t freeRunPosition = 0;   // stands in for the timeline when there is no host transport
    std::array<float, ABParams::numSlots> heldMatchDb {};

    // Audio thread writes, editor reads.
    std::atomic<float> matchDb          { 0.0f };
    std::atomic<float> desiredMatchDb   { 0.0f };
    std::atomic<bool>  matchClamped     { false };
    std::atomic<float> totalRefDb       { 0.0f };
    std::atomic<float> abPosition       { 0.0f };
    std::atomic<float> refPosition      { 0.0f };
    std::atomic<bool>  transportRunning { false };

    // Message thread only.
    std::array<ReferenceClip::Ptr, ABParams::numSlots> uiClips;
    std::array<juce::String, ABParams::numSlots> referencePaths;
    juce::String statusMessage;
    bool loading = false;
    bool loadFailed = false;

    // Written by whatever thread prepareToPlay/setStateInformation arrive on,
    // consumed by handleAsyncUpdate on the message thread.
    std::atomic<double> sampleRateForLoader { 0.0 };
    juce::CriticalSection restoreLock;
    std::array<juce::String, ABParams::numSlots> pathsToRestore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABReferenceProcessor)
};

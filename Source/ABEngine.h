//
//  ABEngine.h
//  AB Reference
//
//  The switch, the gain that makes the switch mean something, and the mono
//  button - all three of them ramped, because every one of them is a
//  discontinuity if it is not.
//
//  Three decisions worth defending:
//
//  **Equal power, not linear.** A and B are different pieces of programme
//  material with no correlation between them, so their powers add, not their
//  amplitudes. A linear crossfade therefore dips about 3 dB in the middle. Over
//  8 ms that dip is not heard as a dip - it is heard as a click, or as "the
//  switch does something", and the listener starts wondering about the tool
//  instead of the mix.
//
//  **8 ms.** Long enough that the discontinuity is below the ear's resolution
//  for a step, short enough that nobody perceives a transition at all. Longer
//  fades start to blur the comparison, which is the one thing this control must
//  not do: you are trying to hear a difference across the switch, so the switch
//  must not average the two sides together for any length of time you can hear.
//
//  **A is never touched.** The fully-on-A path does not read or write the
//  buffer at all, so the output is bit-identical rather than merely
//  transparent. That is what makes it safe to leave this plugin in the chain
//  while bouncing, and it is the only way the comparison is honest: if both
//  sides were being adjusted you would be comparing two things the plugin made
//  up. MeterCheck asserts it bit for bit.
//
//  The reference arrives already at level. Level matching is per reference -
//  three slots, three different loudnesses, three different match amounts - so
//  the gain belongs upstream in ReferenceBus, applied to each slot's own audio
//  before the slots are mixed. This class only decides how much of the finished
//  reference bus you hear.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class ABEngine
{
public:
    ABEngine() = default;

    void prepare (double sampleRate);

    /** Jumps to the given side with no fade. For prepareToPlay only - using
        this while audio runs is the hard switch this class exists to avoid. */
    void reset (bool startOnB) noexcept;

    /** Audio thread. false = A, true = B. */
    void setTarget (bool wantB) noexcept { target = wantB ? 1.0f : 0.0f; }

    /** Audio thread. 0 = stereo, 1 = summed to mono. Ramped like everything else. */
    void setMonoAmount (float amount) noexcept { monoSum.setTargetValue (juce::jlimit (0.0f, 1.0f, amount)); }

    /** True when the fade has settled on A, which is the state in which the
        output is guaranteed bit-identical to the input. */
    bool isFullyOnA() const noexcept { return position <= 0.0f && target <= 0.0f; }

    /** Where the fade currently sits, for the UI to show a switch in motion. */
    float getPosition() const noexcept { return position; }

    /** Audio thread. `main` carries A in and the result out; `reference` is the
        reference bus, already at level. */
    void process (juce::AudioBuffer<float>& main,
                  const juce::AudioBuffer<float>& reference,
                  int numSamples) noexcept;

private:
    void applyMonoSum (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    // 8 ms. See the header comment - this number is a perceptual choice, not a
    // performance one, so it is not a parameter.
    static constexpr double crossfadeSeconds = 0.008;

    float position  = 0.0f;   // 0 = A, 1 = B
    float target    = 0.0f;
    float increment = 1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoSum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABEngine)
};

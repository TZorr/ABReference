//
//  ABEngine.cpp
//  AB Reference
//

#include "ABEngine.h"

void ABEngine::prepare (double sampleRate)
{
    const double fadeSamples = juce::jmax (1.0, sampleRate * crossfadeSeconds);
    increment = (float) (1.0 / fadeSamples);

    monoSum.reset (sampleRate, crossfadeSeconds);

    reset (target > 0.5f);
}

void ABEngine::reset (bool startOnB) noexcept
{
    position = startOnB ? 1.0f : 0.0f;
    target   = position;
    monoSum.setCurrentAndTargetValue (monoSum.getTargetValue());
}

void ABEngine::process (juce::AudioBuffer<float>& main,
                        const juce::AudioBuffer<float>& reference,
                        int numSamples) noexcept
{
    // Clamped to two because the pointer arrays below are two wide. The bus
    // layout already guarantees stereo, so this is belt and braces - but a
    // fixed-size array indexed by a value from elsewhere is worth pinning down
    // at the point of use rather than trusting at a distance.
    const int channels = juce::jmin (2, juce::jmin (main.getNumChannels(), reference.getNumChannels()));

    if (channels <= 0 || numSamples <= 0)
        return;

    // An exact comparison, and it is exact on purpose: the ramp below clamps to
    // the target with jmin/jmax, so position lands on precisely the same float
    // the target holds rather than approaching it. Written without == so the
    // float-equal warning does not have to be suppressed on a line where the
    // warning is simply wrong.
    const bool settled = ! (position < target) && ! (position > target);

    if (settled && position <= 0.0f)
    {
        // Fully on A. The buffer is not read, not written, not multiplied by
        // 1.0 - it is left exactly as the host handed it over. Only mono sum,
        // which is a monitoring choice rather than part of the comparison, can
        // still touch it.
        applyMonoSum (main, numSamples);
        return;
    }

    if (settled && position >= 1.0f)
    {
        // Fully on B: a copy, no trigonometry. The reference bus has already
        // applied that slot's own match gain.
        float* out[2] { nullptr, nullptr };
        const float* in[2] { nullptr, nullptr };

        for (int ch = 0; ch < channels; ++ch)
        {
            out[ch] = main.getWritePointer (ch);
            in[ch]  = reference.getReadPointer (ch);
        }

        for (int n = 0; n < numSamples; ++n)
            for (int ch = 0; ch < channels; ++ch)
                out[ch][n] = in[ch][n];

        applyMonoSum (main, numSamples);
        return;
    }

    // Mid-fade. Two sin/cos per sample for a handful of milliseconds every time
    // somebody presses the switch is not a cost worth optimising away with a
    // table whose interpolation error would show up as exactly the kind of
    // low-level artefact this fade exists to remove.
    float* out[2] { nullptr, nullptr };
    const float* in[2] { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
    {
        out[ch] = main.getWritePointer (ch);
        in[ch]  = reference.getReadPointer (ch);
    }

    float localPosition = position;

    for (int n = 0; n < numSamples; ++n)
    {
        if (localPosition < target)
            localPosition = juce::jmin (target, localPosition + increment);
        else if (localPosition > target)
            localPosition = juce::jmax (target, localPosition - increment);

        const float angle   = localPosition * juce::MathConstants<float>::halfPi;
        const float gainA   = std::cos (angle);
        const float gainB   = std::sin (angle);
        for (int ch = 0; ch < channels; ++ch)
            out[ch][n] = out[ch][n] * gainA + in[ch][n] * gainB;
    }

    position = localPosition;

    applyMonoSum (main, numSamples);
}

void ABEngine::applyMonoSum (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (buffer.getNumChannels() < 2)
        return;

    if (! monoSum.isSmoothing() && monoSum.getCurrentValue() <= 0.0f)
        return;   // the common case: stereo, buffer untouched

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int n = 0; n < numSamples; ++n)
    {
        const float amount = monoSum.getNextValue();
        const float mid = (left[n] + right[n]) * 0.5f;

        left[n]  += (mid - left[n])  * amount;
        right[n] += (mid - right[n]) * amount;
    }
}

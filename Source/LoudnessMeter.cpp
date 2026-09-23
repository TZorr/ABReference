//
//  LoudnessMeter.cpp
//  AB Reference
//

#include "LoudnessMeter.h"

#include <cmath>

//==============================================================================
// The analogue prototypes, straight out of BS.1770-4. The odd precision is the
// spec's own: these are the values that make the bilinear transform land on the
// published 48 kHz coefficients exactly, and rounding them to something
// human-looking moves the result in the fifth digit.

namespace
{
    constexpr double shelfFrequency = 1681.974450955533;
    constexpr double shelfGainDb    = 3.999843853973347;
    constexpr double shelfQ         = 0.7071752369554196;

    // Not 0.5. The spec's shelf is defined with this exponent relating the
    // band gain to the shelf gain, and using a plain square root shifts the
    // transition band enough to matter on cymbals.
    constexpr double shelfVbExponent = 0.4996667741545416;

    constexpr double highPassFrequency = 38.13547087602444;
    constexpr double highPassQ         = 0.5003270373238773;
}

LoudnessMeter::BiquadCoefficients LoudnessMeter::makeShelfCoefficients (double sampleRate) noexcept
{
    const double K  = std::tan (juce::MathConstants<double>::pi * shelfFrequency / sampleRate);
    const double Vh = std::pow (10.0, shelfGainDb / 20.0);
    const double Vb = std::pow (Vh, shelfVbExponent);
    const double KK = K * K;
    const double KQ = K / shelfQ;

    const double a0 = 1.0 + KQ + KK;

    BiquadCoefficients c;
    c.b0 = (Vh + Vb * KQ + KK) / a0;
    c.b1 = 2.0 * (KK - Vh)     / a0;
    c.b2 = (Vh - Vb * KQ + KK) / a0;
    c.a1 = 2.0 * (KK - 1.0)    / a0;
    c.a2 = (1.0 - KQ + KK)     / a0;
    return c;
}

LoudnessMeter::BiquadCoefficients LoudnessMeter::makeHighPassCoefficients (double sampleRate) noexcept
{
    const double K  = std::tan (juce::MathConstants<double>::pi * highPassFrequency / sampleRate);
    const double KK = K * K;
    const double KQ = K / highPassQ;

    const double a0 = 1.0 + KQ + KK;

    // The numerator stays at the spec's literal 1, -2, 1 rather than being
    // normalised by a0. That is not a slip: BS.1770 defines the RLB stage that
    // way, it gives about +0.04 dB at Nyquist, and "correcting" it puts the
    // meter 0.04 dB away from every other meter in the world.
    BiquadCoefficients c;
    c.b0 =  1.0;
    c.b1 = -2.0;
    c.b2 =  1.0;
    c.a1 = 2.0 * (KK - 1.0) / a0;
    c.a2 = (1.0 - KQ + KK)  / a0;
    return c;
}

//==============================================================================
void LoudnessMeter::prepare (double sampleRate, int numChannels)
{
    preparedChannels = juce::jlimit (1, maxChannels, numChannels);

    const auto shelf    = makeShelfCoefficients (sampleRate);
    const auto highPass = makeHighPassCoefficients (sampleRate);

    for (auto& f : filters)
    {
        f.shelf.setCoefficients (shelf);
        f.highPass.setCoefficients (highPass);
    }

    // 100 ms. Every gating and averaging window in BS.1770 is a whole number of
    // these, so this is the only granularity the meter needs to track.
    subBlockSamples = juce::jmax (1, (int) std::lround (sampleRate * 0.1));

    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& f : filters)
        f.reset();

    subBlockSum   = 0.0;
    subBlockCount = 0;

    ringSum.fill (0.0);
    ringCount.fill (0);
    writeIndex    = 0;
    subBlocksSeen = 0;

    histPower.fill (0.0);
    histCount.fill (0);
    gatedBlockCount = 0;

    momentary.store  (kLoudnessSilence, std::memory_order_relaxed);
    shortTerm.store  (kLoudnessSilence, std::memory_order_relaxed);
    integrated.store (kLoudnessSilence, std::memory_order_relaxed);
}

void LoudnessMeter::processBlock (const float* const* channelData, int numChannels, int numSamples) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_relaxed))
        reset();

    const int channels = juce::jmin (numChannels, preparedChannels);

    if (channels <= 0 || numSamples <= 0)
        return;

    for (int n = 0; n < numSamples; ++n)
    {
        double sumOfSquares = 0.0;

        // Channel weights G_i are all 1.0 for L and R. They only depart from
        // unity for surround channels, which this plugin does not have, so the
        // multiplication is left out rather than written as x * 1.0.
        for (int ch = 0; ch < channels; ++ch)
        {
            const double y = filters[(size_t) ch].process ((double) channelData[ch][n]);
            sumOfSquares += y * y;
        }

        subBlockSum += sumOfSquares;

        if (++subBlockCount >= subBlockSamples)
            closeSubBlock();
    }
}

void LoudnessMeter::processBuffer (const juce::AudioBuffer<float>& buffer) noexcept
{
    processBlock (buffer.getArrayOfReadPointers(),
                  buffer.getNumChannels(),
                  buffer.getNumSamples());
}

//==============================================================================
void LoudnessMeter::closeSubBlock() noexcept
{
    ringSum[(size_t) writeIndex]   = subBlockSum;
    ringCount[(size_t) writeIndex] = subBlockCount;
    writeIndex = (writeIndex + 1) & ringMask;
    ++subBlocksSeen;

    subBlockSum   = 0.0;
    subBlockCount = 0;

    if (subBlocksSeen >= momentaryBlocks)
        momentary.store (loudnessFromPower (windowPower (momentaryBlocks)), std::memory_order_relaxed);

    if (subBlocksSeen >= shortTermBlocks)
        shortTerm.store (loudnessFromPower (windowPower (shortTermBlocks)), std::memory_order_relaxed);

    // A gating block is 400 ms with 75% overlap, which is exactly "the last
    // four sub-blocks, once per sub-block". Closing one here is what gives the
    // integrated measurement its 10 Hz update rate for free.
    if (subBlocksSeen >= momentaryBlocks)
    {
        const double blockPower = windowPower (momentaryBlocks);
        const float  blockLufs  = loudnessFromPower (blockPower);

        if (blockLufs > kLoudnessSilence)
        {
            const int bucket = (int) std::floor ((blockLufs - histogramFloor) / histogramStepDb);

            // bucket < 0 is the absolute gate at -70 LUFS: silence and room
            // tone are not part of a programme's loudness.
            if (bucket >= 0)
            {
                const size_t index = (size_t) juce::jmin (bucket, histogramSize - 1);
                histPower[index] += blockPower;
                histCount[index] += 1;
                ++gatedBlockCount;

                recomputeIntegrated();
            }
        }
    }
}

double LoudnessMeter::windowPower (int numSubBlocks) const noexcept
{
    double power = 0.0;
    int64_t samples = 0;

    for (int k = 0; k < numSubBlocks; ++k)
    {
        const size_t index = (size_t) ((writeIndex - 1 - k) & ringMask);
        power   += ringSum[index];
        samples += ringCount[index];
    }

    return samples > 0 ? power / (double) samples : 0.0;
}

void LoudnessMeter::recomputeIntegrated() noexcept
{
    // First pass: the ungated mean over everything that survived the absolute
    // gate. This exists only to locate the relative gate.
    double  totalPower = 0.0;
    int64_t totalCount = 0;

    for (size_t i = 0; i < (size_t) histogramSize; ++i)
    {
        totalPower += histPower[i];
        totalCount += histCount[i];
    }

    if (totalCount == 0)
    {
        integrated.store (kLoudnessSilence, std::memory_order_relaxed);
        return;
    }

    const double relativeThreshold = -0.691 + 10.0 * std::log10 (totalPower / (double) totalCount) - 10.0;

    // Second pass: the mean over blocks louder than the relative gate. Buckets
    // are 0.1 dB wide, so taking whole buckets from the first one entirely
    // above the threshold quantises the gate by at most 0.1 dB - far below the
    // point where it changes a reading, and it keeps this a linear scan.
    const int startBucket = (int) std::ceil ((relativeThreshold - histogramFloor) / histogramStepDb);

    double  gatedPower = 0.0;
    int64_t gatedCount = 0;

    for (size_t i = (size_t) juce::jmax (0, startBucket); i < (size_t) histogramSize; ++i)
    {
        gatedPower += histPower[i];
        gatedCount += histCount[i];
    }

    integrated.store (gatedCount > 0 ? loudnessFromPower (gatedPower / (double) gatedCount)
                                     : kLoudnessSilence,
                      std::memory_order_relaxed);
}

bool LoudnessMeter::hasIntegratedFor (double seconds) const noexcept
{
    // Gating blocks arrive every 100 ms, so the count is the duration in
    // tenths of a second - of material that passed the absolute gate, which is
    // the part that matters. Three seconds of silence buys nothing.
    return (double) gatedBlockCount * 0.1 >= seconds;
}

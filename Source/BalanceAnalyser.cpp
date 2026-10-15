//
//  BalanceAnalyser.cpp
//  AB Reference
//

#include "BalanceAnalyser.h"
#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <complex>

//==============================================================================
float Balance::erbNumber (float hz) noexcept
{
    return 21.4f * std::log10 (4.37e-3f * hz + 1.0f);
}

float Balance::hzFromErbNumber (float erb) noexcept
{
    return (std::pow (10.0f, erb / 21.4f) - 1.0f) / 4.37e-3f;
}

float Balance::bandSpacing() noexcept
{
    return (erbNumber (highestHz) - erbNumber (lowestHz)) / (float) (numBands - 1);
}

float Balance::bandCentreErb (int band) noexcept
{
    return erbNumber (lowestHz) + (float) band * bandSpacing();
}

float Balance::bandCentreHz (int band) noexcept
{
    return hzFromErbNumber (bandCentreErb (band));
}

//==============================================================================
namespace
{
    /** |H(e^jw)|^2 of one BS.1770 biquad. */
    double powerGain (const LoudnessMeter::BiquadCoefficients& c, double omega)
    {
        const auto z1 = std::polar (1.0, -omega);
        const auto z2 = z1 * z1;

        const auto numerator   = c.b0 + c.b1 * z1 + c.b2 * z2;
        const auto denominator = 1.0 + c.a1 * z1 + c.a2 * z2;

        return std::norm (numerator) / std::norm (denominator);
    }

    constexpr double absoluteGateLufs = -70.0;
    constexpr double relativeGateLu   = -10.0;

    float toDb (double share) noexcept
    {
        return (float) (10.0 * std::log10 (juce::jmax (share, 1.0e-12)));
    }
}

//==============================================================================
void BalanceAnalyser::prepare (double sampleRate)
{
    rate = sampleRate;

    const int order = sampleRate > 60000.0 ? 14 : 13;
    frameSize = 1 << order;

    fft = std::make_unique<juce::dsp::FFT> (order);
    work.assign ((size_t) frameSize * 2, 0.0f);

    // Periodic Hann.
    window.resize ((size_t) frameSize);
    double windowPower = 0.0;

    for (int n = 0; n < frameSize; ++n)
    {
        const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / frameSize);
        window[(size_t) n] = (float) w;
        windowPower += w * w;
    }

    // Parseval with the window's power taken out, so a bin's |X|^2 times this
    // (twice, for every bin but DC and Nyquist) is its share of the signal's
    // mean square - the unit LUFS is computed in.
    powerScale = 1.0 / ((double) frameSize * windowPower);

    const int bins = frameSize / 2 + 1;
    firstBand.assign ((size_t) bins, -2);
    firstWeight.assign ((size_t) bins, 0.0f);
    secondWeight.assign ((size_t) bins, 0.0f);
    kWeight.assign ((size_t) bins, 0.0f);

    const auto shelf    = LoudnessMeter::makeShelfCoefficients (sampleRate);
    const auto highPass = LoudnessMeter::makeHighPassCoefficients (sampleRate);

    const float firstErb = Balance::bandCentreErb (0);
    const float spacing  = Balance::bandSpacing();

    for (int k = 0; k < bins; ++k)
    {
        const double hz = (double) k * sampleRate / frameSize;
        const double omega = juce::MathConstants<double>::twoPi * k / frameSize;

        // Up to 20 kHz only. BS.1770 weights everything to Nyquist, but at 96
        // kHz that is half the spectrum nobody hears, and a reference with
        // ultrasonic noise in it would read as quieter everywhere else.
        kWeight[(size_t) k] = hz <= 20000.0
                                ? (float) (powerGain (shelf, omega) * powerGain (highPass, omega))
                                : 0.0f;

        // Where the bin sits between two band centres, in units of the spacing.
        // Each bin feeds the two neighbouring triangles with weights that sum
        // to one; bins outside the outermost triangles feed nothing.
        const float position = (Balance::erbNumber ((float) hz) - firstErb) / spacing;

        if (k == 0 || position <= -1.0f || position >= (float) Balance::numBands)
            continue;

        const int below = (int) std::floor (position);
        const float fraction = position - (float) below;

        firstBand[(size_t) k]    = below;
        firstWeight[(size_t) k]  = below >= 0 ? 1.0f - fraction : 0.0f;
        secondWeight[(size_t) k] = below + 1 < Balance::numBands ? fraction : 0.0f;
    }

    resetAverage();
}

double BalanceAnalyser::analyseFrame (const float* const* channels, int numChannels, Balance::Bands& bandPower)
{
    bandPower.fill (0.0f);

    if (fft == nullptr)
        return 0.0;

    const int bins = frameSize / 2 + 1;
    double kTotal = 0.0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* samples = channels[ch];

        for (int n = 0; n < frameSize; ++n)
            work[(size_t) n] = samples[n] * window[(size_t) n];

        std::fill (work.begin() + frameSize, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);

        for (int k = 0; k < bins; ++k)
        {
            const double re = work[(size_t) (2 * k)];
            const double im = work[(size_t) (2 * k + 1)];
            const double interior = (k == 0 || k == bins - 1) ? 1.0 : 2.0;
            const double power = (re * re + im * im) * powerScale * interior;

            kTotal += power * kWeight[(size_t) k];

            const int below = firstBand[(size_t) k];

            if (below == -2)
                continue;

            if (below >= 0)
                bandPower[(size_t) below] += (float) (power * firstWeight[(size_t) k]);

            if (below + 1 < Balance::numBands)
                bandPower[(size_t) (below + 1)] += (float) (power * secondWeight[(size_t) k]);
        }
    }

    return kTotal;
}

double BalanceAnalyser::frameLufs (double kWeightedPower) noexcept
{
    return -0.691 + 10.0 * std::log10 (juce::jmax (kWeightedPower, 1.0e-30));
}

//==============================================================================
Balance::Corridor BalanceAnalyser::measureCorridor (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    Balance::Corridor corridor;

    const int length   = audio.getNumSamples();
    const int channels = juce::jmin (2, audio.getNumChannels());

    if (length <= 0 || channels <= 0 || sampleRate <= 0.0)
        return corridor;

    BalanceAnalyser analyser;
    analyser.prepare (sampleRate);

    const int size = analyser.getFrameSize();
    const int hop  = analyser.getHopSize();

    // Every frame's band powers and K-weighted power. A few megabytes for the
    // longest reference this plugin accepts, freed when this returns.
    std::vector<Balance::Bands> frames;
    std::vector<double> framePower;

    juce::AudioBuffer<float> padded (channels, size);

    for (int start = 0; start < length; start += hop)
    {
        const float* pointers[2] {};

        // The last frame or two run past the end: zero-padded copies.
        if (start + size <= length)
        {
            for (int ch = 0; ch < channels; ++ch)
                pointers[ch] = audio.getReadPointer (ch, start);
        }
        else
        {
            padded.clear();

            for (int ch = 0; ch < channels; ++ch)
            {
                padded.copyFrom (ch, 0, audio, ch, start, length - start);
                pointers[ch] = padded.getReadPointer (ch);
            }
        }

        Balance::Bands bands;
        framePower.push_back (analyser.analyseFrame (pointers, channels, bands));
        frames.push_back (bands);
    }

    // Gating as for integrated loudness: absolute first, then relative to the
    // mean of what passed.
    const int numFrames = (int) frames.size();
    std::vector<bool> passes ((size_t) numFrames, false);

    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < numFrames; ++i)
    {
        if (frameLufs (framePower[(size_t) i]) > absoluteGateLufs)
        {
            sum += framePower[(size_t) i];
            ++count;
        }
    }

    if (count == 0)
        return corridor;

    const double relativeGate = frameLufs (sum / count) + relativeGateLu;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto lufs = frameLufs (framePower[(size_t) i]);
        passes[(size_t) i] = lufs > absoluteGateLufs && lufs >= relativeGate;
    }

    // 3 s windows, one starting every second. A window counts when at least
    // half of it passed the gate; a reference shorter than one window is one
    // window.
    const int perWindow = juce::jmax (1, juce::roundToInt (Balance::windowSeconds * sampleRate / hop));
    const int step      = juce::jmax (1, juce::roundToInt (sampleRate / hop));
    const int needed    = numFrames < perWindow ? 1 : juce::jmax (1, perWindow / 2);

    std::array<std::vector<float>, Balance::numBands> values;

    for (int first = 0; first < numFrames; first += step)
    {
        const int last = juce::jmin (numFrames, first + perWindow);

        std::array<double, Balance::numBands> bandSum {};
        double powerSum = 0.0;
        int used = 0;

        for (int i = first; i < last; ++i)
        {
            if (! passes[(size_t) i])
                continue;

            for (int b = 0; b < Balance::numBands; ++b)
                bandSum[(size_t) b] += frames[(size_t) i][(size_t) b];

            powerSum += framePower[(size_t) i];
            ++used;
        }

        if (used >= needed && powerSum > 0.0)
            for (int b = 0; b < Balance::numBands; ++b)
                values[(size_t) b].push_back (toDb (bandSum[(size_t) b] / powerSum));

        if (last == numFrames)
            break;
    }

    if (values[0].empty())
        return corridor;

    const auto percentile = [] (const std::vector<float>& sorted, double p)
    {
        const double position = p * (double) (sorted.size() - 1);
        const auto below = (size_t) std::floor (position);
        const auto above = juce::jmin (below + 1, sorted.size() - 1);
        const auto fraction = (float) (position - (double) below);
        return sorted[below] + (sorted[above] - sorted[below]) * fraction;
    };

    for (int b = 0; b < Balance::numBands; ++b)
    {
        auto& v = values[(size_t) b];
        std::sort (v.begin(), v.end());

        corridor.low[(size_t) b]    = percentile (v, 0.1);
        corridor.median[(size_t) b] = percentile (v, 0.5);
        corridor.high[(size_t) b]   = percentile (v, 0.9);
    }

    corridor.windows = (int) values[0].size();
    return corridor;
}

//==============================================================================
void BalanceAnalyser::resetAverage() noexcept
{
    averagePower.fill (0.0f);
    averageKPower = 0.0;
    haveAverage = false;
    framesSinceSignal = 0;
}

void BalanceAnalyser::addToAverage (const Balance::Bands& bandPower, double kWeightedPower) noexcept
{
    if (frameLufs (kWeightedPower) <= absoluteGateLufs)
    {
        ++framesSinceSignal;
        return;
    }

    framesSinceSignal = 0;

    if (! haveAverage)
    {
        averagePower = bandPower;
        averageKPower = kWeightedPower;
        haveAverage = true;
        return;
    }

    // One time constant of windowSeconds, frame by frame.
    const auto alpha = (float) (1.0 - std::exp (-(double) getHopSize() / (rate * Balance::windowSeconds)));

    for (int b = 0; b < Balance::numBands; ++b)
        averagePower[(size_t) b] += alpha * (bandPower[(size_t) b] - averagePower[(size_t) b]);

    averageKPower += alpha * (kWeightedPower - averageKPower);
}

bool BalanceAnalyser::getAverage (Balance::Bands& balanceDb) const noexcept
{
    if (! haveAverage || averageKPower <= 0.0)
        return false;

    for (int b = 0; b < Balance::numBands; ++b)
        balanceDb[(size_t) b] = toDb (averagePower[(size_t) b] / averageKPower);

    return true;
}

double BalanceAnalyser::getSecondsSinceSignal() const noexcept
{
    return rate > 0.0 ? (double) framesSinceSignal * getHopSize() / rate : 0.0;
}

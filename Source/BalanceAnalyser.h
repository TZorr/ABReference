//
//  BalanceAnalyser.h
//  AB Reference
//
//  Spectral balance on the ear's own frequency scale, for comparing A with the
//  reference at equal loudness.
//
//  Three decisions make this a balance and not just another spectrum:
//
//    - Power is summed, not dB averaged, in 40 bands spaced evenly on the ERB
//      scale (Glasberg & Moore): narrow in hertz at the bottom, wide at the top,
//      the way the ear resolves frequency. A third-octave band is too wide in
//      the bass and too narrow in the presence range by comparison. The bands
//      are overlapping triangles that sum to one, so every bin's energy is
//      counted exactly once.
//
//    - Every band is expressed as a share of the frame's K-weighted power -
//      the ear weighting BS.1770 uses, so "equally loud" here means exactly
//      what it means to the level match. A curve is then independent of level:
//      the same master 10 dB quieter draws the same line.
//
//    - Three-second averages, both sides. The reference is cut into 3 s
//      windows at load and every window's balance goes into a per-band
//      distribution; its 10th, 50th and 90th percentiles are the corridor. A is
//      averaged live over the same 3 s. Single 85 ms frames would compare a
//      kick drum with the gap after it.
//
//  The corridor is what makes the display tub-shaped: masters agree closely in
//  the mids, where the ear is most sensitive, and differ a lot in the sub bass
//  and the air band, so the 10-90 % range is narrow in the middle and wide at
//  both ends.
//
//  Nothing here runs on the audio thread. The reference is analysed on the
//  loader thread; A is analysed on the message thread from samples the audio
//  thread hands over through a FIFO (see ABReferenceProcessor::updateBalance).
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace Balance
{
    inline constexpr int   numBands  = 40;
    inline constexpr float lowestHz  = 30.0f;
    inline constexpr float highestHz = 16000.0f;

    /** The averaging time for one balance reading, on both sides. */
    inline constexpr double windowSeconds = 3.0;

    using Bands = std::array<float, numBands>;

    /** Glasberg & Moore's ERB-number scale, in Cams. */
    float erbNumber (float hz) noexcept;
    float hzFromErbNumber (float erb) noexcept;

    /** Spacing of the band centres on the ERB-number scale. */
    float bandSpacing() noexcept;
    float bandCentreErb (int band) noexcept;
    float bandCentreHz (int band) noexcept;

    /** The reference's balance: per band, the 10th, 50th and 90th percentile of
        its 3 s windows, in dB relative to the window's K-weighted power. */
    struct Corridor
    {
        Bands low {}, median {}, high {};
        int windows = 0;   // 0 means nothing above the gate was found
        bool isValid() const noexcept { return windows > 0; }
    };
}

//==============================================================================
class BalanceAnalyser
{
public:
    BalanceAnalyser() = default;

    /** Allocates. The FFT is 8192 points up to 60 kHz and 16384 above, so a
        bin is about 6 Hz wide at every rate and the narrow bass bands still get
        several bins each. */
    void prepare (double sampleRate);

    double getSampleRate() const noexcept { return rate; }
    int getFrameSize() const noexcept { return frameSize; }
    int getHopSize()   const noexcept { return frameSize / 2; }

    /** One Hann-windowed frame of getFrameSize() samples per channel. Writes the
        power in each band, in mean-square units summed over the channels, and
        returns the frame's K-weighted mean square on the same scale. */
    double analyseFrame (const float* const* channels, int numChannels, Balance::Bands& bandPower);

    /** A frame's loudness in LUFS from analyseFrame's return value, for gating. */
    static double frameLufs (double kWeightedPower) noexcept;

    /** The reference's corridor, measured over a whole buffer. Gated like
        integrated loudness (-70 LUFS absolute, -10 LU relative), so fades,
        silence and the tail after the last note do not widen it. */
    static Balance::Corridor measureCorridor (const juce::AudioBuffer<float>& audio, double sampleRate);

    /** Live use: frames analysed one after another, averaged over
        Balance::windowSeconds. Frames below -70 LUFS are skipped, so a stopped
        transport holds the last reading instead of drifting into the noise. */
    void addToAverage (const Balance::Bands& bandPower, double kWeightedPower) noexcept;
    void resetAverage() noexcept;

    /** dB per band relative to the K-weighted power, or false until there has
        been anything above the gate. */
    bool getAverage (Balance::Bands& balanceDb) const noexcept;

    /** Seconds of analysed material since the last frame above the gate. */
    double getSecondsSinceSignal() const noexcept;

private:
    double rate = 0.0;
    int frameSize = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work;

    // Per bin: up to two bands and their triangle weights, and the K-weighting
    // power gain. Built once in prepare().
    std::vector<int>   firstBand;
    std::vector<float> firstWeight, secondWeight;
    std::vector<float> kWeight;
    double powerScale = 0.0;

    // Live average: band powers and K-weighted power averaged separately and
    // divided at the end, so a loud bar counts for more than a quiet one, as
    // it does to the ear and in the reference's 3 s windows.
    Balance::Bands averagePower {};
    double averageKPower = 0.0;
    bool haveAverage = false;
    int framesSinceSignal = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BalanceAnalyser)
};

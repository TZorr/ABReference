//
//  MeterCheck.cpp
//  AB Reference
//
//  The meters and the A/B engine, run against signals whose answer is known
//  from the standard rather than from this code.
//
//  This exists because of the failure mode that is specific to loudness tools:
//  a LUFS meter that is wrong does not crash, does not glitch, and does not
//  look wrong. It reads -9.2 instead of -8.6 and every decision made in front
//  of it inherits the error without anybody noticing. There is no way to hear
//  that, so it has to be measured, and it has to be measured against numbers
//  that came from somewhere other than this project.
//
//  Where the numbers come from:
//
//    - The 48 kHz filter coefficients are the table printed in ITU-R BS.1770-4.
//      Matching it is what makes the derived coefficients trustworthy at 44.1
//      and 96 kHz, where no published table exists to check against.
//    - The sine cases are EBU Tech 3341 compliance tests 1 and 2.
//    - The gating cases are arithmetic on the definitions in BS.1770-4, and
//      each is built so that a meter with the gate missing gives a visibly
//      different answer - a test that passes with the feature removed is not
//      testing the feature.
//    - The crossfade cases assert the two properties the A/B switch claims:
//      that A is passed through bit for bit, and that the fade is equal power.
//
//  Run it with no arguments. Exit status is non-zero if anything failed.
//

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

#include "ABEngine.h"
#include "LoudnessMeter.h"
#include "ReferenceLoader.h"
#include "ReferenceBus.h"
#include "ReferencePlayer.h"
#include "TruePeakMeter.h"

//==============================================================================
namespace
{
    int failures = 0;
    int checks   = 0;

    /** Same bits, not "close enough". Comparing the bit patterns rather than the
        values is what the bit-transparency claims actually mean, it keeps
        -Wfloat-equal honest instead of suppressed, and it is stricter than ==:
        +0.0 and -0.0 compare equal as floats but are different bits. */
    bool sameBits (float a, float b) noexcept
    {
        return std::bit_cast<std::uint32_t> (a) == std::bit_cast<std::uint32_t> (b);
    }

    void section (const std::string& title)
    {
        std::printf ("\n\033[1m%s\033[0m\n", title.c_str());
    }

    void report (bool passed, const std::string& what, const std::string& detail)
    {
        ++checks;

        if (! passed)
            ++failures;

        std::printf ("  %s  %-52s %s\n",
                     passed ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m",
                     what.c_str(),
                     detail.c_str());
    }

    void expectNear (double actual, double expected, double tolerance, const std::string& what)
    {
        const double delta = std::abs (actual - expected);
        char detail[160];
        std::snprintf (detail, sizeof (detail), "got %.6f  expected %.6f  (delta %.2e, tol %.0e)",
                       actual, expected, delta, tolerance);
        report (delta <= tolerance, what, detail);
    }

    void expectWithin (double actual, double low, double high, const std::string& what)
    {
        char detail[160];
        std::snprintf (detail, sizeof (detail), "got %.4f  expected [%.4f, %.4f]", actual, low, high);
        report (actual >= low && actual <= high, what, detail);
    }

    void expectTrue (bool condition, const std::string& what, const std::string& detail = {})
    {
        report (condition, what, detail);
    }

    //==========================================================================
    /** Amplitude of a sine at the given dBFS, using the convention a full-scale
        sine (amplitude 1.0) is 0 dBFS. This is the convention EBU Tech 3341
        uses to specify its test signals. */
    double sineAmplitudeFor (double dBFS)
    {
        return std::pow (10.0, dBFS / 20.0);
    }

    void fillSine (juce::AudioBuffer<float>& buffer, double sampleRate,
                   double frequency, double amplitude, double phase = 0.0)
    {
        const double increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const auto value = (float) (amplitude * std::cos (phase + increment * n));

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, n, value);
        }
    }

    /** Feeds a buffer to a meter in host-sized chunks, so the meter is exercised
        the way the plugin actually drives it rather than in one giant call. */
    void feed (LoudnessMeter& meter, const juce::AudioBuffer<float>& buffer, int blockSize = 512)
    {
        const int total = buffer.getNumSamples();

        for (int start = 0; start < total; start += blockSize)
        {
            const int count = juce::jmin (blockSize, total - start);
            const float* pointers[2]
            {
                buffer.getReadPointer (0, start),
                buffer.getNumChannels() > 1 ? buffer.getReadPointer (1, start) : nullptr
            };

            meter.processBlock (pointers, buffer.getNumChannels(), count);
        }
    }
}

//==============================================================================
static void testCoefficientDerivation()
{
    section ("K-weighting derivation vs. the BS.1770-4 table at 48 kHz");

    const auto shelf = LoudnessMeter::makeShelfCoefficients (48000.0);

    expectNear (shelf.b0,  1.53512485958697, 1e-9, "shelf b0");
    expectNear (shelf.b1, -2.69169618940638, 1e-9, "shelf b1");
    expectNear (shelf.b2,  1.19839281085285, 1e-9, "shelf b2");
    expectNear (shelf.a1, -1.69065929318241, 1e-9, "shelf a1");
    expectNear (shelf.a2,  0.73248077421585, 1e-9, "shelf a2");

    // The published RLB coefficients carry the fingerprint of a float32 round
    // trip, so they are only good to about seven digits. Holding the derivation
    // to a tighter tolerance than the reference it is checked against would be
    // testing the rounding, not the filter.
    const auto highPass = LoudnessMeter::makeHighPassCoefficients (48000.0);

    expectNear (highPass.b0,  1.0, 1e-12, "RLB b0");
    expectNear (highPass.b1, -2.0, 1e-12, "RLB b1");
    expectNear (highPass.b2,  1.0, 1e-12, "RLB b2");
    expectNear (highPass.a1, -1.99004745483398, 1e-6, "RLB a1");
    expectNear (highPass.a2,  0.99007225036621, 1e-6, "RLB a2");
}

//==============================================================================
static void testSineLevels()
{
    section ("EBU Tech 3341 sine cases, at three sample rates");

    // The whole point of deriving the coefficients rather than pasting the
    // 48 kHz table: 44.1 and 96 kHz have to read the same. A meter using the
    // pasted table reads about 0.15 LU high at 44.1 kHz and considerably worse
    // at 96 kHz, which is exactly the kind of error nobody notices.
    for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const double level : { -23.0, -33.0 })
        {
            LoudnessMeter meter;
            meter.prepare (sampleRate, 2);

            juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 20.0));
            fillSine (buffer, sampleRate, 1000.0, sineAmplitudeFor (level));
            feed (meter, buffer);

            char label[96];

            std::snprintf (label, sizeof (label), "%g kHz, %g dBFS sine: momentary",
                           sampleRate / 1000.0, level);
            expectNear (meter.getMomentaryLufs(), level, 0.1, label);

            std::snprintf (label, sizeof (label), "%g kHz, %g dBFS sine: short-term",
                           sampleRate / 1000.0, level);
            expectNear (meter.getShortTermLufs(), level, 0.1, label);

            std::snprintf (label, sizeof (label), "%g kHz, %g dBFS sine: integrated",
                           sampleRate / 1000.0, level);
            expectNear (meter.getIntegratedLufs(), level, 0.1, label);
        }
    }
}

//==============================================================================
static void testGating()
{
    section ("Gating");

    constexpr double sampleRate = 48000.0;
    const double amplitude = sineAmplitudeFor (-23.0);

    // Absolute gate. Ten seconds of programme followed by ten seconds of
    // digital silence must still integrate to -23. Without the -70 LUFS gate
    // the silence halves the mean power and the answer becomes -26.0.
    {
        LoudnessMeter meter;
        meter.prepare (sampleRate, 2);

        juce::AudioBuffer<float> tone (2, (int) (sampleRate * 10.0));
        fillSine (tone, sampleRate, 1000.0, amplitude);
        feed (meter, tone);

        juce::AudioBuffer<float> silence (2, (int) (sampleRate * 10.0));
        silence.clear();
        feed (meter, silence);

        expectNear (meter.getIntegratedLufs(), -23.0, 0.1,
                    "absolute gate: tone then silence");
    }

    // Relative gate. Ten seconds at -23 then ten at -43. The quiet half is more
    // than 10 LU below the ungated mean, so it must be excluded and the answer
    // stays -23. A meter with the relative gate missing reads -25.97.
    {
        LoudnessMeter meter;
        meter.prepare (sampleRate, 2);

        juce::AudioBuffer<float> loud (2, (int) (sampleRate * 10.0));
        fillSine (loud, sampleRate, 1000.0, amplitude);
        feed (meter, loud);

        juce::AudioBuffer<float> quiet (2, (int) (sampleRate * 10.0));
        fillSine (quiet, sampleRate, 1000.0, sineAmplitudeFor (-43.0));
        feed (meter, quiet);

        expectNear (meter.getIntegratedLufs(), -23.0, 0.15,
                    "relative gate: -23 then -43 excludes the quiet half");
    }

    // A control for the case above: -23 then -28 is only 5 LU down, which is
    // inside the relative gate, so both halves count and the mean must move.
    // 10*log10((1 + 10^-0.5)/2) = -1.83 dB below -23.
    {
        LoudnessMeter meter;
        meter.prepare (sampleRate, 2);

        juce::AudioBuffer<float> loud (2, (int) (sampleRate * 10.0));
        fillSine (loud, sampleRate, 1000.0, amplitude);
        feed (meter, loud);

        juce::AudioBuffer<float> nearby (2, (int) (sampleRate * 10.0));
        fillSine (nearby, sampleRate, 1000.0, sineAmplitudeFor (-28.0));
        feed (meter, nearby);

        const double expected = -23.0 + 10.0 * std::log10 ((1.0 + std::pow (10.0, -0.5)) / 2.0);
        expectNear (meter.getIntegratedLufs(), expected, 0.15,
                    "relative gate: -23 then -28 keeps both halves");
    }
}

//==============================================================================
static void testWindowLengths()
{
    section ("Window lengths");

    constexpr double sampleRate = 48000.0;

    // One second of tone followed by two seconds of silence fills the 3 s
    // short-term window exactly one third full, so short-term must read
    // 10*log10(1/3) = -4.77 below the tone, while the 400 ms momentary window
    // sees only silence. A meter whose windows are the wrong length fails one
    // or both of these by several dB.
    LoudnessMeter meter;
    meter.prepare (sampleRate, 2);

    juce::AudioBuffer<float> tone (2, (int) sampleRate);
    fillSine (tone, sampleRate, 1000.0, sineAmplitudeFor (-23.0));
    feed (meter, tone);

    juce::AudioBuffer<float> silence (2, (int) (sampleRate * 2.0));
    silence.clear();
    feed (meter, silence);

    expectNear (meter.getShortTermLufs(), -23.0 + 10.0 * std::log10 (1.0 / 3.0), 0.15,
                "short-term window is 3 s");

    expectTrue (meter.getMomentaryLufs() < -60.0,
                "momentary window is 400 ms",
                "expected near-silence after 2 s of digital black");

    // The integrated value is NOT -23 here, and working out why is the useful
    // part of this case. Gating blocks are 400 ms with 75% overlap, so one
    // closes every 100 ms covering the previous four sub-blocks. With ten
    // sub-blocks of tone there are seven blocks made entirely of tone, then
    // three that straddle the boundary at 3/4, 1/2 and 1/4 power, and then only
    // silence, which the absolute gate drops. The straddling three are between
    // 1.2 and 6 dB down - nowhere near the 10 LU relative gate - so they count,
    // and the mean lands 10*log10(8.5/10) below the tone.
    //
    // A meter that reported a clean -23 here would be one that had quietly
    // thrown its partial blocks away.
    const double expectedIntegrated = -23.0 + 10.0 * std::log10 ((7.0 + 0.75 + 0.5 + 0.25) / 10.0);

    expectNear (meter.getIntegratedLufs(), expectedIntegrated, 0.05,
                "integrated counts the partial blocks at the boundary");
}

//==============================================================================
static void testTruePeak()
{
    section ("True peak");

    constexpr double sampleRate = 44100.0;

    // A sine at exactly fs/4, offset by an eighth of a cycle, lands every
    // sample on +/-0.7071 while the waveform between them reaches 1.0. Sample
    // peak says -3.01 dBFS; the truth is 0 dBTP. This is the case that makes
    // the difference between the two meters visible, and it is not a contrived
    // one - it is what a limiter's output looks like.
    juce::AudioBuffer<float> buffer (2, 4096);
    fillSine (buffer, sampleRate, sampleRate / 4.0, 1.0, juce::MathConstants<double>::pi / 4.0);

    const double samplePeakDb = juce::Decibels::gainToDecibels (buffer.getMagnitude (0, buffer.getNumSamples()));

    TruePeakMeter meter;
    meter.prepare (2, 512);

    auto sweep = [&]
    {
        for (int start = 0; start < buffer.getNumSamples(); start += 512)
        {
            const float* pointers[2] { buffer.getReadPointer (0, start), buffer.getReadPointer (1, start) };
            meter.processBlock (pointers, 2, 512);
        }
    };

    sweep();
    const double truePeakDb = meter.getMaxTruePeakDb();

    expectNear (samplePeakDb, -3.0103, 0.01, "the test signal's sample peak is -3.01 dBFS");

    // 0.05 dB, and that tolerance is measured rather than guessed: this case
    // was the one that exposed the cold-filter overshoot, reading +0.81 dBTP
    // before TruePeakMeter learned to prime itself and 0.00 after. The tight
    // band is the point - loosen it and the bug comes back unnoticed.
    expectNear (truePeakDb, 0.0, 0.05, "4x true peak recovers the inter-sample peak");

    expectTrue (truePeakDb - samplePeakDb > 1.8,
                "true peak is meaningfully above sample peak",
                juce::String (truePeakDb - samplePeakDb, 3).toStdString() + " dB above");
}

//==============================================================================
static void testCrossfade()
{
    section ("A/B engine");

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // The central claim of the whole plugin: on A, the buffer is not touched.
    // Not "transparent", not "unity gain" - the same bits that arrived.
    {
        ABEngine engine;
        engine.prepare (sampleRate);
        engine.reset (false);
        engine.setTarget (false);

        juce::Random random (1234);
        juce::AudioBuffer<float> main (2, blockSize), original (2, blockSize), reference (2, blockSize);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
            {
                main.setSample (ch, n, random.nextFloat() * 2.0f - 1.0f);
                reference.setSample (ch, n, random.nextFloat() * 2.0f - 1.0f);
            }

        original.makeCopyOf (main);
        engine.process (main, reference, blockSize);

        bool identical = true;

        for (int ch = 0; ch < 2 && identical; ++ch)
            for (int n = 0; n < blockSize; ++n)
                if (! sameBits (main.getSample (ch, n), original.getSample (ch, n)))
                {
                    identical = false;
                    break;
                }

        expectTrue (identical, "A is passed through bit for bit",
                    "with a loud reference sitting on the other side of the switch");
    }

    // Equal power across the fade. Running the fade twice - once with A held at
    // 1.0 and B silent, once the other way round - recovers cos and sin
    // directly, and their squares must sum to 1 at every sample. A linear fade
    // fails this by 3 dB in the middle, which is the audible dip.
    {
        const int totalSamples = (int) (sampleRate * 0.02);   // comfortably past the 8 ms fade

        auto runFade = [&] (bool measureA)
        {
            ABEngine engine;
            engine.prepare (sampleRate);
            engine.reset (false);
            engine.setTarget (true);

            std::vector<float> captured;
            captured.reserve ((size_t) totalSamples);

            juce::AudioBuffer<float> main (2, blockSize), reference (2, blockSize);

            for (int done = 0; done < totalSamples; done += blockSize)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < blockSize; ++n)
                    {
                        main.setSample (ch, n, measureA ? 1.0f : 0.0f);
                        reference.setSample (ch, n, measureA ? 0.0f : 1.0f);
                    }

                engine.process (main, reference, blockSize);

                for (int n = 0; n < blockSize; ++n)
                    captured.push_back (main.getSample (0, n));
            }

            return captured;
        };

        const auto gainA = runFade (true);
        const auto gainB = runFade (false);

        double worstError = 0.0;

        for (size_t n = 0; n < gainA.size(); ++n)
        {
            const double power = (double) gainA[n] * gainA[n] + (double) gainB[n] * gainB[n];
            worstError = juce::jmax (worstError, std::abs (power - 1.0));
        }

        expectNear (worstError, 0.0, 1e-5, "gain_A^2 + gain_B^2 == 1 throughout the fade");

        expectTrue (gainA.front() > 0.99f && gainA.back() < 0.01f,
                    "the fade starts on A and ends on B");

        // No step discontinuity: with both sides held at DC, every sample-to-
        // sample change is the fade's own slope. Over 8 ms at 48 kHz that is
        // 384 steps, so no single step may exceed roughly pi/2 / 384.
        double worstStep = 0.0;

        for (size_t n = 1; n < gainB.size(); ++n)
            worstStep = juce::jmax (worstStep, std::abs ((double) gainB[n] - gainB[n - 1]));

        expectWithin (worstStep, 0.0, 0.006, "no step discontinuity across the switch");
    }
}

//==============================================================================
namespace
{
    /** A clip that says when it dies, so the lifetime protocol can be checked
        rather than assumed. */
    int liveClips = 0;

    struct CountedClip : public ReferenceClip
    {
        CountedClip() { ++liveClips; }
        ~CountedClip() override { --liveClips; }
    };

    /** Every sample carries its own index, so a mis-mapped timeline shows up as
        a wrong number rather than as a plausible-sounding noise. */
    ReferenceClip::Ptr makeRampClip (int numSamples, double sampleRate = 48000.0)
    {
        auto* clip = new CountedClip();
        clip->playbackSampleRate = sampleRate;
        clip->sourceSampleRate   = sampleRate;
        clip->sourceChannels     = 2;
        clip->audio.setSize (2, numSamples);

        for (int n = 0; n < numSamples; ++n)
        {
            clip->audio.setSample (0, n, (float) n);
            clip->audio.setSample (1, n, (float) -n);
        }

        return clip;
    }
}

static void testReferencePlayer()
{
    section ("ReferencePlayer: timeline mapping");

    constexpr int clipLength = 1000;
    constexpr int blockSize  = 64;

    ReferencePlayer player;
    player.publish (makeRampClip (clipLength));
    player.swapInPendingClip();

    juce::AudioBuffer<float> destination (2, blockSize);

    // Timeline sample 0 must be reference sample 0. This is the property the
    // null test in a DAW is checking, asserted here where it can be checked
    // exactly instead of by ear.
    {
        player.read (destination, 0, blockSize, { false });

        bool exact = true;

        for (int n = 0; n < blockSize; ++n)
            if (! sameBits (destination.getSample (0, n), (float) n)
                    || ! sameBits (destination.getSample (1, n), (float) -n))
                exact = false;

        expectTrue (exact, "timeline 0 maps to reference sample 0", "sample for sample");
    }

    // An arbitrary offset must land on exactly that sample, with no rounding
    // and no interpolation.
    {
        player.read (destination, 137, blockSize, { false });
        expectNear (destination.getSample (0, 0), 137.0, 0.0, "offset 137 reads sample 137");
        expectNear (destination.getSample (0, 63), 200.0, 0.0, "and stays aligned across the block");
    }

    // Before the reference starts: silence up to the start, then the file.
    {
        player.read (destination, -20, blockSize, { false });
        expectNear (destination.getSample (0, 0),  0.0, 0.0, "before the start is silent");
        expectNear (destination.getSample (0, 19), 0.0, 0.0, "silent right up to the start");
        expectNear (destination.getSample (0, 20), 0.0, 0.0, "reference sample 0 is itself zero here");
        expectNear (destination.getSample (0, 21), 1.0, 0.0, "then the reference begins");
    }

    // Past the end: silence, not a wrap, when loop is off.
    {
        player.read (destination, clipLength - 10, blockSize, { false });
        expectNear (destination.getSample (0, 9),  (double) clipLength - 1, 0.0, "last sample is played");
        expectNear (destination.getSample (0, 10), 0.0, 0.0, "past the end is silent");
        expectNear (destination.getSample (0, 63), 0.0, 0.0, "and stays silent");
    }

    // With loop on it wraps, and negative positions wrap too rather than
    // reading backwards off the front of the buffer.
    //
    // A repeat is one crossfade shorter than the file - 250 samples here, since
    // the 10 ms fade is capped at a quarter of a 1000 sample clip - so the period
    // is 750 and the positions below are chosen clear of the seam. The seam
    // itself is checked in the loop-region section.
    {
        constexpr int period = 750;

        player.read (destination, 700, blockSize, { true });
        expectNear (destination.getSample (0, 0), 700.0, 0.0,
                    "the first pass plays the file as it is");

        player.read (destination, 700 + period, blockSize, { true });
        expectNear (destination.getSample (0, 0), 700.0, 0.0,
                    "and one period later it repeats exactly");

        player.read (destination, 700 - period, blockSize, { true });
        expectNear (destination.getSample (0, 0), 700.0, 0.0,
                    "a negative position wraps from the end");
    }

    section ("ReferencePlayer: lifetime");

    // Nine swaps with no collection: eight retire slots fill, the ninth is
    // declined, and the audio thread keeps playing something valid throughout.
    // Nothing may be freed until the message thread says so.
    {
        const int before = liveClips;

        for (int i = 0; i < 9; ++i)
        {
            player.publish (makeRampClip (clipLength));
            player.swapInPendingClip();
        }

        expectTrue (liveClips > before,
                    "nothing is freed on the audio thread",
                    juce::String (liveClips).toStdString() + " clips still alive");

        expectTrue (player.getActiveClip() != nullptr,
                    "a declined swap keeps the previous clip playing");

        player.collectRetiredClips();
        player.swapInPendingClip();

        expectTrue (player.getActiveClip() != nullptr,
                    "and the swap succeeds once slots are free again");
    }

    // A clip replaced before the audio thread ever collected it is freed by the
    // thread that still owns it, not leaked and not double-freed.
    {
        const int before = liveClips;
        player.publish (makeRampClip (10));
        player.publish (makeRampClip (10));
        expectTrue (liveClips == before + 1,
                    "a clip replaced before pickup is freed by the publisher");
        player.swapInPendingClip();
        player.collectRetiredClips();
    }

    player.releaseAllClips();
    player.collectRetiredClips();

    expectTrue (liveClips == 0, "releaseAllClips frees everything",
                juce::String (liveClips).toStdString() + " left alive");
}

//==============================================================================
namespace
{
    /** One inside the region, zero everywhere else. Any output sample that dips
        below one is proof the player read a sample outside the selection, which
        is the one thing a loop region must never do. */
    ReferenceClip::Ptr makeGatedClip (int numSamples, int regionStart, int regionEnd,
                                      double sampleRate = 48000.0)
    {
        auto* clip = new CountedClip();
        clip->playbackSampleRate = sampleRate;
        clip->sourceSampleRate   = sampleRate;
        clip->sourceChannels     = 2;
        clip->audio.setSize (2, numSamples);
        clip->audio.clear();

        for (int ch = 0; ch < 2; ++ch)
            for (int n = regionStart; n < regionEnd; ++n)
                clip->audio.setSample (ch, n, 1.0f);

        return clip;
    }

    /** A sine whose phase does not line up across the loop point, so a raw wrap
        would produce a real step to hide. */
    ReferenceClip::Ptr makeSineClip (int numSamples, double frequency, double sampleRate)
    {
        auto* clip = new CountedClip();
        clip->playbackSampleRate = sampleRate;
        clip->sourceSampleRate   = sampleRate;
        clip->sourceChannels     = 2;
        clip->audio.setSize (2, numSamples);

        for (int n = 0; n < numSamples; ++n)
        {
            const auto value = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi
                                                          * frequency * (double) n / sampleRate));

            for (int ch = 0; ch < 2; ++ch)
                clip->audio.setSample (ch, n, value);
        }

        return clip;
    }

    /** Reads one block and returns it, so a test can compare two reads without
        four lines of buffer plumbing each time. */
    juce::AudioBuffer<float> readBlock (ReferencePlayer& player, int64_t position, int numSamples,
                                        const ReferencePlayer::LoopRegion& loop)
    {
        juce::AudioBuffer<float> destination (2, numSamples);
        player.read (destination, position, numSamples, loop);
        return destination;
    }
}

static void testLoopRegion()
{
    section ("Loop region: mapping");

    // 48 kHz throughout, because the crossfade is 10 ms and its length in samples
    // is the thing every assertion below has to reason about.
    constexpr double rate = 48000.0;
    constexpr int clipLength = 96000;      // two seconds
    constexpr int regionStart = 4800;      // 0.1 s
    constexpr int regionEnd   = 9600;      // 0.2 s, so the region is 4800 long
    constexpr int fade = 480;              // 10 ms, well under a quarter of the region
    constexpr int period = regionEnd - regionStart - fade;   // a repeat is that much shorter

    const ReferencePlayer::LoopRegion region { true, regionStart, regionEnd };

    {
        ReferencePlayer player;
        player.publish (makeRampClip (clipLength, rate));
        player.swapInPendingClip();

        // The region restarts at timeline zero. This is the decision the whole
        // feature turns on: a selected region is all B plays, wherever the host's
        // transport happens to be.
        auto block = readBlock (player, 0, 64, region);
        expectNear (block.getSample (0, 0), (double) regionStart, 0.0,
                    "timeline 0 reads the region's first sample");
        expectNear (block.getSample (0, 63), (double) regionStart + 63, 0.0,
                    "and stays aligned across the block");

        // The whole of the first pass is the selection, untouched.
        block = readBlock (player, period - 1, 1, region);
        expectNear (block.getSample (0, 0), (double) regionStart + period - 1, 0.0,
                    "the first pass plays the selection exactly as drawn");

        // Repeats are periodic to the sample - which is what makes the mapping
        // safe to hand a host that jumps around its own timeline.
        const auto once  = readBlock (player, fade + 100, 64, region);
        const auto twice = readBlock (player, fade + 100 + period, 64, region);

        bool periodic = true;

        for (int n = 0; n < 64; ++n)
            if (! sameBits (once.getSample (0, n), twice.getSample (0, n)))
                periodic = false;

        expectTrue (periodic, "a repeat is periodic to the sample");

        // A negative timeline position wraps into the region rather than reading
        // backwards off the front of the buffer.
        block = readBlock (player, -(int64_t) period + 1000, 1, region);
        expectNear (block.getSample (0, 0), (double) regionStart + 1000, 0.0,
                    "a negative position wraps into the region");

        // The mapping is a pure function of position: no state carried between
        // blocks, so the same position always produces the same samples. That is
        // what lets a host loop, scrub or jump without the reference drifting.
        const auto first  = readBlock (player, 123456, 512, region);
        const auto second = readBlock (player, 123456, 512, region);

        bool identical = true;

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
                if (! sameBits (first.getSample (ch, n), second.getSample (ch, n)))
                    identical = false;

        expectTrue (identical, "reading the same position twice is bit-identical");

        // Block boundaries must not shift anything: one 512 sample read has to
        // equal eight 64 sample reads over the same span, seam included.
        bool joins = true;

        for (int part = 0; part < 8; ++part)
        {
            const auto piece = readBlock (player, period - 200 + part * 64, 64, region);

            for (int n = 0; n < 64; ++n)
                if (! sameBits (piece.getSample (0, n),
                                readBlock (player, period - 200, 512, region)
                                    .getSample (0, part * 64 + n)))
                    joins = false;
        }

        expectTrue (joins, "block size does not change what is read");
    }

    section ("Loop region: reads nothing outside the selection");

    {
        ReferencePlayer player;
        player.publish (makeGatedClip (clipLength, regionStart, regionEnd, rate));
        player.swapInPendingClip();

        // Three full cycles, seams and all. Everything inside the region is 1.0
        // and everything outside is 0.0, so a single sample below 1.0 anywhere in
        // the output means a sample from outside the selection was played.
        float lowest = 2.0f, highest = 0.0f;

        for (int64_t position = 0; position < 3 * (regionEnd - regionStart); position += 512)
        {
            const auto block = readBlock (player, position, 512, region);

            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 512; ++n)
                {
                    lowest  = juce::jmin (lowest,  block.getSample (ch, n));
                    highest = juce::jmax (highest, block.getSample (ch, n));
                }
        }

        expectTrue (lowest >= 0.999f, "never reads a sample from outside the region",
                    "lowest sample " + juce::String (lowest, 4).toStdString());

        // The ceiling is 1.414, not 1.0, and that is the equal-power law rather
        // than a bug: two identical signals crossfaded by cos and sin sum to
        // sqrt(2) at the midpoint. Real programme either side of a loop point is
        // not identical, which is exactly the case equal power is right for - see
        // ABEngine.h for the same argument about the A/B switch.
        expectWithin (highest, 1.0, 1.4143, "the seam stays within the equal-power ceiling");
    }

    section ("Loop region: no click at the seam");

    {
        // 440.3 Hz over a one second region is 440.3 cycles: three tenths of a
        // cycle unaccounted for at the wrap, which is a large step to hide.
        constexpr int loopStart = 48000;
        constexpr int loopEnd   = 96000;

        ReferencePlayer player;
        auto clip = makeSineClip (144000, 440.3, rate);
        const float firstSample = clip->audio.getSample (0, loopStart);
        const float lastSample  = clip->audio.getSample (0, loopEnd - 1);

        player.publish (clip);
        player.swapInPendingClip();

        const ReferencePlayer::LoopRegion sineRegion { true, loopStart, loopEnd };

        const double rawStep = std::abs (firstSample - lastSample);

        // The largest sample-to-sample step inside the body, for scale. A sine
        // has one, and the seam is only acceptable if it is not much worse.
        const auto body = readBlock (player, 1000, 4096, sineRegion);
        double bodyStep = 0.0;

        for (int n = 1; n < 4096; ++n)
            bodyStep = juce::jmax (bodyStep, (double) std::abs (body.getSample (0, n) - body.getSample (0, n - 1)));

        // Now across the wrap, with the crossfade doing its work. The read spans
        // the end of the first pass and the start of the repeat, so the seam is
        // in the middle of it.
        constexpr int seamPeriod = (loopEnd - loopStart) - 480;
        const auto seam = readBlock (player, seamPeriod - 2048, 4096, sineRegion);
        double seamStep = 0.0;

        for (int n = 1; n < 4096; ++n)
            seamStep = juce::jmax (seamStep, (double) std::abs (seam.getSample (0, n) - seam.getSample (0, n - 1)));

        expectTrue (rawStep > 20.0 * bodyStep, "the test signal really does have a step to hide",
                    "raw wrap step " + juce::String (rawStep, 4).toStdString()
                      + " vs body step " + juce::String (bodyStep, 4).toStdString());

        // Twice the material's own largest step, not once: inside the blend two
        // sines are sliding past each other and their slopes can briefly add.
        // Against a raw wrap step of twenty-odd times that, this is still the
        // difference between a click and no click.
        expectWithin (seamStep, 0.0, 2.0 * bodyStep,
                      "the crossfade leaves no step the material could not have made");
    }

    section ("Loop region: degenerate cases");

    {
        ReferencePlayer player;
        player.publish (makeRampClip (1000, rate));
        player.swapInPendingClip();

        // Both parameters at zero - the default, and what every session saved
        // before regions existed restores to - has to mean the whole file.
        const auto whole = readBlock (player, 0, 64, { true, 0, 0 });
        const auto full  = readBlock (player, 0, 64, { true, 0, 1000 });

        bool same = true;

        for (int n = 0; n < 64; ++n)
            if (! sameBits (whole.getSample (0, n), full.getSample (0, n)))
                same = false;

        expectTrue (same, "an empty region means the whole file");

        // A region pointing past the end of a shorter reference is clamped, not
        // refused: swapping in a shorter file should play what there is.
        const auto clamped = readBlock (player, 0, 64, { true, 900, 99999 });
        expectNear (clamped.getSample (0, 0), 900.0, 0.0,
                    "a region past the end of the file is clamped to it");

        // An inverted region is not a region.
        const auto inverted = readBlock (player, 0, 64, { true, 800, 200 });
        expectNear (inverted.getSample (0, 0), 0.0, 0.0,
                    "an inverted region falls back to the whole file");

        // And with Loop off the region is ignored entirely - which is what keeps
        // the README's bit-exact null test a statement about today's build.
        const auto off = readBlock (player, 0, 64, { false, 400, 600 });
        expectNear (off.getSample (0, 0),  0.0, 0.0, "with Loop off the region is ignored");
        expectNear (off.getSample (0, 63), 63.0, 0.0, "and the timeline mapping is untouched");
    }
}

//==============================================================================
static void testResampler()
{
    section ("Resampling");

    // 1 kHz from 48 to 96 kHz. The absolute test is not "does it sound right"
    // but "is sample n still at the time sample n represents" - a resampler
    // that forgot to compensate its own group delay passes every spectral check
    // and still puts the reference a millisecond late against the master.
    constexpr double sourceRate = 48000.0;
    constexpr double targetRate = 96000.0;
    constexpr double frequency  = 1000.0;

    juce::AudioBuffer<float> source (2, (int) sourceRate);
    fillSine (source, sourceRate, frequency, 0.5);

    const auto resampled = ReferenceLoader::resample (source, sourceRate, targetRate);

    expectNear (resampled.getNumSamples(), sourceRate * 2.0, 1.0, "length scales with the rate");

    // Compare against the sine the output should be, over the middle of the
    // signal so that neither edge's filter ramp is under test here.
    double worstError = 0.0;

    for (int n = (int) targetRate / 10; n < resampled.getNumSamples() - (int) targetRate / 10; ++n)
    {
        const double expected = 0.5 * std::cos (juce::MathConstants<double>::twoPi * frequency * n / targetRate);
        worstError = juce::jmax (worstError, std::abs ((double) resampled.getSample (0, n) - expected));
    }

    expectWithin (worstError, 0.0, 0.001, "phase and amplitude survive the conversion");

    // Gain and timing across every rate pair this plugin will realistically
    // meet. Both properties were measured before they were asserted: the gain
    // came back a flat 0.99 - 0.087 dB, identical at 50 Hz and 5 kHz, so a
    // kernel normalisation rather than passband droop - and the timing came
    // back exactly zero for integer ratios and a fraction of a sample for the
    // others. The tolerances below are what the code actually delivers, not a
    // margin picked to make the test pass.
    struct RatePair { double from, to; };

    for (const auto pair : { RatePair { 48000.0, 96000.0 },
                             RatePair { 44100.0, 48000.0 },
                             RatePair { 48000.0, 44100.0 },
                             RatePair { 96000.0, 48000.0 } })
    {
        for (const double tone : { 50.0, 1000.0, 5000.0 })
        {
            juce::AudioBuffer<float> in (2, (int) pair.from);
            fillSine (in, pair.from, tone, 0.5);

            const auto out = ReferenceLoader::resample (in, pair.from, pair.to);

            const double omega = juce::MathConstants<double>::twoPi * tone / pair.to;
            double sine = 0.0, cosine = 0.0;
            const int low  = out.getNumSamples() / 10;
            const int high = out.getNumSamples() - out.getNumSamples() / 10;

            for (int n = low; n < high; ++n)
            {
                cosine += out.getSample (0, n) * std::cos (omega * n);
                sine   += out.getSample (0, n) * std::sin (omega * n);
            }

            const double amplitude = 2.0 * std::hypot (cosine, sine) / (high - low);
            const double delaySamples = std::atan2 (-sine, cosine) / omega;

            char label[128];

            std::snprintf (label, sizeof (label), "%.1f -> %.1f kHz, %g Hz: unity gain",
                           pair.from / 1000.0, pair.to / 1000.0, tone);
            expectNear (juce::Decibels::gainToDecibels (amplitude / 0.5), 0.0, 0.005, label);

            // Half an output sample is the most the integer rounding of the
            // latency compensation can be out by. At 48 kHz that is 10 us -
            // three orders of magnitude below anything an A/B comparison can
            // resolve, and it only happens for non-integer rate ratios.
            std::snprintf (label, sizeof (label), "%.1f -> %.1f kHz, %g Hz: aligned to the sample",
                           pair.from / 1000.0, pair.to / 1000.0, tone);
            expectWithin (std::abs (delaySamples), 0.0, 0.5, label);
        }
    }


    // The same test with the delay compensation removed would fail by roughly
    // 0.5 * sin(2*pi*1000*100/48000) = 0.48, so this tolerance really is
    // measuring the compensation and not just the interpolator's quality.
    expectTrue (worstError < 0.01,
                "group delay is compensated, not merely small",
                "an uncompensated 100-sample delay would give ~0.48 here");
}

//==============================================================================
namespace
{
    struct CaptureListener : public ReferenceLoader::Listener
    {
        void referenceLoadStarted (int, const juce::File&) override { started = true; }

        void referenceLoadFinished (int loadedSlot,
                                    ReferenceClip::Ptr loaded,
                                    const juce::File& loadedFile,
                                    const juce::String& text) override
        {
            slot = loadedSlot;
            clip = loaded;
            file = loadedFile;
            message = text;
            finished = true;
        }

        int slot = -1;

        bool started = false, finished = false;
        ReferenceClip::Ptr clip;
        juce::File file;
        juce::String message;
    };

    /** A sine at -23 dBFS is convenient twice over: its loudness is -23 LUFS by
        the EBU cases above, and because a sine's amplitude *is* its dBFS under
        that convention, its peak is -23 dBFS too. One signal, two numbers that
        should both come back as the same figure. */
    bool writeTestWav (const juce::File& file, double sampleRate, int channels,
                       double seconds, double level)
    {
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return false;

        // The writer takes the stream out of the unique_ptr on success, so there
        // is no release() to forget here and no way to double-own the stream.
        auto writer = format.createWriterFor (stream,
                                              juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (sampleRate)
                                                  .withNumChannels (channels)
                                                  .withBitsPerSample (24));

        if (writer == nullptr)
            return false;

        const int total = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> buffer (channels, total);
        const double increment = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        const double amplitude = std::pow (10.0, level / 20.0);

        for (int n = 0; n < total; ++n)
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, n, (float) (amplitude * std::cos (increment * n)));

        // A 20 ms fade at each end, and it is not cosmetic. Written without it
        // this file steps from digital silence to full amplitude at sample 0,
        // and a sample-rate conversion rings at that step the way any filter
        // rings at a step: the 44.1 -> 48 kHz version of an un-faded file
        // measures 0.72 dB hotter than the original at the sample grid alone,
        // before true peak is even considered. That is real - it is genuinely
        // in the converted audio, and the meter is right to report it - but it
        // is a property of a file trimmed mid-waveform, not of the loader.
        // Music fades in from silence, so the test signal does too.
        const int fade = (int) (sampleRate * 0.02);
        buffer.applyGainRamp (0, fade, 0.0f, 1.0f);
        buffer.applyGainRamp (total - fade, fade, 1.0f, 0.0f);

        return writer->writeFromAudioSampleBuffer (buffer, 0, total);
    }

    void pumpUntilFinished (CaptureListener& listener, int timeoutMs)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! listener.finished && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

static void testEndToEndLoad()
{
    section ("ReferenceLoader end to end");

    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("ABReferenceCheck");
    directory.createDirectory();

    // The case the plugin will meet most often: a 44.1 kHz reference dropped
    // into a 48 kHz session. Everything the loader does happens here at once -
    // decode, fold, resample, measure - and the answer is known in advance.
    {
        auto file = directory.getChildFile ("stereo441.wav");

        if (! writeTestWav (file, 44100.0, 2, 8.0, -23.0))
        {
            expectTrue (false, "could not write the test file", file.getFullPathName().toStdString());
            return;
        }

        CaptureListener listener;
        ReferenceLoader loader (listener);
        loader.setPlaybackSampleRate (48000.0);
        loader.loadFile (0, file);
        pumpUntilFinished (listener, 15000);

        expectTrue (listener.finished, "the load completes");
        expectTrue (listener.clip != nullptr, "a clip comes back",
                    listener.message.toStdString());

        if (listener.clip != nullptr)
        {
            const auto& clip = *listener.clip;

            expectNear (clip.sourceSampleRate,   44100.0, 0.5, "the source rate is reported");
            expectNear (clip.playbackSampleRate, 48000.0, 0.5, "the clip is at the session rate");
            expectTrue (clip.audio.getNumChannels() == 2, "the clip is stereo");
            expectNear (clip.getLengthSeconds(), 8.0, 0.01, "the length survives the conversion");

            // The measurement is taken after resampling, so this is the number
            // the level match will actually use - and it has to agree with what
            // any other meter says about the file, or the match is matching to
            // a fiction.
            expectNear (clip.integratedLufs, -23.0, 0.1, "measured loudness matches the file");
            expectNear (clip.truePeakDb,     -23.0, 0.1, "measured true peak matches the file");
        }
    }

    // A mono reference is duplicated, not attenuated. If it were halved to
    // "conserve energy" the loudness would come back 3 dB low and the level
    // match would spend its whole life correcting an error of the plugin's own
    // making.
    {
        auto file = directory.getChildFile ("mono48.wav");
        writeTestWav (file, 48000.0, 1, 8.0, -23.0);

        CaptureListener listener;
        ReferenceLoader loader (listener);
        loader.setPlaybackSampleRate (48000.0);
        loader.loadFile (0, file);
        pumpUntilFinished (listener, 15000);

        expectTrue (listener.clip != nullptr, "a mono file loads");

        if (listener.clip != nullptr)
        {
            expectTrue (listener.clip->audio.getNumChannels() == 2, "mono is folded up to stereo");
            expectTrue (listener.clip->sourceChannels == 1, "and the UI is told it was mono");
            expectNear (listener.clip->integratedLufs, -23.0, 0.1,
                        "mono duplicated reads the same loudness as stereo");
        }
    }

    // An unreadable file has to fail with something a person can act on, not
    // with silence and not with a crash.
    {
        auto file = directory.getChildFile ("notaudio.wav");
        file.replaceWithText ("this is not a wav file");

        CaptureListener listener;
        ReferenceLoader loader (listener);
        loader.setPlaybackSampleRate (48000.0);
        loader.loadFile (0, file);
        pumpUntilFinished (listener, 5000);

        expectTrue (listener.finished && listener.clip == nullptr,
                    "an unreadable file fails cleanly");
        expectTrue (listener.message.isNotEmpty(),
                    "and says why", listener.message.toStdString());
    }

    directory.deleteRecursively();
}

//==============================================================================
namespace
{
    /** A clip of one constant value, so a gain applied to it can be read
        straight off the output. */
    ReferenceClip::Ptr makeFlatClipImpl (float value, float lufs, int numSamples)
    {
        auto* clip = new CountedClip();
        clip->playbackSampleRate = 48000.0;
        clip->sourceSampleRate   = 48000.0;
        clip->sourceChannels     = 2;
        clip->integratedLufs     = lufs;
        clip->audio.setSize (2, numSamples);

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (clip->audio.getWritePointer (ch), value, numSamples);

        return clip;
    }

    /** A clip whose audio level and whose declared loudness are set from the
        same number. Deliberately not two independent arguments: the first
        version of this harness passed a clip the same audio with two different
        declared loudnesses, which made the level-match case assert something
        untrue of any real file - and it duly "failed" against correct code. */
    ReferenceClip::Ptr makeClipAt (float lufs, int numSamples = 4096)
    {
        return makeFlatClipImpl (0.1f * std::pow (10.0f, (lufs + 20.0f) / 20.0f), lufs, numSamples);
    }

    std::array<ReferencePlayer::LoopRegion, ABParams::numSlots> noRegions() { return {}; }
}

static void testReferenceBus()
{
    section ("ReferenceBus: three slots");

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Each slot carries its own gain. Two references genuinely 10 LU apart, each
    // given the gain that matches it to the same A, must come out at the same
    // level - that is the entire point of doing the gain per slot, and one
    // shared gain would leave them 10 dB apart.
    {
        ReferenceBus bus;
        bus.prepare (sampleRate, blockSize);

        bus.publish (0, makeClipAt (-20.0f));
        bus.publish (1, makeClipAt (-30.0f));
        bus.swapInPendingClips();

        // Matching both to A at -20 LUFS: slot 1 needs nothing, slot 2 needs +10 dB.
        bus.setSlotGain (0, juce::Decibels::decibelsToGain (0.0f),  false);
        bus.setSlotGain (1, juce::Decibels::decibelsToGain (10.0f), false);

        juce::AudioBuffer<float> out (2, blockSize);
        const auto regions = noRegions();

        bus.reset (0);
        bus.read (out, 0, blockSize, regions);
        const double levelOne = std::abs (out.getSample (0, blockSize - 1));

        bus.reset (1);
        bus.read (out, 0, blockSize, regions);
        const double levelTwo = std::abs (out.getSample (0, blockSize - 1));

        expectNear (juce::Decibels::gainToDecibels (levelTwo / levelOne), 0.0, 0.01,
                    "two references 10 LU apart come out level-matched");
    }

    // The slot crossfade obeys the same law as the A/B one, and for the same
    // reason: two references are uncorrelated, so their powers add.
    {
        const int totalSamples = (int) (sampleRate * 0.02);

        auto runFade = [&] (bool measureOutgoing)
        {
            ReferenceBus bus;
            bus.prepare (sampleRate, blockSize);
            bus.publish (0, makeFlatClipImpl (measureOutgoing ? 1.0f : 0.0f, -20.0f, totalSamples + blockSize));
            bus.publish (1, makeFlatClipImpl (measureOutgoing ? 0.0f : 1.0f, -20.0f, totalSamples + blockSize));
            bus.swapInPendingClips();
            bus.setSlotGain (0, 1.0f, false);
            bus.setSlotGain (1, 1.0f, false);
            bus.reset (0);
            bus.setSlot (1);

            std::vector<float> captured;
            juce::AudioBuffer<float> out (2, blockSize);
            const auto regions = noRegions();

            for (int done = 0; done < totalSamples; done += blockSize)
            {
                bus.read (out, done, blockSize, regions);

                for (int n = 0; n < blockSize; ++n)
                    captured.push_back (out.getSample (0, n));
            }

            return captured;
        };

        const auto outgoing = runFade (true);
        const auto incoming = runFade (false);

        double worstError = 0.0, worstStep = 0.0;

        for (size_t n = 0; n < outgoing.size(); ++n)
        {
            const double power = (double) outgoing[n] * outgoing[n]
                                   + (double) incoming[n] * incoming[n];
            worstError = juce::jmax (worstError, std::abs (power - 1.0));

            if (n > 0)
                worstStep = juce::jmax (worstStep, std::abs ((double) incoming[n] - incoming[n - 1]));
        }

        expectNear (worstError, 0.0, 1e-5, "slot fade: gain_out^2 + gain_in^2 == 1 throughout");
        expectWithin (worstStep, 0.0, 0.006, "slot fade: no step discontinuity");
        expectTrue (outgoing.front() > 0.99f && outgoing.back() < 0.01f,
                    "slot fade starts on the old slot and ends on the new one");
    }

    // An empty slot is silence, not the previous slot left playing on.
    {
        ReferenceBus bus;
        bus.prepare (sampleRate, blockSize);
        bus.publish (0, makeFlatClipImpl (0.5f, -20.0f, 4096));
        bus.swapInPendingClips();
        bus.setSlotGain (1, 1.0f, false);
        bus.reset (1);

        juce::AudioBuffer<float> out (2, blockSize);
        out.clear();
        bus.read (out, 0, blockSize, noRegions());

        expectNear (out.getMagnitude (0, blockSize), 0.0, 0.0, "an empty slot reads as silence");
    }

    // The claim the whole plugin rests on, now that a second kind of switch
    // exists: walking 1-2-3 while the output is A must leave A alone. It holds
    // by construction - the bus only ever writes the reference buffer, and the
    // on-A path in ABEngine does not read it - but composition is exactly where
    // a construction argument stops being a guarantee, so it is asserted.
    {
        ABEngine engine;
        engine.prepare (sampleRate);
        engine.reset (false);
        engine.setTarget (false);

        ReferenceBus bus;
        bus.prepare (sampleRate, blockSize);

        for (int slot = 0; slot < ABParams::numSlots; ++slot)
        {
            bus.publish (slot, makeFlatClipImpl (0.9f, -20.0f, blockSize * 8));
            bus.setSlotGain (slot, 4.0f, false);
        }

        bus.swapInPendingClips();

        juce::Random random (99);
        juce::AudioBuffer<float> main (2, blockSize), original (2, blockSize), reference (2, blockSize);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
                main.setSample (ch, n, random.nextFloat() * 2.0f - 1.0f);

        original.makeCopyOf (main);

        bool identical = true;

        for (int pass = 0; pass < 6 && identical; ++pass)
        {
            bus.setSlot (pass % ABParams::numSlots);
            bus.read (reference, 0, blockSize, noRegions());
            engine.process (main, reference, blockSize);

            for (int ch = 0; ch < 2 && identical; ++ch)
                for (int n = 0; n < blockSize; ++n)
                    if (! sameBits (main.getSample (ch, n), original.getSample (ch, n)))
                    {
                        identical = false;
                        break;
                    }
        }

        expectTrue (identical, "A survives slot switching bit for bit",
                    "six switches with a loud reference on the other side");
    }

    expectTrue (liveClips == 0, "the bus frees every slot's clips",
                juce::String (liveClips).toStdString() + " left alive");
}

//==============================================================================
int main (int, char**)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf ("\n\033[1mAB Reference - MeterCheck\033[0m\n");
    std::printf ("Meters and A/B engine against BS.1770-4, EBU Tech 3341, and first principles.\n");

    testCoefficientDerivation();
    testSineLevels();
    testGating();
    testWindowLengths();
    testTruePeak();
    testCrossfade();
    testReferencePlayer();
    testReferenceBus();
    testLoopRegion();
    testResampler();
    testEndToEndLoad();

    std::printf ("\n%d checks, \033[%sm%d failed\033[0m\n\n",
                 checks, failures == 0 ? "32" : "31", failures);

    return failures == 0 ? 0 : 1;
}

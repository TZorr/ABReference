//
//  EditorShot.cpp
//  AB Reference
//
//  Builds the plugin headlessly, opens its editor, and paints it into a PNG.
//
//  Two reasons this is a target and not a one-off script. The first is plain
//  smoke testing: constructing the processor, preparing it, running a block
//  through it and constructing the editor exercises a lot of code that auval
//  never reaches, and it does so in a form that fails loudly instead of hanging
//  a window somewhere nobody is looking.
//
//  The second is that it renders. A plugin panel is laid out with arithmetic on
//  rectangles, and arithmetic on rectangles is exactly the kind of thing that is
//  obviously right while reading it and obviously wrong once drawn. Producing an
//  actual image means the layout can be checked without a DAW, without a screen
//  recording permission, and without a human having to describe what they see.
//

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BalanceDisplay.h"
#include "WaveformDisplay.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "ParameterIds.h"

#include <bit>
#include <cstdint>
#include <cstdio>

namespace
{
    /** A reference file to photograph the panel with. Synthesised rather than
        shipped, because the one thing this target must not need is an asset
        somebody has to remember to keep next to it - and because a file built
        here can be given exactly the shape that makes a waveform worth looking
        at: quiet, loud, quiet again, with the sections at obvious boundaries. */
    /** A 24-bit stereo WAV in the temp folder, at 48 kHz. */
    juce::File writeWav (const juce::AudioBuffer<float>& audio, const juce::String& name)
    {
        constexpr double rate = 48000.0;

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile (name + ".wav");

        juce::WavAudioFormat format;
        auto fileStream = file.createOutputStream();

        if (fileStream == nullptr)
            return {};

        // See writeDemoReference: an existing file is opened at its end.
        fileStream->setPosition (0);
        fileStream->truncate();

        std::unique_ptr<juce::OutputStream> stream (std::move (fileStream));

        auto writer = format.createWriterFor (stream,
                                              juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (rate)
                                                  .withNumChannels (2)
                                                  .withBitsPerSample (24));

        if (writer == nullptr)
            return {};

        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        return file;
    }

    /** Stereo pink noise whose low and high ends move from section to section
        while the mids stay put - roughly what a real master does, and what
        gives the reference corridor its tub shape. `bassDb` and `airDb` shift
        the whole thing, for an A that differs from it. */
    juce::AudioBuffer<float> makeBalanceDemo (double seconds, float bassDb, float airDb, int seed)
    {
        constexpr double rate = 48000.0;
        juce::AudioBuffer<float> audio (2, (int) (rate * seconds));
        juce::Random random (seed);

        // Per 3 s section: extra bass and extra air, in dB.
        static const float sectionBass[] { 0.0f, 4.0f, -2.0f, 6.0f, 1.0f, -3.0f, 3.0f, 5.0f, -1.0f, 2.0f };
        static const float sectionAir[]  { 0.0f, -3.0f, 2.0f, 1.0f, -4.0f, 3.0f, -1.0f, 2.0f, 0.0f, -2.0f };

        const double lowCoefficient  = std::exp (-juce::MathConstants<double>::twoPi * 150.0 / rate);
        const double highCoefficient = std::exp (-juce::MathConstants<double>::twoPi * 6000.0 / rate);

        for (int ch = 0; ch < 2; ++ch)
        {
            double b0 = 0.0, b1 = 0.0, b2 = 0.0, low = 0.0, belowAir = 0.0;
            auto* out = audio.getWritePointer (ch);

            for (int n = 0; n < audio.getNumSamples(); ++n)
            {
                const double white = random.nextDouble() * 2.0 - 1.0;
                b0 = 0.99765 * b0 + white * 0.0990460;
                b1 = 0.96300 * b1 + white * 0.2965164;
                b2 = 0.57000 * b2 + white * 1.0526913;
                const double pink = (b0 + b1 + b2 + white * 0.1848) * 0.05;

                low      = (1.0 - lowCoefficient)  * pink + lowCoefficient  * low;
                belowAir = (1.0 - highCoefficient) * pink + highCoefficient * belowAir;
                const double air = pink - belowAir;

                const auto section = (size_t) ((n / (int) (rate * 3.0)) % 10);
                const double bass = std::pow (10.0, (sectionBass[section] + bassDb) / 20.0) - 1.0;
                const double top  = std::pow (10.0, (sectionAir[section]  + airDb)  / 20.0) - 1.0;

                out[n] = (float) (0.5 * (pink + bass * low + top * air));
            }
        }

        return audio;
    }

    juce::File writeDemoReference (const juce::String& name = "ABReferenceDemo")
    {
        constexpr double rate = 48000.0;
        constexpr int seconds = 30;
        const int length = (int) (rate * seconds);

        juce::AudioBuffer<float> audio (2, length);

        for (int n = 0; n < length; ++n)
        {
            const double t = (double) n / rate;

            // Four sections at different levels, each with a slow tremolo so the
            // envelope has texture instead of being four rectangles.
            const double section = t < 6.0 ? 0.18 : t < 14.0 ? 0.72 : t < 22.0 ? 0.42 : 0.85;
            const double shape = 0.65 + 0.35 * std::sin (juce::MathConstants<double>::twoPi * 1.7 * t);

            // Scaled so the file lands near the -24 LUFS the tone below reads at.
            // A demo reference loud enough to peg the level match would make the
            // shot a picture of the clamp warning rather than of the panel.
            const auto value = (float) (0.19 * section * shape
                                          * std::sin (juce::MathConstants<double>::twoPi * 180.0 * t));

            audio.setSample (0, n, value);
            audio.setSample (1, n, value * 0.85f);
        }

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile (name + ".wav");

        juce::WavAudioFormat format;
        auto fileStream = file.createOutputStream();

        if (fileStream == nullptr)
            return {};

        // Explicitly, rather than by deleting first: createOutputStream opens an
        // existing file at its end, so without this a second run appends to the
        // first one and renders a picture of the previous build. A stale artefact
        // that still looks plausible is the worst kind of verification failure.
        fileStream->setPosition (0);
        fileStream->truncate();

        std::unique_ptr<juce::OutputStream> stream (std::move (fileStream));

        // The writer takes the stream out of the unique_ptr on success, so there
        // is no release() to forget and no way to double-own the stream.
        auto writer = format.createWriterFor (stream,
                                              juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (rate)
                                                  .withNumChannels (2)
                                                  .withBitsPerSample (24));

        if (writer == nullptr)
            return {};

        writer->writeFromAudioSampleBuffer (audio, 0, length);
        return file;
    }

    float readParameter (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return state.getRawParameterValue (id)->load();
    }

    juce::MouseEvent eventAt (juce::Component& component, int x, int clicks)
    {
        const juce::Point<float> position ((float) x, (float) component.getHeight() / 2.0f);

        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 position, juce::ModifierKeys::currentModifiers,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &component, &component,
                                 juce::Time::getCurrentTime(),
                                 position, juce::Time::getCurrentTime(),
                                 clicks, false);
    }

    /** Drags a loop region out with the mouse, the way a person would.

        Setting the two parameters directly would produce the same picture and
        prove nothing: the interesting half of this feature is the gesture, and a
        panel that renders a region it cannot be made to select by hand is a panel
        that passes its own test and fails in a DAW. */
    bool dragLoopRegion (WaveformDisplay& strip, int fromX, int toX)
    {
        strip.mouseDown (eventAt (strip, fromX, 1));

        // In steps, because a drag is a stream of events and the component
        // decides between "a click" and "a selection" partway through one.
        for (int x = fromX; x <= toX; x += 8)
            strip.mouseDrag (eventAt (strip, x, 1));

        strip.mouseDrag (eventAt (strip, toX, 1));
        strip.mouseUp   (eventAt (strip, toX, 1));
        return true;
    }

    //==========================================================================
    // --layouts: the processor on all three buses it accepts, the monitor, and
    // an old session's Mono switch. Here rather than in MeterCheck because this
    // is the only target that links the processor.
    int layoutFailures = 0;

    void expectLayout (bool condition, const char* what)
    {
        std::printf ("  %s  %s\n", condition ? "PASS" : "FAIL", what);

        if (! condition)
            ++layoutFailures;
    }

    bool sameBitsLayout (float a, float b) noexcept
    {
        return std::bit_cast<std::uint32_t> (a) == std::bit_cast<std::uint32_t> (b);
    }

    void fillNoise (juce::AudioBuffer<float>& buffer, int seed, int channels)
    {
        juce::Random random (seed);

        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.setSample (ch, n, (random.nextFloat() * 2.0f - 1.0f) * 0.25f);
    }

    void setPlain (ABReferenceProcessor& processor, const char* id, float value)
    {
        auto* parameter = processor.apvts.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** Prepares a processor on the given bus, loads the demo reference into
        slot 1, plays eight seconds of A - the demo tone, both channels alike -
        and returns the level match it settles on. */
    float settledMatchOn (int inputs, int outputs, const juce::File& reference)
    {
        ABReferenceProcessor processor;

        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add  (inputs  == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (outputs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        processor.setBusesLayout (layout);
        processor.prepareToPlay (48000.0, 512);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        processor.loadReference (0, reference);

        const auto deadline = juce::Time::getMillisecondCounter() + 15000;

        while (processor.getSelectedClip() == nullptr && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        juce::AudioBuffer<float> buffer (juce::jmax (inputs, outputs), 512);
        juce::MidiBuffer midi;

        for (int block = 0; block < 48000 * 8 / 512; ++block)
        {
            for (int n = 0; n < 512; ++n)
            {
                const double t = (block * 512 + n) / 48000.0;
                const auto value = (float) (0.07 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t));

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, n, value);
            }

            processor.processBlock (buffer, midi);
        }

        return processor.getMatchDb();
    }

    int runLayoutChecks()
    {
        std::printf ("Bus layouts, monitor and old sessions:\n");

        using Set = juce::AudioChannelSet;

        auto supports = [] (const Set& in, const Set& out)
        {
            ABReferenceProcessor processor;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (in);
            layout.outputBuses.add (out);
            return processor.checkBusesLayoutSupported (layout);
        };

        expectLayout (supports (Set::stereo(), Set::stereo()), "accepts stereo in, stereo out");
        expectLayout (supports (Set::mono(),   Set::stereo()), "accepts mono in, stereo out");
        expectLayout (supports (Set::mono(),   Set::mono()),   "accepts mono in, mono out");
        expectLayout (! supports (Set::stereo(), Set::mono()), "refuses stereo in, mono out");

        juce::MidiBuffer midi;

        // Mono in, stereo out: the second channel arrives as whatever the host
        // left in it and must leave as a copy of the first.
        {
            ABReferenceProcessor processor;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (Set::mono());
            layout.outputBuses.add (Set::stereo());
            processor.setBusesLayout (layout);
            processor.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> buffer (2, 512), original (2, 512);
            fillNoise (buffer, 1, 2);
            original.makeCopyOf (buffer);
            processor.processBlock (buffer, midi);

            bool left = true, copied = true;

            for (int n = 0; n < 512; ++n)
            {
                left   = left   && sameBitsLayout (buffer.getSample (0, n), original.getSample (0, n));
                copied = copied && sameBitsLayout (buffer.getSample (1, n), original.getSample (0, n));
            }

            expectLayout (left,   "[1,2] on A: the input channel is untouched");
            expectLayout (copied, "[1,2] on A: the right channel is a copy of it, not silence");
        }

        // Mono in, mono out: A through the stereo engine and back is x again.
        {
            ABReferenceProcessor processor;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (Set::mono());
            layout.outputBuses.add (Set::mono());
            processor.setBusesLayout (layout);
            processor.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> buffer (1, 512), original (1, 512);
            fillNoise (buffer, 2, 1);
            original.makeCopyOf (buffer);
            processor.processBlock (buffer, midi);

            bool identical = true;

            for (int n = 0; n < 512; ++n)
                identical = identical && sameBitsLayout (buffer.getSample (0, n), original.getSample (0, n));

            expectLayout (identical, "[1,1] on A: bit for bit");

            // Side of a mono signal is nothing - the one thing a mono Side can say.
            setPlain (processor, ABParams::monitor, (float) ABParams::Monitor::side);

            for (int block = 0; block < 4; ++block)
            {
                fillNoise (buffer, 3 + block, 1);
                processor.processBlock (buffer, midi);
            }

            expectLayout (buffer.getMagnitude (0, 512) < 1.0e-7f, "[1,1] Side of a mono signal is silence");
        }

        // Stereo, Mid: the monitor reaches the output through the real processBlock.
        {
            ABReferenceProcessor processor;
            processor.setPlayConfigDetails (2, 2, 48000.0, 512);
            processor.prepareToPlay (48000.0, 512);
            setPlain (processor, ABParams::monitor, (float) ABParams::Monitor::mid);

            juce::AudioBuffer<float> buffer (2, 512), original (2, 512);

            for (int block = 0; block < 4; ++block)
            {
                fillNoise (buffer, 10 + block, 2);
                original.makeCopyOf (buffer);
                processor.processBlock (buffer, midi);
            }

            double worst = 0.0;

            for (int n = 0; n < 512; ++n)
            {
                const double mid = 0.5 * ((double) original.getSample (0, n) + original.getSample (1, n));
                worst = juce::jmax (worst, std::abs (buffer.getSample (0, n) - mid), std::abs (buffer.getSample (1, n) - mid));
            }

            expectLayout (worst < 1.0e-6, "[2,2] Monitor = Mid gives L = R = (L + R) / 2");
        }

        // An old session with Mono on comes back as Mid; with Mono off, Stereo.
        for (const bool monoWasOn : { true, false })
        {
            ABReferenceProcessor source;
            juce::MemoryBlock block;
            source.getStateInformation (block);

            auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());

            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute ("id") == ABParams::monitor)
                {
                    xml->removeChildElement (child, true);
                    break;
                }

            auto* legacy = xml->createNewChildElement ("PARAM");
            legacy->setAttribute ("id", ABParams::legacyMonoSum);
            legacy->setAttribute ("value", monoWasOn ? 1.0 : 0.0);

            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

            ABReferenceProcessor restored;
            setPlain (restored, ABParams::monitor, (float) ABParams::Monitor::side);   // so a no-op cannot pass
            restored.setStateInformation (old.getData(), (int) old.getSize());

            const auto monitor = (int) restored.apvts.getRawParameterValue (ABParams::monitor)->load();

            expectLayout (monitor == (monoWasOn ? 1 : 0),
                          monoWasOn ? "an old session's Mono on restores as Mid"
                                    : "an old session's Mono off restores as Stereo");
        }

        // The level match on a mono bus is made against the reference's mono
        // fold, so the same material gets the same match whichever bus it is on.
        {
            const auto reference = writeDemoReference ("ABReferenceLayouts");
            const float stereoMatch = settledMatchOn (2, 2, reference);
            const float monoMatch   = settledMatchOn (1, 1, reference);
            const float mixedMatch  = settledMatchOn (1, 2, reference);

            std::printf ("  match: [2,2] %+.3f dB   [1,1] %+.3f dB   [1,2] %+.3f dB\n",
                         stereoMatch, monoMatch, mixedMatch);

            expectLayout (std::abs (stereoMatch) > 0.5f, "the demo reference needs a real match (not a vacuous test)");
            expectLayout (std::abs (monoMatch - stereoMatch) < 0.1f,
                          "[1,1] matches like [2,2] for material that is the same on both channels");
            expectLayout (std::abs (mixedMatch - stereoMatch) < 0.01f, "[1,2] matches exactly like [2,2]");
        }

        std::printf ("%s\n", layoutFailures == 0 ? "All layout checks passed" : "LAYOUT CHECKS FAILED");
        return layoutFailures == 0 ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc > 1 && juce::String (argv[1]) == "--layouts")
        return runLayoutChecks();

    const juce::File output = argc > 1 ? juce::File (juce::String (argv[1]))
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("editor.png");

    ABReferenceProcessor processor;

    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // An optional reference, because the empty panel is the state a user sees
    // for about four seconds and the loaded one is the state they work in.
    // "--demo" builds one, which is how the waveform and the loop region get
    // photographed without a file having to live in the repository.
    const juce::String argument = argc > 2 ? juce::String (argv[2]) : juce::String();
    const bool demo = argument == "--demo";

    if (argc > 2)
    {
        // prepareToPlay leaves the sample rate as a note for the message thread
        // rather than calling the loader from whatever thread it arrived on, so
        // the loop has to turn once before the loader knows what rate to
        // produce clips at. In a plugin the host's own loop does this; here it
        // has to be done by hand.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto reference = demo ? writeDemoReference() : juce::File (argument);

        if (reference == juce::File())
        {
            std::printf ("FAIL: could not build the demo reference\n");
            return 1;
        }

        processor.loadReference (0, reference);

        const auto loadDeadline = juce::Time::getMillisecondCounter() + 15000;

        while (processor.getSelectedClip() == nullptr
               && juce::Time::getMillisecondCounter() < loadDeadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        if (processor.getSelectedClip() == nullptr)
        {
            std::printf ("FAIL: reference did not load - %s\n",
                         processor.getStatusMessage().toRawUTF8());
            return 1;
        }

        // Demo mode fills a second slot and selects it, so the picture shows all
        // three states the row can be in at once: an occupied slot that is not
        // selected, an occupied slot that is, and an empty one. A long name goes
        // in slot 2 on purpose - if truncation is going to look bad, it should
        // look bad in the artefact rather than in a session.
        if (demo)
        {
            const auto second = writeDemoReference ("TheBiggerLights_master_v3");

            if (second != juce::File())
            {
                processor.loadReference (1, second);

                const auto deadline = juce::Time::getMillisecondCounter() + 15000;

                while (processor.getUiClip (1) == nullptr
                       && juce::Time::getMillisecondCounter() < deadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

                processor.setSelectedSlot (1, true);
            }
        }
    }

    // Push a little audio through before painting, so the meters have something
    // to say and the readouts are not all dashes in the picture.
    int blocksPushed = 0;

    auto pushAudio = [&processor, &blocksPushed] (int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int block = 0; block < blocks; ++block, ++blocksPushed)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 512; ++n)
                {
                    const double t = (blocksPushed * 512 + n) / 48000.0;
                    buffer.setSample (ch, n, (float) (0.07 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)));
                }

            processor.processBlock (buffer, midi);
        }
    };

    pushAudio (400);   // just over four seconds

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());

    if (editor == nullptr)
    {
        std::printf ("FAIL: the processor produced no editor\n");
        return 1;
    }

    // Drag a loop region out on the waveform, over the third of the demo file's
    // four sections - so the picture shows a selection with file either side of
    // it rather than a strip that is all region.
    if (demo)
    {
        // The strip is handed the clip by the editor's timer, and a strip with no
        // clip in it ignores the mouse - correctly, since there is nothing to
        // select. So let the timer fire before pretending to be a person.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

        WaveformDisplay* strip = nullptr;

        for (auto* child : editor->getChildren())
            if (auto* candidate = dynamic_cast<WaveformDisplay*> (child))
                strip = candidate;

        if (strip == nullptr)
        {
            std::printf ("FAIL: the panel has no waveform strip\n");
            return 1;
        }

        // The demo file is 30 seconds, so these pixels are 14.0 s and 22.0 s.
        const int fromX = juce::roundToInt (strip->getWidth() * 14.0 / 30.0);
        const int toX   = juce::roundToInt (strip->getWidth() * 22.0 / 30.0);

        dragLoopRegion (*strip, fromX, toX);

        const float start = readParameter (processor.apvts, processor.getLoopStartParamId());
        const float end   = readParameter (processor.apvts, processor.getLoopEndParamId());

        // A pixel of slack either way: the drag is quantised to the strip's
        // width, so it cannot land on the second exactly and should not claim to.
        const double perPixel = 30.0 / (double) strip->getWidth();

        if (std::abs (start - 14.0f) > perPixel || std::abs (end - 22.0f) > perPixel)
        {
            std::printf ("FAIL: dragging 14.0 - 22.0 s set %.3f - %.3f\n", start, end);
            return 1;
        }

        if (readParameter (processor.apvts, ABParams::loop) <= 0.5f)
        {
            std::printf ("FAIL: selecting a region did not switch Loop on\n");
            return 1;
        }

        std::printf ("Loop region dragged out by hand: %.2f - %.2f s, Loop on\n", start, end);

        // And a little more audio, so the playhead in the picture is somewhere
        // the region put it rather than where the timeline was before it existed.
        pushAudio (100);
    }

    // Let the editor's own refresh timer fire at least once, so what is painted
    // is the state the panel actually settles into rather than its constructor
    // defaults. Also how a theme switch reaches the panel here: the editor's
    // timer notices Palette::dark changed, as it does for a second window.
    const auto settle = []
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 400;

        while (juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    };

    const auto render = [&editor] (const juce::File& file)
    {
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

        {
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);
        }

        juce::PNGImageFormat format;

        // Deleted first, because createOutputStream opens an existing file at its
        // end. Without this every render after the first is appended to the last
        // one, and since a decoder stops at the first image in the file, the picture
        // you look at is the one from the build before the change you are checking.
        // This target exists to make the panel checkable; a stale image that still
        // looks plausible defeats the entire point of it.
        file.deleteFile();

        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr || ! format.writeImageToStream (image, *stream))
        {
            std::printf ("FAIL: could not write %s\n", file.getFullPathName().toRawUTF8());
            return false;
        }

        std::printf ("Editor %d x %d rendered to %s\n",
                     image.getWidth(), image.getHeight(), file.getFullPathName().toRawUTF8());
        return true;
    };

    // The light panel, then the dark one beside it. Palette::setDark, not the
    // button, so the user's saved choice is neither read nor written here.
    Palette::setDark (false);
    settle();

    if (! render (output))
        return 1;

    Palette::setDark (true);
    settle();

    const auto darkOutput = output.getSiblingFile (output.getFileNameWithoutExtension() + "-dark"
                                                   + output.getFileExtension());
    const bool darkWritten = render (darkOutput);
    Palette::setDark (false);

    if (! darkWritten)
        return 1;

    // The Balance view, in demo mode: a reference whose ends move between
    // sections, and an A with more bass and less air than any of them, so the
    // picture shows the corridor's tub shape and A leaving it at both ends.
    if (demo)
    {
        const auto balanceReference = writeWav (makeBalanceDemo (30.0, 0.0f, 0.0f, 7), "ABReferenceBalanceDemo");

        if (balanceReference == juce::File())
        {
            std::printf ("FAIL: could not write the balance demo reference\n");
            return 1;
        }

        processor.loadReference (2, balanceReference);

        const auto loadDeadline = juce::Time::getMillisecondCounter() + 15000;

        while (processor.getUiClip (2) == nullptr && juce::Time::getMillisecondCounter() < loadDeadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto clip = processor.getUiClip (2);

        if (clip == nullptr || ! clip->balance.isValid())
        {
            std::printf ("FAIL: the balance demo reference has no corridor\n");
            return 1;
        }

        processor.setSelectedSlot (2, false);
        processor.apvts.state.setProperty (ABParams::propDisplayView, 1, nullptr);

        // A, through the real processBlock, with the balance hand-over drained
        // after every block the way the editor's timer drains it.
        // Fifteen seconds, five time constants: the 220 Hz tone pushed for the
        // other shots is still in A's average otherwise.
        const auto a = makeBalanceDemo (15.0, 6.0f, -5.0f, 11);
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        for (int start = 0; start + 512 <= a.getNumSamples(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, a, ch, start, 512);

            processor.processBlock (block, midi);
            processor.updateBalance();
        }

        Balance::Bands aBalance {};

        if (! processor.getABalance (aBalance))
        {
            std::printf ("FAIL: A has no balance after 15 s of signal\n");
            return 1;
        }

        // A has more bass and less air than the reference's average section.
        // Not the full 6 and 5 dB: the shelves are gentle at the band centres,
        // and at equal loudness extra bass pushes every other band down a
        // little. The direction, and a clear margin, is what is checked.
        const float lowGap  = aBalance[2]  - clip->balance.median[2];
        const float highGap = aBalance[37] - clip->balance.median[37];
        std::printf ("Balance: A against the reference: %+.1f dB at %.0f Hz, %+.1f dB at %.0f Hz, %d windows\n",
                     lowGap, Balance::bandCentreHz (2), highGap, Balance::bandCentreHz (37),
                     clip->balance.windows);

        if (lowGap < 2.0f || highGap > -2.0f)
        {
            std::printf ("FAIL: more bass and less air should read above and below the reference\n");
            return 1;
        }

        const auto balanceOutput = output.getSiblingFile (output.getFileNameWithoutExtension() + "-balance"
                                                          + output.getFileExtension());
        const auto balanceDark = output.getSiblingFile (output.getFileNameWithoutExtension() + "-balance-dark"
                                                        + output.getFileExtension());

        Palette::setDark (false);
        settle();

        // The cursor, hovered the way a mouse would, a little right of 1 kHz,
        // so the shot shows the readout as well as the curves.
        BalanceDisplay* balanceView = nullptr;

        for (auto* child : editor->getChildren())
            if (auto* candidate = dynamic_cast<BalanceDisplay*> (child))
                balanceView = candidate;

        if (balanceView == nullptr || ! balanceView->isVisible())
        {
            std::printf ("FAIL: the Balance view is not showing\n");
            return 1;
        }

        balanceView->mouseMove (eventAt (*balanceView, juce::roundToInt ((float) balanceView->getWidth() * 0.62f), 0));

        if (! render (balanceOutput))
            return 1;

        Palette::setDark (true);
        settle();
        const bool written = render (balanceDark);
        Palette::setDark (false);

        if (! written)
            return 1;
    }

    processor.editorBeingDeleted (editor.get());
    editor.reset();
    return 0;
}

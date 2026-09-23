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
#include "WaveformDisplay.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "ParameterIds.h"

#include <cstdio>

namespace
{
    /** A reference file to photograph the panel with. Synthesised rather than
        shipped, because the one thing this target must not need is an asset
        somebody has to remember to keep next to it - and because a file built
        here can be given exactly the shape that makes a waveform worth looking
        at: quiet, loud, quiet again, with the sections at obvious boundaries. */
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
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

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
    // defaults.
    const auto deadline = juce::Time::getMillisecondCounter() + 400;

    while (juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

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
    output.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (output.createOutputStream());

    if (stream == nullptr || ! format.writeImageToStream (image, *stream))
    {
        std::printf ("FAIL: could not write %s\n", output.getFullPathName().toRawUTF8());
        return 1;
    }

    processor.editorBeingDeleted (editor.get());
    editor.reset();

    std::printf ("Editor %d x %d rendered to %s\n",
                 image.getWidth(), image.getHeight(), output.getFullPathName().toRawUTF8());
    return 0;
}

/*
    Renders a plugin editor to a PNG without opening a window.

        render_editor <out.png> [size=WxH] [signal=off] [realtime=off] [paramID=value ...]

    Why this exists: iterating on a UI by launching the standalone, looking at
    it, and describing what is wrong is slow and lossy. This produces an image
    file on every build, so the interface can be reviewed and diffed the same
    way as any other artefact — and reviewed by someone who is not sitting at
    the machine.

    It paints the component tree straight into an offscreen juce::Image. No
    window is ever created, so this also runs in CI.

    ── Order of operations ─────────────────────────────────────────────────
    The tool deliberately mirrors what a host does, because doing it in any
    other order produces a picture that lies:

      1. Set parameters.        A host restores state before opening a window.
      2. Create the editor.     It caches things at construction.
      3. Feed audio AND run the message loop together, so the editor's timer
         fires between blocks exactly as it would in a session. That is what
         drives the spectrum analyser and any history display; pumping them by
         hand instead gets the decay behaviour wrong and produces an empty or
         faded graph.
      4. Render.
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

/*  Which plugin this binary renders is chosen at compile time: the two headers
    above are found via the include path, and the class name comes from the
    build. One source file therefore serves every plugin in the repo, and a new
    plugin gets a renderer by adding one line to tools/CMakeLists.txt.
*/
#ifndef PLUGIN_PROCESSOR_CLASS
 #define PLUGIN_PROCESSOR_CLASS MixingPluginProcessor
#endif

using ProcessorType = PLUGIN_PROCESSOR_CLASS;

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr,
            "usage: render_editor <out.png> [size=WxH] [signal=off] [realtime=off]"
            " [paramID=value ...]\n");
        return 1;
    }

    // Spins up the message manager and the platform GUI layer without creating
    // any window. Required before touching Component or Graphics.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    ProcessorType processor;

    int  requestedWidth = 0, requestedHeight = 0;
    bool feedSignal = true;
    bool realtime   = true;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String argument { argv[i] };
        const auto separator = argument.indexOfChar ('=');

        if (separator < 0)
            continue;

        if (argument.startsWith ("signal="))
        {
            feedSignal = argument.substring (7) != "off";
            continue;
        }

        if (argument.startsWith ("realtime="))
        {
            realtime = argument.substring (9) != "off";
            continue;
        }

        if (argument.startsWith ("size="))
        {
            const auto dims = argument.substring (5);
            requestedWidth  = dims.upToFirstOccurrenceOf ("x", false, false).getIntValue();
            requestedHeight = dims.fromFirstOccurrenceOf ("x", false, false).getIntValue();
            continue;
        }

        const auto id    = argument.substring (0, separator);
        const auto value = argument.substring (separator + 1).getFloatValue();

        if (auto* parameter = processor.apvts.getParameter (id))
        {
            parameter->setValueNotifyingHost (
                parameter->getNormalisableRange().convertTo0to1 (value));
            std::printf ("  set %s = %g\n", id.toRawUTF8(), value);
        }
        else
        {
            std::fprintf (stderr, "  warning: no parameter '%s'\n", id.toRawUTF8());
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::fprintf (stderr, "error: createEditor() returned nullptr\n");
        return 1;
    }

    if (requestedWidth > 0 && requestedHeight > 0)
        editor->setSize (requestedWidth, requestedHeight);

    if (feedSignal)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int    blockSize  = 512;
        constexpr int    numBlocks  = 400;

        // One block of audio is this many milliseconds of real time. Giving the
        // message loop the same amount keeps the editor's clock roughly in step
        // with the audio, so a history graph's time axis means something.
        constexpr int blockMilliseconds = (int) (1000.0 * blockSize / sampleRate);

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        // Fixed seed: renders must be reproducible or they are useless for
        // spotting a change between two builds.
        juce::Random random (0x5eed);

        // Paul Kellet's pink filter. Pink rather than white because a log
        // frequency axis makes white noise ramp upward, which tells you nothing
        // about the display and everything about the signal.
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

        for (int block = 0; block < numBlocks; ++block)
        {
            // A slow swell rather than a constant level. A compressor sitting at
            // one fixed reduction shows nothing about its attack or release;
            // moving level is the only way the behaviour becomes visible.
            const auto swell = 0.35f + 0.6f * (float) std::abs (std::sin (block * 0.024));

            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;

                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;

                const auto pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.11f;
                b6 = white * 0.115926f;

                buffer.setSample (0, i, pink * swell);
                buffer.setSample (1, i, pink * swell);
            }

            processor.processBlock (buffer, midi);

            /*  Let the editor's timer run, by the same path it uses in a real
                host rather than a hand-rolled imitation.

                The sleep is not padding: timers become due by wall clock, and
                processing a block takes microseconds, so without spending the
                real time a 30 Hz timer would almost never fire. Sleeping for
                one block's worth keeps the editor's clock in step with the
                audio, which is what makes a history graph's time axis mean
                something. It also makes a render take a few seconds — the
                price of the picture being true.
            */
            if (realtime)
            {
                juce::Thread::sleep (blockMilliseconds);
                juce::Timer::callPendingTimersSynchronously();
            }
        }
    }

    const auto width  = editor->getWidth();
    const auto height = editor->getHeight();

    if (width <= 0 || height <= 0)
    {
        std::fprintf (stderr, "error: editor has zero size (%d x %d)\n", width, height);
        return 1;
    }

    juce::Image image (juce::Image::ARGB, width, height, true);

    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    juce::File outputFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    outputFile.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (outputFile.createOutputStream());

    if (stream == nullptr || ! stream->openedOk())
    {
        std::fprintf (stderr, "error: could not open %s for writing\n",
                      outputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, *stream))
    {
        std::fprintf (stderr, "error: PNG encoding failed\n");
        return 1;
    }

    std::printf ("wrote %s (%d x %d)\n", outputFile.getFullPathName().toRawUTF8(), width, height);
    return 0;
}

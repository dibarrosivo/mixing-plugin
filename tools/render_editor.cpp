/*
    Renders a plugin editor to a PNG without opening a window.

        render_editor <output.png> [width height]

    Why this exists: iterating on a UI by launching the standalone, looking at
    it, and describing what is wrong is slow and lossy. This produces an image
    file on every build, so the interface can be reviewed and diffed the same
    way as any other artefact — and reviewed by someone who is not sitting at
    the machine.

    It paints the component tree straight into an offscreen juce::Image. No
    window is ever created, so this also runs in CI.
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: render_editor <out.png> [size=WxH] [signal=off] [paramID=value ...]\n");
        return 1;
    }

    // Spins up the message manager and the platform GUI layer without creating
    // any window. Required before touching Component or Graphics.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    MixingPluginProcessor processor;

    // Any further arguments of the form id=value set a parameter before the
    // render. A UI is only worth reviewing in the states it will actually be
    // used in — a flat curve tells you almost nothing.
    int  requestedWidth = 0, requestedHeight = 0;
    bool feedSignal = true;

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
            const auto normalised = parameter->getNormalisableRange().convertTo0to1 (value);
            parameter->setValueNotifyingHost (normalised);
            std::printf ("  set %s = %g\n", id.toRawUTF8(), value);
        }
        else
        {
            std::fprintf (stderr, "  warning: no parameter '%s'\n", id.toRawUTF8());
        }
    }

    // Run audio through the processor so the analyser has something to show.
    // Without this the spectrum renders as an empty floor and the layout cannot
    // be judged — the shape behind the curve is most of the visual weight.
    if (feedSignal)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int    blockSize  = 512;
        constexpr int    numBlocks  = 220;   // ~2.3 s, well past the smoother

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

                buffer.setSample (0, i, pink);
                buffer.setSample (1, i, pink);
            }

            processor.processBlock (buffer, midi);

            // Pump the analyser roughly at the editor's 30 Hz timer rate.
            // Draining it only at the end would run the decay path over and
            // over on an empty FIFO and fade the whole spectrum to the floor.
            if (block % 3 == 0)
                processor.analyser.update();
        }

        processor.analyser.update();
    }

    // Editor is created only now. A host restores state before opening the
    // window, so building it first would render whatever the editor cached at
    // construction rather than the state actually being asked for.
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::fprintf (stderr, "error: createEditor() returned nullptr\n");
        return 1;
    }

    if (requestedWidth > 0 && requestedHeight > 0)
        editor->setSize (requestedWidth, requestedHeight);

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

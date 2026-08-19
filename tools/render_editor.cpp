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
        std::fprintf (stderr, "usage: render_editor <output.png> [size=WxH] [paramID=value ...]\n");
        return 1;
    }

    // Spins up the message manager and the platform GUI layer without creating
    // any window. Required before touching Component or Graphics.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    MixingPluginProcessor processor;

    // Any further arguments of the form id=value set a parameter before the
    // render. A UI is only worth reviewing in the states it will actually be
    // used in — a flat curve tells you almost nothing.
    int requestedWidth = 0, requestedHeight = 0;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String argument { argv[i] };
        const auto separator = argument.indexOfChar ('=');

        if (separator < 0)
            continue;

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

    // Editor is created only now. A host restores state before opening the
    // window, so building it first would render whatever the editor cached at
    // construction rather than the state actually being asked for.
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::fprintf (stderr, "error: createEditor() returned nullptr\n");
        return 1;
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

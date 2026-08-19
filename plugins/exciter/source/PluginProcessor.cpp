#include "PluginProcessor.h"
#include "PluginEditor.h"

ExciterProcessor::ExciterProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    focusParam      = apvts.getRawParameterValue (ParamID::focus);
    driveParam      = apvts.getRawParameterValue (ParamID::drive);
    mixParam        = apvts.getRawParameterValue (ParamID::mix);
    typeParam       = apvts.getRawParameterValue (ParamID::type);
    listenParam     = apvts.getRawParameterValue (ParamID::listen);
    inputGainParam  = apvts.getRawParameterValue (ParamID::inputGain);
    outputGainParam = apvts.getRawParameterValue (ParamID::outputGain);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));
    jassert (bypassParam != nullptr);
}

void ExciterProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (maximumExpectedSamplesPerBlock),
        static_cast<juce::uint32> (juce::jmax (getTotalNumInputChannels(),
                                               getTotalNumOutputChannels()))
    };

    inputGain.prepare (spec);
    outputGain.prepare (spec);
    exciter.prepare (spec);
    analyser.prepare (sampleRate);

    /*  The first plugin in this repo with any latency at all.

        The oversampler's linear-phase filters delay the signal, and the host
        has to know so it can slide every other track to match. Without this the
        excited track sits ~61 samples late against the rest of the session,
        which on a doubled vocal is audible comb filtering.
    */
    setLatencySamples (exciter.getLatencySamples());

    inputGain.setGainDecibels (inputGainParam->load());
    outputGain.setGainDecibels (outputGainParam->load());
    inputGain.reset();
    outputGain.reset();
}

void ExciterProcessor::releaseResources()
{
    inputGain.reset();
    exciter.reset();
    outputGain.reset();
    analyser.reset();
}

bool ExciterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void ExciterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels  = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (auto ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const auto bypassed = bypassParam->get();

    inputGain.setGainDecibels  (bypassed ? 0.0f : inputGainParam->load());
    outputGain.setGainDecibels (bypassed ? 0.0f : outputGainParam->load());

    dsp::HarmonicExciter::Settings settings;

    /*  Bypass is expressed as mix = 0, NOT as enabled = false.

        Disabling the exciter would skip its dry delay line, so a bypassed
        instance would output ~61 samples EARLY relative to a host that is still
        compensating for the latency we reported. Keeping the path alive with
        nothing mixed in means bypass changes the sound and nothing else.
    */
    settings.enabled    = true;
    settings.focusHz    = focusParam->load();
    settings.drive      = driveParam->load();
    settings.mixPercent = bypassed ? 0.0f : mixParam->load();
    settings.listen     = ! bypassed && listenParam->load() > 0.5f;

    const auto typeIndex = (int) typeParam->load();
    settings.type = typeIndex == 0 ? dsp::SaturationType::tube
                  : typeIndex == 1 ? dsp::SaturationType::tape
                                   : dsp::SaturationType::transistor;

    exciter.setSettings (settings);

    juce::dsp::AudioBlock<float> block { buffer };

    inputGain.process (block);
    exciter.process (block);
    outputGain.process (block);

    pushToAnalyser (buffer);
}

void ExciterProcessor::pushToAnalyser (const juce::AudioBuffer<float>& buffer) noexcept
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    const auto scale = 1.0f / (float) numChannels;
    const auto chunkSize = (int) analyserScratch.size();

    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const auto chunk = juce::jmin (chunkSize, numSamples - offset);

        for (int i = 0; i < chunk; ++i)
        {
            auto sum = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                sum += buffer.getReadPointer (ch)[offset + i];

            analyserScratch[(size_t) i] = sum * scale;
        }

        analyser.pushSamples (analyserScratch.data(), chunk);
    }
}

juce::AudioProcessorEditor* ExciterProcessor::createEditor()
{
    return new ExciterEditor (*this);
}

void ExciterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ExciterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExciterProcessor();
}

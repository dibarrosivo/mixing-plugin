#include "PluginProcessor.h"
#include "PluginEditor.h"

MultibandProcessor::MultibandProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (size_t band = 0; band < numBands; ++band)
    {
        const auto i = (int) band;

        bandParams[band].on        = apvts.getRawParameterValue (ParamID::bandOn (i));
        bandParams[band].threshold = apvts.getRawParameterValue (ParamID::bandThreshold (i));
        bandParams[band].ratio     = apvts.getRawParameterValue (ParamID::bandRatio (i));
        bandParams[band].attack    = apvts.getRawParameterValue (ParamID::bandAttack (i));
        bandParams[band].release   = apvts.getRawParameterValue (ParamID::bandRelease (i));
        bandParams[band].makeup    = apvts.getRawParameterValue (ParamID::bandMakeup (i));
        bandParams[band].mute      = apvts.getRawParameterValue (ParamID::bandMute (i));
        bandParams[band].solo      = apvts.getRawParameterValue (ParamID::bandSolo (i));
    }

    for (size_t i = 0; i < numCrossovers; ++i)
        crossoverParams[i] = apvts.getRawParameterValue (ParamID::crossover ((int) i));

    inputGainParam  = apvts.getRawParameterValue (ParamID::inputGain);
    outputGainParam = apvts.getRawParameterValue (ParamID::outputGain);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));
    jassert (bypassParam != nullptr);
}

void MultibandProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (maximumExpectedSamplesPerBlock),
        static_cast<juce::uint32> (juce::jmax (getTotalNumInputChannels(),
                                               getTotalNumOutputChannels()))
    };

    inputGain.prepare (spec);
    outputGain.prepare (spec);
    compressor.prepare (sampleRate);
    analyser.prepare (sampleRate);

    inputGain.setGainDecibels (inputGainParam->load());
    outputGain.setGainDecibels (outputGainParam->load());
    inputGain.reset();
    outputGain.reset();
}

void MultibandProcessor::releaseResources()
{
    inputGain.reset();
    compressor.reset();
    outputGain.reset();
    analyser.reset();
}

bool MultibandProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void MultibandProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels  = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (auto ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (! bypassParam->get())
    {
        inputGain.setGainDecibels  (inputGainParam->load());
        outputGain.setGainDecibels (outputGainParam->load());

        std::array<float, numCrossovers> frequencies {};
        for (size_t i = 0; i < numCrossovers; ++i)
            frequencies[i] = crossoverParams[i]->load();

        compressor.setCrossoverFrequencies (frequencies);

        for (size_t band = 0; band < numBands; ++band)
        {
            const auto& p = bandParams[band];

            dsp::Compressor::Settings settings;
            settings.enabled      = p.on->load() > 0.5f;
            settings.thresholdDb  = p.threshold->load();
            settings.ratio        = p.ratio->load();
            settings.kneeDb       = 6.0f;   // fixed; a musical default
            settings.attackMs     = p.attack->load();
            settings.releaseMs    = p.release->load();
            settings.makeupGainDb = p.makeup->load();
            settings.detectorMode = dsp::EnvelopeFollower::Mode::rms;

            compressor.setBand (band, settings);
            compressor.setBandMuted  (band, p.mute->load() > 0.5f);
            compressor.setBandSoloed (band, p.solo->load() > 0.5f);
        }

        juce::dsp::AudioBlock<float> block { buffer };
        inputGain.process (block);

        // The compressor works on raw pointers so the multiband can split,
        // gain and sum within a single sample. See MultibandCompressor.h.
        float* channels[2] {};
        const auto usable = juce::jmin (numOutputChannels, 2);

        for (int ch = 0; ch < usable; ++ch)
            channels[ch] = buffer.getWritePointer (ch);

        compressor.process (channels, usable, buffer.getNumSamples());

        outputGain.process (block);
    }

    pushToAnalyser (buffer);
}

void MultibandProcessor::pushToAnalyser (const juce::AudioBuffer<float>& buffer) noexcept
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

juce::AudioProcessorEditor* MultibandProcessor::createEditor()
{
    return new MultibandEditor (*this);
}

void MultibandProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MultibandProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultibandProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

MixingPluginProcessor::MixingPluginProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (size_t band = 0; band < numBands; ++band)
    {
        const auto index = (int) band;

        bandParams[band].frequency = apvts.getRawParameterValue (ParamID::bandFreq (index));
        bandParams[band].gain      = apvts.getRawParameterValue (ParamID::bandGain (index));
        bandParams[band].q         = apvts.getRawParameterValue (ParamID::bandQ    (index));
        bandParams[band].on        = apvts.getRawParameterValue (ParamID::bandOn   (index));
        bandParams[band].dynamic   = apvts.getRawParameterValue (ParamID::bandDyn (index));
        bandParams[band].threshold = apvts.getRawParameterValue (ParamID::bandThreshold (index));
        bandParams[band].ratio     = apvts.getRawParameterValue (ParamID::bandRatio (index));
        bandParams[band].attack    = apvts.getRawParameterValue (ParamID::bandAttack (index));
        bandParams[band].release   = apvts.getRawParameterValue (ParamID::bandRelease (index));

        jassert (bandParams[band].frequency != nullptr);
    }

    inputGainParam  = apvts.getRawParameterValue (ParamID::inputGain);
    outputGainParam = apvts.getRawParameterValue (ParamID::outputGain);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (
        apvts.getParameter (ParamID::bypass));

    jassert (bypassParam != nullptr);
}

void MixingPluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (maximumExpectedSamplesPerBlock),
        static_cast<juce::uint32> (juce::jmax (getTotalNumInputChannels(),
                                               getTotalNumOutputChannels()))
    };

    inputGain.prepare (spec);
    equaliser.prepare (spec);
    outputGain.prepare (spec);
    analyser.prepare (sampleRate);

    // Push the current parameter values in and snap the smoothers to them, so
    // the first block after a transport start is not a 20 ms ramp from silence.
    inputGain.setGainDecibels (inputGainParam->load());
    outputGain.setGainDecibels (outputGainParam->load());
    inputGain.reset();
    outputGain.reset();
}

void MixingPluginProcessor::releaseResources()
{
    inputGain.reset();
    equaliser.reset();
    outputGain.reset();
    analyser.reset();
}

bool MixingPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void MixingPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Flushes denormals to zero for the lifetime of this scope. Without it,
    // the tiny values left in filter state after silence cost enormous CPU.
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels  = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (auto ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Zero latency, so bypass is a plain pass-through. If you ever add a
    // look-ahead or oversampling stage, this branch has to delay the dry signal
    // by the same amount or bypass will click and phase-cancel.
    if (! bypassParam->get())
    {
        inputGain.setGainDecibels  (inputGainParam->load());
        outputGain.setGainDecibels (outputGainParam->load());

        for (size_t band = 0; band < numBands; ++band)
        {
            const auto& p = bandParams[band];

            dsp::DynamicEqBand::Settings settings;
            settings.frequencyHz    = p.frequency->load();
            settings.staticGainDb   = p.gain->load();
            settings.q              = p.q->load();
            settings.enabled        = p.on->load() > 0.5f;
            settings.dynamicEnabled = p.dynamic->load() > 0.5f;
            settings.thresholdDb    = p.threshold->load();
            settings.ratio          = p.ratio->load();
            settings.kneeDb         = 6.0f;   // fixed for now; a musical default
            settings.attackMs       = p.attack->load();
            settings.releaseMs      = p.release->load();

            equaliser.setBand (band, settings);
        }

        juce::dsp::AudioBlock<float> block { buffer };

        inputGain.process (block);
        equaliser.process (block);
        outputGain.process (block);
    }

    // Fed in both paths, so the analyser keeps showing signal while bypassed
    // rather than going blank and looking broken.
    pushToAnalyser (buffer);
}

void MixingPluginProcessor::pushToAnalyser (const juce::AudioBuffer<float>& buffer) noexcept
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

juce::AudioProcessorEditor* MixingPluginProcessor::createEditor()
{
    return new MixingPluginEditor (*this);
}

void MixingPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // APVTS serialises the whole parameter tree, so new parameters are saved
    // automatically without touching this method.
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MixingPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixingPluginProcessor();
}

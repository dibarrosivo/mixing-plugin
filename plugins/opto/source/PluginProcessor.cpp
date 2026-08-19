#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // The slow stage is derived rather than exposed. Real optical cells recover
    // in two stages roughly an order of magnitude apart; tying them keeps the
    // panel to one release control and stops the two being set to nonsense
    // relative to each other.
    constexpr float slowReleaseFactor = 18.0f;
    constexpr float slowReleaseCeilingMs = 8000.0f;
}

OptoProcessor::OptoProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    characterParam  = apvts.getRawParameterValue (ParamID::character);
    thresholdParam  = apvts.getRawParameterValue (ParamID::threshold);
    ratioParam      = apvts.getRawParameterValue (ParamID::ratio);
    attackParam     = apvts.getRawParameterValue (ParamID::attack);
    releaseParam    = apvts.getRawParameterValue (ParamID::release);
    programParam    = apvts.getRawParameterValue (ParamID::program);
    makeupParam     = apvts.getRawParameterValue (ParamID::makeup);
    inputGainParam  = apvts.getRawParameterValue (ParamID::inputGain);
    outputGainParam = apvts.getRawParameterValue (ParamID::outputGain);

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));
    jassert (bypassParam != nullptr);
}

void OptoProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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

    inputGain.setGainDecibels (inputGainParam->load());
    outputGain.setGainDecibels (outputGainParam->load());
    inputGain.reset();
    outputGain.reset();
}

void OptoProcessor::releaseResources()
{
    inputGain.reset();
    compressor.reset();
    outputGain.reset();
}

bool OptoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void OptoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

        const auto character = characterParam->load() < 0.5f ? Character::optical
                                                             : Character::fet;
        const auto releaseMs = releaseParam->load();

        dsp::OpticalCompressor::Settings settings;
        settings.enabled       = true;
        settings.thresholdDb   = thresholdParam->load();
        settings.ratio         = ratioParam->load();
        settings.makeupGainDb  = makeupParam->load();
        settings.attackMs      = attackParam->load();
        settings.releaseFastMs = releaseMs;
        settings.releaseSlowMs = juce::jmin (slowReleaseCeilingMs, releaseMs * slowReleaseFactor);
        settings.programDepth  = programParam->load() * 0.01f;

        /*  Character is only a few numbers, but they are the ones that matter.

            Optical: RMS detection over a longer window, a wide knee, and the
            program-dependent tail left intact — it leans on level, not
            transients.

            FET: peak detection over a very short window and a tight knee, so it
            reacts to attacks. The program tail is forced off, because a 1176's
            release does not lengthen with sustained reduction the way a cell's
            does.
        */
        if (character == Character::optical)
        {
            settings.detectorMode = dsp::EnvelopeFollower::Mode::rms;
            settings.detectorMs   = 5.0f;
            settings.kneeDb       = 10.0f;
        }
        else
        {
            settings.detectorMode = dsp::EnvelopeFollower::Mode::peak;
            settings.detectorMs   = 0.2f;
            settings.kneeDb       = 2.0f;
            settings.programDepth = 0.0f;
        }

        compressor.setSettings (settings);

        juce::dsp::AudioBlock<float> block { buffer };
        inputGain.process (block);

        float* channels[2] {};
        const auto usable = juce::jmin (numOutputChannels, 2);

        for (int ch = 0; ch < usable; ++ch)
            channels[ch] = buffer.getWritePointer (ch);

        compressor.process (channels, usable, buffer.getNumSamples());

        outputGain.process (block);
    }
}

juce::AudioProcessorEditor* OptoProcessor::createEditor()
{
    return new OptoEditor (*this);
}

void OptoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OptoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OptoProcessor();
}

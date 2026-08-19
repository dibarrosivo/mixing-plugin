#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include <dsp/GainStage.h>
#include <dsp/ToneFilter.h>

class MixingPluginProcessor final : public juce::AudioProcessor
{
public:
    MixingPluginProcessor();
    ~MixingPluginProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    // Pulls the double-precision overload into scope. Declaring only the float
    // one hides it, which is what -Woverloaded-virtual is complaining about.
    // Harmless today because supportsDoublePrecisionProcessing() is false, but
    // it would silently bypass the base class the moment that changes.
    using juce::AudioProcessor::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    // Letting the host drive bypass through a real parameter means automation,
    // host bypass buttons and our own toggle all stay in sync.
    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    // Looking a parameter up by string ID inside processBlock hashes a string
    // on every block. Resolve the pointers once in the constructor and read
    // through them forever after.
    std::atomic<float>* inputGainParam  { nullptr };
    std::atomic<float>* toneFreqParam   { nullptr };
    std::atomic<float>* toneGainParam   { nullptr };
    std::atomic<float>* toneQParam      { nullptr };
    std::atomic<float>* outputGainParam { nullptr };
    juce::AudioParameterBool* bypassParam { nullptr };

    // The signal chain, in order. Adding a module means: declare it here,
    // prepare it in prepareToPlay, reset it in reset, process it in processBlock.
    dsp::GainStage  inputGain;
    dsp::ToneFilter tone;
    dsp::GainStage  outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixingPluginProcessor)
};

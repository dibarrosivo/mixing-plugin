#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <dsp/GainStage.h>
#include <dsp/OpticalCompressor.h>

#include "Parameters.h"

class OptoProcessor final : public juce::AudioProcessor
{
public:
    OptoProcessor();
    ~OptoProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    float getGainReductionDb() const noexcept { return compressor.getGainReductionDb(); }

    // How far into the slow release the optical cell currently is, 0 to 1.
    // Shown in the UI because it is what explains the release behaving
    // differently on a transient than on a sustained note.
    float getProgramMemory() const noexcept { return compressor.getProgramMemory(); }

private:
    std::atomic<float>* characterParam { nullptr };
    std::atomic<float>* thresholdParam { nullptr };
    std::atomic<float>* ratioParam     { nullptr };
    std::atomic<float>* attackParam    { nullptr };
    std::atomic<float>* releaseParam   { nullptr };
    std::atomic<float>* programParam   { nullptr };
    std::atomic<float>* makeupParam    { nullptr };
    std::atomic<float>* inputGainParam { nullptr };
    std::atomic<float>* outputGainParam { nullptr };
    juce::AudioParameterBool* bypassParam { nullptr };

    dsp::GainStage          inputGain;
    dsp::OpticalCompressor  compressor;
    dsp::GainStage          outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoProcessor)
};

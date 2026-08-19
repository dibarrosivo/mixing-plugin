#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <dsp/GainStage.h>
#include <dsp/HarmonicExciter.h>
#include <dsp/SpectrumAnalyser.h>

#include "Parameters.h"

class ExciterProcessor final : public juce::AudioProcessor
{
public:
    ExciterProcessor();
    ~ExciterProcessor() override = default;

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

    dsp::SpectrumAnalyser<12> analyser;

private:
    void pushToAnalyser (const juce::AudioBuffer<float>& buffer) noexcept;

    std::array<float, 512> analyserScratch {};

    std::atomic<float>* focusParam  { nullptr };
    std::atomic<float>* driveParam  { nullptr };
    std::atomic<float>* mixParam    { nullptr };
    std::atomic<float>* typeParam   { nullptr };
    std::atomic<float>* listenParam { nullptr };
    std::atomic<float>* inputGainParam  { nullptr };
    std::atomic<float>* outputGainParam { nullptr };
    juce::AudioParameterBool* bypassParam { nullptr };

    dsp::GainStage       inputGain;
    dsp::HarmonicExciter exciter;
    dsp::GainStage       outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExciterProcessor)
};

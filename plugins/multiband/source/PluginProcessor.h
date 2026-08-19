#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <dsp/GainStage.h>
#include <dsp/MultibandCompressor.h>
#include <dsp/SpectrumAnalyser.h>

#include "Parameters.h"

class MultibandProcessor final : public juce::AudioProcessor
{
public:
    static constexpr size_t numBands      = (size_t) ParamID::numBands;
    static constexpr size_t numCrossovers = (size_t) ParamID::numCrossovers;

    MultibandProcessor();
    ~MultibandProcessor() override = default;

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

    // Live gain reduction, in dB and always <= 0. Atomic inside, so the editor
    // can read it from the message thread.
    float getBandReductionDb (int band) const noexcept
    {
        return compressor.getBandReductionDb ((size_t) band);
    }

    // The crossover actually in use, after ordering has been enforced. The
    // display draws these rather than the raw parameters, so a divider dragged
    // past its neighbour shows where it really ended up.
    float getEffectiveCrossoverHz (int index) const noexcept
    {
        return compressor.getCrossoverFrequency ((size_t) index);
    }

private:
    void pushToAnalyser (const juce::AudioBuffer<float>& buffer) noexcept;

    std::array<float, 512> analyserScratch {};

    struct BandParams
    {
        std::atomic<float>* on        { nullptr };
        std::atomic<float>* threshold { nullptr };
        std::atomic<float>* ratio     { nullptr };
        std::atomic<float>* attack    { nullptr };
        std::atomic<float>* release   { nullptr };
        std::atomic<float>* makeup    { nullptr };
        std::atomic<float>* mute      { nullptr };
        std::atomic<float>* solo      { nullptr };
    };

    std::array<BandParams, numBands>          bandParams {};
    std::array<std::atomic<float>*, numCrossovers> crossoverParams {};

    std::atomic<float>* inputGainParam  { nullptr };
    std::atomic<float>* outputGainParam { nullptr };
    juce::AudioParameterBool* bypassParam { nullptr };

    dsp::GainStage                       inputGain;
    dsp::MultibandCompressor<numBands>   compressor;
    dsp::GainStage                       outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandProcessor)
};

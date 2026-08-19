#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <dsp/GainStage.h>
#include <dsp/ParametricEq.h>
#include <dsp/SpectrumAnalyser.h>

#include "Parameters.h"

class MixingPluginProcessor final : public juce::AudioProcessor
{
public:
    static constexpr size_t numBands = (size_t) ParamID::numBands;

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

    // Driven from the audio thread by pushSamples(), drained and transformed on
    // the message thread by the editor. The lock-free FIFO inside is what makes
    // that safe; see dsp/SpectrumAnalyser.h.
    dsp::SpectrumAnalyser<12> analyser;

    // Live gain reduction for a band, in dB and always <= 0. Read by the editor
    // to show the dynamics working; atomic inside, so this is safe to call from
    // the message thread.
    float getBandReductionDb (int band) const noexcept
    {
        return equaliser.getBandReductionDb ((size_t) band);
    }

private:
    void pushToAnalyser (const juce::AudioBuffer<float>& buffer) noexcept;

    // Fixed scratch for the mono sum. A member rather than a local so the audio
    // thread never touches a large stack frame, and never allocates.
    std::array<float, 512> analyserScratch {};

    // Cached atomic pointers: looking parameters up by string ID inside
    // processBlock would hash a string every block. Resolve once, read forever.
    struct BandParams
    {
        std::atomic<float>* frequency { nullptr };
        std::atomic<float>* gain      { nullptr };
        std::atomic<float>* q         { nullptr };
        std::atomic<float>* on        { nullptr };
        std::atomic<float>* dynamic   { nullptr };
        std::atomic<float>* threshold { nullptr };
        std::atomic<float>* ratio     { nullptr };
        std::atomic<float>* attack    { nullptr };
        std::atomic<float>* release   { nullptr };
    };

    std::array<BandParams, numBands> bandParams {};

    std::atomic<float>* inputGainParam  { nullptr };
    std::atomic<float>* outputGainParam { nullptr };
    juce::AudioParameterBool* bypassParam { nullptr };

    // The signal chain, in order. Adding a module means: declare it here,
    // prepare it in prepareToPlay, reset it in releaseResources, process it in
    // processBlock. See libs/dsp for the module contract.
    dsp::GainStage                  inputGain;
    dsp::ParametricEq<numBands>     equaliser;
    dsp::GainStage                  outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixingPluginProcessor)
};

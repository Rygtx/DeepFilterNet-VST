#pragma once

#include "DenoiseEngine.h"

#include <JuceHeader.h>

class DeepFilterNetVstAudioProcessor final : public juce::AudioProcessor
{
public:
    DeepFilterNetVstAudioProcessor();
    ~DeepFilterNetVstAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParametersState();
    double getCurrentSampleRateHz() const;
    juce::String getDiagnosticText() const;
    bool isSampleRateCompatible() const;
    bool isDenoiserReady() const;
    static juce::StringArray getReduceMaskChoices();

    static constexpr const char* pluginDisplayName = "DeepFilterNet";
    static constexpr auto attenParamId = "attenLimDb";
    static constexpr auto postParamId = "postFilterBeta";
    static constexpr auto reduceMaskParamId = "reduceMask";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void publishSharedDiagnostics() const;

    dfvst::DenoiseEngine engine_;
    juce::AudioProcessorValueTreeState parameters_;
    std::atomic<float>* attenLimDbParam_ = nullptr;
    std::atomic<float>* postFilterBetaParam_ = nullptr;
    std::atomic<float>* reduceMaskParam_ = nullptr;
    std::atomic<int> prepareToPlayCount_ { 0 };
    std::atomic<int> processBlockCount_ { 0 };
    std::atomic<int> releaseResourcesCount_ { 0 };
    std::atomic<double> lastPreparedSampleRateHz_ { 0.0 };
    std::atomic<int> lastPreparedBlockSizeSamples_ { 0 };
    std::atomic<double> lastProcessSampleRateHz_ { 0.0 };
    std::atomic<int> lastProcessBlockSizeSamples_ { 0 };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

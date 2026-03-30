#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr double targetSampleRate = 48000.0;

juce::String utf8Text(const char* text)
{
    return juce::String::fromUTF8(text);
}
}

DeepFilterNetVstAudioProcessor::DeepFilterNetVstAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    attenLimDbParam_ = parameters_.getRawParameterValue(attenParamId);
    postFilterBetaParam_ = parameters_.getRawParameterValue(postParamId);
    reduceMaskParam_ = parameters_.getRawParameterValue(reduceMaskParamId);
}

DeepFilterNetVstAudioProcessor::~DeepFilterNetVstAudioProcessor() = default;

void DeepFilterNetVstAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.setSampleRate(sampleRate);
    engine_.setMaximumBlockSize(samplesPerBlock);
    engine_.prepare();
    setLatencySamples(engine_.getLatencySamples());
}

void DeepFilterNetVstAudioProcessor::releaseResources()
{
    engine_.release();
    setLatencySamples(0);
}

bool DeepFilterNetVstAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void DeepFilterNetVstAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (attenLimDbParam_ != nullptr && postFilterBetaParam_ != nullptr && reduceMaskParam_ != nullptr)
        engine_.updateParameters(attenLimDbParam_->load(),
                                 postFilterBetaParam_->load(),
                                 juce::roundToInt(reduceMaskParam_->load()));

    engine_.process(buffer);
    setLatencySamples(engine_.getLatencySamples());
}

juce::AudioProcessorEditor* DeepFilterNetVstAudioProcessor::createEditor()
{
    return new DeepFilterNetVstAudioProcessorEditor(*this);
}

bool DeepFilterNetVstAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String DeepFilterNetVstAudioProcessor::getName() const
{
    return pluginDisplayName;
}

bool DeepFilterNetVstAudioProcessor::acceptsMidi() const
{
    return false;
}

bool DeepFilterNetVstAudioProcessor::producesMidi() const
{
    return false;
}

bool DeepFilterNetVstAudioProcessor::isMidiEffect() const
{
    return false;
}

double DeepFilterNetVstAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DeepFilterNetVstAudioProcessor::getNumPrograms()
{
    return 1;
}

int DeepFilterNetVstAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DeepFilterNetVstAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String DeepFilterNetVstAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return utf8Text("默认");
}

void DeepFilterNetVstAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void DeepFilterNetVstAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = parameters_.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DeepFilterNetVstAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xmlState = getXmlFromBinary(data, sizeInBytes);
    if (xmlState == nullptr)
        return;

    if (!xmlState->hasTagName(parameters_.state.getType()))
        return;

    parameters_.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState& DeepFilterNetVstAudioProcessor::getParametersState()
{
    return parameters_;
}

double DeepFilterNetVstAudioProcessor::getCurrentSampleRateHz() const
{
    return getSampleRate();
}

bool DeepFilterNetVstAudioProcessor::isSampleRateCompatible() const
{
    return std::abs(getCurrentSampleRateHz() - targetSampleRate) <= 1.0;
}

bool DeepFilterNetVstAudioProcessor::isDenoiserReady() const
{
    return engine_.isReady();
}

juce::StringArray DeepFilterNetVstAudioProcessor::getReduceMaskChoices()
{
    return {
        utf8Text("独立（NONE）"),
        utf8Text("最大值（MAX）"),
        utf8Text("平均值（MEAN）")
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout DeepFilterNetVstAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(attenParamId, 1),
        utf8Text("降噪强度"),
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int)
            {
                return juce::String(value, 0) + " dB";
            })));

    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(postParamId, 1),
        utf8Text("后置滤波"),
        juce::NormalisableRange<float>(0.0f, 0.05f, 0.0005f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int)
            {
                return juce::String(value, 3);
            })));

    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(reduceMaskParamId, 1),
        utf8Text("声道掩码合并"),
        getReduceMaskChoices(),
        0));

    return { parameters.begin(), parameters.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DeepFilterNetVstAudioProcessor();
}

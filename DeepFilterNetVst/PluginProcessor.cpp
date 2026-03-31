#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

#if JUCE_WINDOWS
#include <Windows.h>
#endif

namespace
{
constexpr double targetSampleRate = 48000.0;
constexpr float inputActivityThreshold = 1.0e-6f;

juce::String utf8Text(const char* text)
{
    return juce::String::fromUTF8(text);
}

juce::String getWrapperTypeText(juce::AudioProcessor::WrapperType wrapperType)
{
    switch (wrapperType)
    {
        case juce::AudioProcessor::wrapperType_Undefined:  return utf8Text("未知");
        case juce::AudioProcessor::wrapperType_VST:        return utf8Text("VST");
        case juce::AudioProcessor::wrapperType_VST3:       return utf8Text("VST3");
        case juce::AudioProcessor::wrapperType_AudioUnit:  return utf8Text("Audio Unit");
        case juce::AudioProcessor::wrapperType_AudioUnitv3:return utf8Text("Audio Unit v3");
        case juce::AudioProcessor::wrapperType_AAX:        return utf8Text("AAX");
        case juce::AudioProcessor::wrapperType_Standalone: return utf8Text("独立程序");
        case juce::AudioProcessor::wrapperType_LV2:        return utf8Text("LV2");
        default:                                           return juce::AudioProcessor::getWrapperTypeDescription(wrapperType);
    }
}

juce::String getHostText(const juce::PluginHostType& hostType)
{
    const juce::String description(hostType.getHostDescription());
    return description.equalsIgnoreCase("Unknown") ? utf8Text("未知") : description;
}

bool hasRealInputSignal(const juce::AudioBuffer<float>& buffer, int inputChannelCount)
{
    const auto channelsToCheck = juce::jmin(inputChannelCount, buffer.getNumChannels());
    for (int channel = 0; channel < channelsToCheck; ++channel)
    {
        const auto* input = buffer.getReadPointer(channel);
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            if (std::abs(input[sampleIndex]) > inputActivityThreshold)
                return true;
        }
    }

    return false;
}

bool shouldDelayRuntimeInitialization(juce::AudioProcessor::WrapperType wrapperType)
{
    return wrapperType == juce::AudioProcessor::wrapperType_VST;
}

struct SharedDiagnosticSnapshot
{
    bool available = false;
    uint32_t writerProcessId = 0;
    int wrapperType = 0;
    int prepareCount = 0;
    int processCount = 0;
    int releaseCount = 0;
    double lastPreparedSampleRateHz = 0.0;
    int lastPreparedBlockSizeSamples = 0;
    double lastProcessSampleRateHz = 0.0;
    int lastProcessBlockSizeSamples = 0;
    double currentSampleRateHz = 0.0;
    int denoiserReady = 0;
    int64_t lastUpdateTimeMs = 0;
};

#if JUCE_WINDOWS
struct SharedDiagnosticState
{
    volatile LONG sequence = 0;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t writerProcessId = 0;
    int32_t wrapperType = 0;
    int32_t prepareCount = 0;
    int32_t processCount = 0;
    int32_t releaseCount = 0;
    double lastPreparedSampleRateHz = 0.0;
    int32_t lastPreparedBlockSizeSamples = 0;
    double lastProcessSampleRateHz = 0.0;
    int32_t lastProcessBlockSizeSamples = 0;
    double currentSampleRateHz = 0.0;
    int32_t denoiserReady = 0;
    int64_t lastUpdateTimeMs = 0;
};

constexpr uint32_t sharedDiagnosticMagic = 0x44464654; // DFFT
constexpr uint32_t sharedDiagnosticVersion = 1;

class SharedDiagnosticsMapping
{
public:
    static SharedDiagnosticsMapping& getInstance()
    {
        static SharedDiagnosticsMapping instance;
        return instance;
    }

    void writeSnapshot(int wrapperType,
                       int prepareCount,
                       int processCount,
                       int releaseCount,
                       double lastPreparedSampleRateHz,
                       int lastPreparedBlockSizeSamples,
                       double lastProcessSampleRateHz,
                       int lastProcessBlockSizeSamples,
                       double currentSampleRateHz,
                       bool denoiserReady)
    {
        if (state_ == nullptr)
            return;

        InterlockedIncrement(&state_->sequence);
        state_->magic = sharedDiagnosticMagic;
        state_->version = sharedDiagnosticVersion;
        state_->writerProcessId = ::GetCurrentProcessId();
        state_->wrapperType = static_cast<int32_t>(wrapperType);
        state_->prepareCount = prepareCount;
        state_->processCount = processCount;
        state_->releaseCount = releaseCount;
        state_->lastPreparedSampleRateHz = lastPreparedSampleRateHz;
        state_->lastPreparedBlockSizeSamples = lastPreparedBlockSizeSamples;
        state_->lastProcessSampleRateHz = lastProcessSampleRateHz;
        state_->lastProcessBlockSizeSamples = lastProcessBlockSizeSamples;
        state_->currentSampleRateHz = currentSampleRateHz;
        state_->denoiserReady = denoiserReady ? 1 : 0;
        state_->lastUpdateTimeMs = juce::Time::currentTimeMillis();
        InterlockedIncrement(&state_->sequence);
    }

    SharedDiagnosticSnapshot readSnapshot() const
    {
        SharedDiagnosticSnapshot snapshot;

        if (state_ == nullptr)
            return snapshot;

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto begin = state_->sequence;
            if ((begin & 1) != 0)
                continue;

            SharedDiagnosticSnapshot candidate;
            candidate.available = state_->magic == sharedDiagnosticMagic && state_->version == sharedDiagnosticVersion;
            candidate.writerProcessId = state_->writerProcessId;
            candidate.wrapperType = state_->wrapperType;
            candidate.prepareCount = state_->prepareCount;
            candidate.processCount = state_->processCount;
            candidate.releaseCount = state_->releaseCount;
            candidate.lastPreparedSampleRateHz = state_->lastPreparedSampleRateHz;
            candidate.lastPreparedBlockSizeSamples = state_->lastPreparedBlockSizeSamples;
            candidate.lastProcessSampleRateHz = state_->lastProcessSampleRateHz;
            candidate.lastProcessBlockSizeSamples = state_->lastProcessBlockSizeSamples;
            candidate.currentSampleRateHz = state_->currentSampleRateHz;
            candidate.denoiserReady = state_->denoiserReady;
            candidate.lastUpdateTimeMs = state_->lastUpdateTimeMs;

            const auto end = state_->sequence;
            if (begin == end && (end & 1) == 0)
                return candidate;
        }

        return snapshot;
    }

private:
    SharedDiagnosticsMapping()
    {
        mapping_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE,
                                        nullptr,
                                        PAGE_READWRITE,
                                        0,
                                        static_cast<DWORD>(sizeof(SharedDiagnosticState)),
                                        L"Local\\DeepFilterNetVstDiagnosticsV1");

        if (mapping_ != nullptr)
            state_ = static_cast<SharedDiagnosticState*>(::MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedDiagnosticState)));

        if (state_ != nullptr && (state_->magic != sharedDiagnosticMagic || state_->version != sharedDiagnosticVersion))
            std::memset(state_, 0, sizeof(SharedDiagnosticState));
    }

    ~SharedDiagnosticsMapping()
    {
        if (state_ != nullptr)
            ::UnmapViewOfFile(state_);

        if (mapping_ != nullptr)
            ::CloseHandle(mapping_);
    }

    HANDLE mapping_ = nullptr;
    SharedDiagnosticState* state_ = nullptr;
};
#else
class SharedDiagnosticsMapping
{
public:
    static SharedDiagnosticsMapping& getInstance()
    {
        static SharedDiagnosticsMapping instance;
        return instance;
    }

    void writeSnapshot(int, int, int, int, double, int, double, int, double, bool) {}
    SharedDiagnosticSnapshot readSnapshot() const { return {}; }
};
#endif
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
    prepareToPlayCount_.fetch_add(1);
    lastPreparedSampleRateHz_.store(sampleRate);
    lastPreparedBlockSizeSamples_.store(samplesPerBlock);

    engine_.setSampleRate(sampleRate);
    engine_.setMaximumBlockSize(samplesPerBlock);

    if (attenLimDbParam_ != nullptr && postFilterBetaParam_ != nullptr && reduceMaskParam_ != nullptr)
        engine_.updateParameters(attenLimDbParam_->load(),
                                 postFilterBetaParam_->load(),
                                 juce::roundToInt(reduceMaskParam_->load()));

    if (shouldDelayRuntimeInitialization(wrapperType))
    {
        engine_.release();
        setLatencySamples(0);
    }
    else
    {
        const auto channelCount = juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels());
        engine_.prepare(channelCount);
        setLatencySamples(engine_.getLatencySamples());
    }

    publishSharedDiagnostics();
}

void DeepFilterNetVstAudioProcessor::releaseResources()
{
    releaseResourcesCount_.fetch_add(1);
    engine_.release();
    setLatencySamples(0);
    publishSharedDiagnostics();
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

    const auto numSamples = buffer.getNumSamples();
    processBlockCount_.fetch_add(1);
    lastProcessSampleRateHz_.store(getSampleRate());
    lastProcessBlockSizeSamples_.store(numSamples);

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);

    if (attenLimDbParam_ != nullptr && postFilterBetaParam_ != nullptr && reduceMaskParam_ != nullptr)
        engine_.updateParameters(attenLimDbParam_->load(),
                                 postFilterBetaParam_->load(),
                                 juce::roundToInt(reduceMaskParam_->load()));

    if (shouldDelayRuntimeInitialization(wrapperType)
        && !hasRealInputSignal(buffer, getTotalNumInputChannels()))
    {
        setLatencySamples(engine_.getLatencySamples());
        publishSharedDiagnostics();
        return;
    }

    engine_.process(buffer);
    setLatencySamples(engine_.getLatencySamples());
    publishSharedDiagnostics();
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

juce::String DeepFilterNetVstAudioProcessor::getDiagnosticText() const
{
    juce::StringArray lines;
    const juce::PluginHostType hostType;
    const auto sharedSnapshot = SharedDiagnosticsMapping::getInstance().readSnapshot();

    lines.add(utf8Text("本地实例"));
    lines.add(utf8Text("宿主：") + getHostText(hostType));
    lines.add(utf8Text("包装：") + getWrapperTypeText(wrapperType));
    lines.add(utf8Text("准备处理次数：") + juce::String(prepareToPlayCount_.load()));
    lines.add(utf8Text("处理回调次数：") + juce::String(processBlockCount_.load()));
    lines.add(utf8Text("释放资源次数：") + juce::String(releaseResourcesCount_.load()));
    lines.add(utf8Text("最近准备处理：")
              + juce::String(lastPreparedSampleRateHz_.load(), 1)
              + utf8Text(" Hz / ")
              + juce::String(lastPreparedBlockSizeSamples_.load()));
    lines.add(utf8Text("最近处理回调：")
              + juce::String(lastProcessSampleRateHz_.load(), 1)
              + utf8Text(" Hz / ")
              + juce::String(lastProcessBlockSizeSamples_.load()));
    lines.add(utf8Text("当前采样率查询值：") + juce::String(getSampleRate(), 1));
    lines.add(utf8Text("运行时就绪：") + juce::String(isDenoiserReady() ? utf8Text("是") : utf8Text("否")));

    lines.add({});
    lines.add(utf8Text("共享实例"));

    if (!sharedSnapshot.available)
    {
        lines.add(utf8Text("共享诊断：暂无数据"));
        return lines.joinIntoString("\n");
    }

    lines.add(utf8Text("写入进程号：") + juce::String(static_cast<int>(sharedSnapshot.writerProcessId)));
    lines.add(utf8Text("共享包装：")
              + getWrapperTypeText(static_cast<juce::AudioProcessor::WrapperType>(sharedSnapshot.wrapperType)));
    lines.add(utf8Text("共享准备处理次数：") + juce::String(sharedSnapshot.prepareCount));
    lines.add(utf8Text("共享处理回调次数：") + juce::String(sharedSnapshot.processCount));
    lines.add(utf8Text("共享释放资源次数：") + juce::String(sharedSnapshot.releaseCount));
    lines.add(utf8Text("共享最近准备处理：")
              + juce::String(sharedSnapshot.lastPreparedSampleRateHz, 1)
              + utf8Text(" Hz / ")
              + juce::String(sharedSnapshot.lastPreparedBlockSizeSamples));
    lines.add(utf8Text("共享最近处理回调：")
              + juce::String(sharedSnapshot.lastProcessSampleRateHz, 1)
              + utf8Text(" Hz / ")
              + juce::String(sharedSnapshot.lastProcessBlockSizeSamples));
    lines.add(utf8Text("共享当前采样率：") + juce::String(sharedSnapshot.currentSampleRateHz, 1));
    lines.add(utf8Text("共享运行时就绪：") + juce::String(sharedSnapshot.denoiserReady != 0 ? utf8Text("是") : utf8Text("否")));
    lines.add(utf8Text("共享最近更新时间：")
              + (sharedSnapshot.lastUpdateTimeMs > 0
                     ? juce::Time(sharedSnapshot.lastUpdateTimeMs).toString(true, true, true, true)
                     : utf8Text("无")));

    return lines.joinIntoString("\n");
}

void DeepFilterNetVstAudioProcessor::publishSharedDiagnostics() const
{
    SharedDiagnosticsMapping::getInstance().writeSnapshot(static_cast<int>(wrapperType),
                                                          prepareToPlayCount_.load(),
                                                          processBlockCount_.load(),
                                                          releaseResourcesCount_.load(),
                                                          lastPreparedSampleRateHz_.load(),
                                                          lastPreparedBlockSizeSamples_.load(),
                                                          lastProcessSampleRateHz_.load(),
                                                          lastProcessBlockSizeSamples_.load(),
                                                          getSampleRate(),
                                                          isDenoiserReady());
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

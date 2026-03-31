#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#if JUCE_WINDOWS
#include <Windows.h>
#endif

namespace
{
constexpr double targetSampleRate = 48000.0;
constexpr float inputActivityThreshold = 1.0e-6f;
constexpr double silentResetThresholdSeconds = 0.5;
constexpr int maxSharedDiagnosticInstances = 32;
constexpr int64_t sharedDiagnosticMaxIdleMs = 10 * 60 * 1000;

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

int64_t getSilentResetThresholdSamples(double sampleRate)
{
    if (sampleRate <= 0.0)
        return (std::numeric_limits<int64_t>::max)();

    return static_cast<int64_t>(std::ceil(sampleRate * silentResetThresholdSeconds));
}

uint32_t allocateInstanceSerial()
{
    static std::atomic<uint32_t> nextInstanceSerial { 1 };
    return nextInstanceSerial.fetch_add(1);
}

uint32_t getCurrentProcessIdValue()
{
#if JUCE_WINDOWS
    return static_cast<uint32_t>(::GetCurrentProcessId());
#else
    return 0;
#endif
}

uint64_t createInstanceId(uint32_t processId, uint32_t instanceSerial)
{
    return (static_cast<uint64_t>(processId) << 32) | static_cast<uint64_t>(instanceSerial);
}

juce::String formatInstanceTag(uint32_t processId, uint32_t instanceSerial)
{
    return utf8Text("PID ")
           + juce::String(static_cast<int>(processId))
           + utf8Text(" / #")
           + juce::String(static_cast<int>(instanceSerial));
}

juce::String formatInstanceId(uint64_t instanceId)
{
    return "0x" + juce::String::toHexString(static_cast<juce::int64>(instanceId)).paddedLeft('0', 16);
}

juce::String makeSectionHeader(const juce::String& title)
{
    return "### " + title;
}

struct SharedDiagnosticEntry
{
    bool available = false;
    uint64_t instanceId = 0;
    uint32_t instanceSerial = 0;
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
bool isProcessAlive(uint32_t processId)
{
    if (processId == 0)
        return false;

    if (processId == getCurrentProcessIdValue())
        return true;

    HANDLE processHandle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (processHandle == nullptr)
        return false;

    DWORD exitCode = 0;
    const auto stillRunning = ::GetExitCodeProcess(processHandle, &exitCode) != FALSE && exitCode == STILL_ACTIVE;
    ::CloseHandle(processHandle);
    return stillRunning;
}

struct SharedDiagnosticEntryState
{
    uint64_t instanceId = 0;
    uint32_t instanceSerial = 0;
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

struct SharedDiagnosticState
{
    volatile LONG sequence = 0;
    uint32_t magic = 0;
    uint32_t version = 0;
    SharedDiagnosticEntryState entries[maxSharedDiagnosticInstances];
};

constexpr uint32_t sharedDiagnosticMagic = 0x44464654; // DFFT
constexpr uint32_t sharedDiagnosticVersion = 2;

class SharedDiagnosticsMapping
{
public:
    static SharedDiagnosticsMapping& getInstance()
    {
        static SharedDiagnosticsMapping instance;
        return instance;
    }

    void writeSnapshot(uint64_t instanceId,
                       uint32_t instanceSerial,
                       int wrapperType,
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

        const auto nowMs = juce::Time::currentTimeMillis();

        InterlockedIncrement(&state_->sequence);
        cleanupExpiredEntries(nowMs);
        state_->magic = sharedDiagnosticMagic;
        state_->version = sharedDiagnosticVersion;

        if (auto* entry = findOrAllocateEntry(instanceId))
        {
            entry->instanceId = instanceId;
            entry->instanceSerial = instanceSerial;
            entry->writerProcessId = getCurrentProcessIdValue();
            entry->wrapperType = static_cast<int32_t>(wrapperType);
            entry->prepareCount = prepareCount;
            entry->processCount = processCount;
            entry->releaseCount = releaseCount;
            entry->lastPreparedSampleRateHz = lastPreparedSampleRateHz;
            entry->lastPreparedBlockSizeSamples = lastPreparedBlockSizeSamples;
            entry->lastProcessSampleRateHz = lastProcessSampleRateHz;
            entry->lastProcessBlockSizeSamples = lastProcessBlockSizeSamples;
            entry->currentSampleRateHz = currentSampleRateHz;
            entry->denoiserReady = denoiserReady ? 1 : 0;
            entry->lastUpdateTimeMs = nowMs;
        }

        InterlockedIncrement(&state_->sequence);
    }

    void removeSnapshot(uint64_t instanceId)
    {
        if (state_ == nullptr)
            return;

        InterlockedIncrement(&state_->sequence);
        clearEntry(instanceId);
        InterlockedIncrement(&state_->sequence);
    }

    std::vector<SharedDiagnosticEntry> readSnapshots() const
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto begin = state_->sequence;
            if ((begin & 1) != 0)
                continue;

            std::vector<SharedDiagnosticEntry> entries;
            const auto isValidState = state_->magic == sharedDiagnosticMagic && state_->version == sharedDiagnosticVersion;
            const auto nowMs = juce::Time::currentTimeMillis();

            if (isValidState)
            {
                entries.reserve(maxSharedDiagnosticInstances);
                for (const auto& sourceEntry : state_->entries)
                {
                    if (!isEntryActive(sourceEntry, nowMs))
                        continue;

                    SharedDiagnosticEntry candidate;
                    candidate.available = true;
                    candidate.instanceId = sourceEntry.instanceId;
                    candidate.instanceSerial = sourceEntry.instanceSerial;
                    candidate.writerProcessId = sourceEntry.writerProcessId;
                    candidate.wrapperType = sourceEntry.wrapperType;
                    candidate.prepareCount = sourceEntry.prepareCount;
                    candidate.processCount = sourceEntry.processCount;
                    candidate.releaseCount = sourceEntry.releaseCount;
                    candidate.lastPreparedSampleRateHz = sourceEntry.lastPreparedSampleRateHz;
                    candidate.lastPreparedBlockSizeSamples = sourceEntry.lastPreparedBlockSizeSamples;
                    candidate.lastProcessSampleRateHz = sourceEntry.lastProcessSampleRateHz;
                    candidate.lastProcessBlockSizeSamples = sourceEntry.lastProcessBlockSizeSamples;
                    candidate.currentSampleRateHz = sourceEntry.currentSampleRateHz;
                    candidate.denoiserReady = sourceEntry.denoiserReady;
                    candidate.lastUpdateTimeMs = sourceEntry.lastUpdateTimeMs;
                    entries.push_back(candidate);
                }
            }

            const auto end = state_->sequence;
            if (begin == end && (end & 1) == 0)
            {
                std::sort(entries.begin(),
                          entries.end(),
                          [](const SharedDiagnosticEntry& left, const SharedDiagnosticEntry& right)
                          {
                              if (left.writerProcessId != right.writerProcessId)
                                  return left.writerProcessId < right.writerProcessId;

                              if (left.instanceSerial != right.instanceSerial)
                                  return left.instanceSerial < right.instanceSerial;

                              return left.instanceId < right.instanceId;
                          });
                return entries;
            }
        }

        return {};
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

    static bool isEntryActive(const SharedDiagnosticEntryState& entry, int64_t nowMs)
    {
        if (entry.instanceId == 0 || entry.writerProcessId == 0)
            return false;

        if (!isProcessAlive(entry.writerProcessId))
            return false;

        return nowMs - entry.lastUpdateTimeMs <= sharedDiagnosticMaxIdleMs;
    }

    void cleanupExpiredEntries(int64_t nowMs)
    {
        for (auto& entry : state_->entries)
        {
            if (!isEntryActive(entry, nowMs))
                std::memset(&entry, 0, sizeof(entry));
        }
    }

    SharedDiagnosticEntryState* findOrAllocateEntry(uint64_t instanceId)
    {
        SharedDiagnosticEntryState* freeEntry = nullptr;
        SharedDiagnosticEntryState* oldestEntry = nullptr;

        for (auto& entry : state_->entries)
        {
            if (entry.instanceId == instanceId && entry.instanceId != 0)
                return &entry;

            if (entry.instanceId == 0)
            {
                if (freeEntry == nullptr)
                    freeEntry = &entry;

                continue;
            }

            if (oldestEntry == nullptr || entry.lastUpdateTimeMs < oldestEntry->lastUpdateTimeMs)
                oldestEntry = &entry;
        }

        return freeEntry != nullptr ? freeEntry : oldestEntry;
    }

    void clearEntry(uint64_t instanceId)
    {
        for (auto& entry : state_->entries)
        {
            if (entry.instanceId == instanceId)
            {
                std::memset(&entry, 0, sizeof(entry));
                break;
            }
        }
    }
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

    void writeSnapshot(uint64_t, uint32_t, int, int, int, int, double, int, double, int, double, bool) {}
    void removeSnapshot(uint64_t) {}
    std::vector<SharedDiagnosticEntry> readSnapshots() const { return {}; }
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
    instanceSerial_ = allocateInstanceSerial();
    instanceId_ = createInstanceId(getCurrentProcessIdValue(), instanceSerial_);
    publishSharedDiagnostics();
}

DeepFilterNetVstAudioProcessor::~DeepFilterNetVstAudioProcessor()
{
    removeSharedDiagnostics();
}

void DeepFilterNetVstAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    prepareToPlayCount_.fetch_add(1);
    lastPreparedSampleRateHz_.store(sampleRate);
    lastPreparedBlockSizeSamples_.store(samplesPerBlock);
    consecutiveSilentInputSamples_ = 0;
    engineResetForCurrentSilence_ = false;

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
    consecutiveSilentInputSamples_ = 0;
    engineResetForCurrentSilence_ = false;
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

    const auto hasRealInput = hasRealInputSignal(buffer, getTotalNumInputChannels());

    if (shouldDelayRuntimeInitialization(wrapperType) && !hasRealInput)
    {
        if (!engine_.isReady())
        {
            setLatencySamples(engine_.getLatencySamples());
            publishSharedDiagnostics();
            return;
        }

        consecutiveSilentInputSamples_ = juce::jmin((std::numeric_limits<int64_t>::max)() - static_cast<int64_t>(numSamples),
                                                    consecutiveSilentInputSamples_)
                                         + static_cast<int64_t>(numSamples);

        if (!engineResetForCurrentSilence_
            && consecutiveSilentInputSamples_ >= getSilentResetThresholdSamples(getSampleRate()))
        {
            engine_.reset();
            engineResetForCurrentSilence_ = true;
        }

        if (engineResetForCurrentSilence_)
        {
            buffer.clear();
            setLatencySamples(engine_.getLatencySamples());
            publishSharedDiagnostics();
            return;
        }
    }
    else
    {
        consecutiveSilentInputSamples_ = 0;
        engineResetForCurrentSilence_ = false;
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
    auto sharedEntries = SharedDiagnosticsMapping::getInstance().readSnapshots();
    std::sort(sharedEntries.begin(),
              sharedEntries.end(),
              [this](const SharedDiagnosticEntry& left, const SharedDiagnosticEntry& right)
              {
                  const auto leftIsLocal = left.instanceId == instanceId_;
                  const auto rightIsLocal = right.instanceId == instanceId_;

                  if (leftIsLocal != rightIsLocal)
                      return leftIsLocal;

                  if (left.writerProcessId != right.writerProcessId)
                      return left.writerProcessId < right.writerProcessId;

                  return left.instanceSerial < right.instanceSerial;
              });

    lines.add(makeSectionHeader(utf8Text("本地实例 ") + formatInstanceTag(getCurrentProcessIdValue(), instanceSerial_)));
    lines.add(utf8Text("实例 ID：") + formatInstanceId(instanceId_));
    lines.add(utf8Text("进程号：") + juce::String(static_cast<int>(getCurrentProcessIdValue())));
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

    if (sharedEntries.empty())
    {
        return lines.joinIntoString("\n");
    }

    for (const auto& sharedEntry : sharedEntries)
    {
        lines.add({});
        lines.add(makeSectionHeader(utf8Text("共享实例 ")
                                    + formatInstanceTag(sharedEntry.writerProcessId, sharedEntry.instanceSerial)
                                    + (sharedEntry.instanceId == instanceId_ ? utf8Text("（本地）") : juce::String())));
        lines.add(utf8Text("实例 ID：") + formatInstanceId(sharedEntry.instanceId));
        lines.add(utf8Text("进程号：") + juce::String(static_cast<int>(sharedEntry.writerProcessId)));
        lines.add(utf8Text("包装：")
                  + getWrapperTypeText(static_cast<juce::AudioProcessor::WrapperType>(sharedEntry.wrapperType)));
        lines.add(utf8Text("准备处理次数：") + juce::String(sharedEntry.prepareCount));
        lines.add(utf8Text("处理回调次数：") + juce::String(sharedEntry.processCount));
        lines.add(utf8Text("释放资源次数：") + juce::String(sharedEntry.releaseCount));
        lines.add(utf8Text("最近准备处理：")
                  + juce::String(sharedEntry.lastPreparedSampleRateHz, 1)
                  + utf8Text(" Hz / ")
                  + juce::String(sharedEntry.lastPreparedBlockSizeSamples));
        lines.add(utf8Text("最近处理回调：")
                  + juce::String(sharedEntry.lastProcessSampleRateHz, 1)
                  + utf8Text(" Hz / ")
                  + juce::String(sharedEntry.lastProcessBlockSizeSamples));
        lines.add(utf8Text("当前采样率：") + juce::String(sharedEntry.currentSampleRateHz, 1));
        lines.add(utf8Text("运行时就绪：") + juce::String(sharedEntry.denoiserReady != 0 ? utf8Text("是") : utf8Text("否")));
        lines.add(utf8Text("最近更新时间：")
                  + (sharedEntry.lastUpdateTimeMs > 0
                         ? juce::Time(sharedEntry.lastUpdateTimeMs).toString(true, true, true, true)
                         : utf8Text("无")));
    }

    return lines.joinIntoString("\n");
}

void DeepFilterNetVstAudioProcessor::publishSharedDiagnostics() const
{
    SharedDiagnosticsMapping::getInstance().writeSnapshot(instanceId_,
                                                          instanceSerial_,
                                                          static_cast<int>(wrapperType),
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

void DeepFilterNetVstAudioProcessor::removeSharedDiagnostics() const
{
    SharedDiagnosticsMapping::getInstance().removeSnapshot(instanceId_);
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

#include "DenoiseEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dfvst
{
namespace
{
size_t estimateResampledCount(size_t inputCount, double inputRate, double outputRate)
{
    if (inputCount == 0 || inputRate <= 0.0 || outputRate <= 0.0)
        return 0;

    return static_cast<size_t>(std::ceil((static_cast<double>(inputCount) * outputRate) / inputRate)) + 8;
}

int clampReduceMask(int reduceMask)
{
    return juce::jlimit(0, 2, reduceMask);
}
}

DenoiseEngine::~DenoiseEngine()
{
    shutdown();
}

void DenoiseEngine::setSampleRate(double sampleRate)
{
    sampleRate_ = sampleRate;
}

void DenoiseEngine::setMaximumBlockSize(int maximumBlockSize)
{
    maximumBlockSize_ = std::max(maximumBlockSize, 0);

    for (auto& channelState : channelStates_)
        resizeChannelBuffers(channelState);

    const auto blockSize = static_cast<size_t>(maximumBlockSize_);
    const auto runtimeRate = runtimeSampleRate_ > 0.0 ? runtimeSampleRate_ : fallbackTargetSampleRate;
    if (blockSize == 0 || sampleRate_ <= 0.0)
        return;

    inputResampler_.reserve(blockSize + 8);
    outputResampler_.reserve(estimateResampledCount(blockSize, runtimeRate, sampleRate_) + 8);
}

void DenoiseEngine::prepare()
{
    shutdown();
}

void DenoiseEngine::reset()
{
    for (auto& channelState : channelStates_)
    {
        channelState.inputQueue.clear();
        channelState.outputQueue.clear();
    }

    inputResampler_.clear();
    outputResampler_.clear();
    primed_ = false;
    std::fill(frameInput_.begin(), frameInput_.end(), 0.0f);
    std::fill(frameOutput_.begin(), frameOutput_.end(), 0.0f);
}

void DenoiseEngine::release()
{
    shutdown();
}

void DenoiseEngine::updateParameters(float attenLimDb, float postFilterBeta, int reduceMask)
{
    attenLimDb_ = juce::jlimit(0.0f, 100.0f, attenLimDb);
    postFilterBeta_ = juce::jlimit(0.0f, 0.05f, postFilterBeta);
    reduceMask_ = clampReduceMask(reduceMask);

    if (state_ != nullptr && reduceMask_ != reduceMaskApplied_)
    {
        shutdown();
        return;
    }

    applyParameters(false);
}

void DenoiseEngine::process(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (sampleRate_ <= 0.0)
        return;

    if (channelCount_ != numChannels)
    {
        shutdown();
        channelCount_ = 0;
    }

    if (!ready_ && !ensureInitialized(numChannels))
        return;

    if (static_cast<int>(channelStates_.size()) != numChannels)
        return;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto& channelState = channelStates_[static_cast<size_t>(channel)];
        if (channelState.scratchInput.size() < static_cast<size_t>(numSamples))
            channelState.scratchInput.resize(static_cast<size_t>(numSamples));

        std::memcpy(channelState.scratchInput.data(),
                    buffer.getReadPointer(channel),
                    static_cast<size_t>(numSamples) * sizeof(float));
        channelReadPointers_[static_cast<size_t>(channel)] = channelState.scratchInput.data();
    }

    inputResampler_.push(channelReadPointers_, static_cast<size_t>(numSamples));
    inputResampler_.drainAvailable(resampledInput_);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto& channelState = channelStates_[static_cast<size_t>(channel)];
        auto& resampledInput = resampledInput_[static_cast<size_t>(channel)];

        if (!resampledInput.empty())
            channelState.inputQueue.push(resampledInput.data(), resampledInput.size());
    }

    while (std::all_of(channelStates_.begin(),
                       channelStates_.end(),
                       [this](const ChannelState& channelState)
                       {
                           return channelState.inputQueue.size() >= static_cast<size_t>(frameSize_);
                       }))
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& channelState = channelStates_[static_cast<size_t>(channel)];
            auto* frameInput = frameInput_.data() + static_cast<size_t>(channel) * static_cast<size_t>(frameSize_);
            channelState.inputQueue.pop(frameInput, static_cast<size_t>(frameSize_));
        }

        dfvst_process_frame(state_, frameInput_.data(), frameOutput_.data());

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& channelState = channelStates_[static_cast<size_t>(channel)];
            const auto* frameOutput = frameOutput_.data() + static_cast<size_t>(channel) * static_cast<size_t>(frameSize_);
            channelState.outputQueue.push(frameOutput, static_cast<size_t>(frameSize_));
        }
    }

    auto processedSamplesAvailable = static_cast<size_t>(-1);
    for (const auto& channelState : channelStates_)
    {
        processedSamplesAvailable = std::min(processedSamplesAvailable, channelState.outputQueue.size());
    }

    if (processedSamplesAvailable != static_cast<size_t>(-1) && processedSamplesAvailable > 0)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& channelState = channelStates_[static_cast<size_t>(channel)];
            auto& resampledOutput = resampledOutput_[static_cast<size_t>(channel)];
            resampledOutput.resize(processedSamplesAvailable);
            const auto popped = channelState.outputQueue.pop(resampledOutput.data(), processedSamplesAvailable);
            juce::ignoreUnused(popped);
            jassert(popped == processedSamplesAvailable);
            channelReadPointers_[static_cast<size_t>(channel)] = resampledOutput.data();
        }

        outputResampler_.push(channelReadPointers_, processedSamplesAvailable);
    }

    if (!primed_)
    {
        primed_ = outputResampler_.hasBufferedInput();
    }

    if (!primed_)
    {
        buffer.clear();
        return;
    }

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto& channelState = channelStates_[static_cast<size_t>(channel)];
        if (channelState.scratchOutput.size() < static_cast<size_t>(numSamples))
            channelState.scratchOutput.resize(static_cast<size_t>(numSamples));

        channelWritePointers_[static_cast<size_t>(channel)] = channelState.scratchOutput.data();
    }

    const auto written = outputResampler_.produce(channelWritePointers_, static_cast<size_t>(numSamples));

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto& channelState = channelStates_[static_cast<size_t>(channel)];
        if (written < static_cast<size_t>(numSamples))
        {
            std::fill(channelState.scratchOutput.begin() + static_cast<std::ptrdiff_t>(written),
                      channelState.scratchOutput.begin() + numSamples,
                      0.0f);
        }

        buffer.copyFrom(channel, 0, channelState.scratchOutput.data(), numSamples);
    }
}

bool DenoiseEngine::isSampleRateSupported() const
{
    return sampleRate_ > 0.0;
}

bool DenoiseEngine::isReady() const
{
    return ready_;
}

int DenoiseEngine::getLatencySamples() const
{
    if (!ready_ || sampleRate_ <= 0.0 || runtimeSampleRate_ <= 0.0)
        return 0;

    const auto hostSamples = (static_cast<double>(frameSize_) * sampleRate_) / runtimeSampleRate_;
    return juce::jmax(0, juce::roundToInt(std::ceil(hostSamples)) + 1);
}

bool DenoiseEngine::ensureInitialized(int channelCount)
{
    if (ready_)
        return true;

    if (initAttempted_ || !isSampleRateSupported() || channelCount <= 0)
        return false;

    initAttempted_ = true;
    ensureChannelStates(channelCount);
    state_ = dfvst_create(static_cast<size_t>(channelCount_), attenLimDb_, postFilterBeta_, reduceMask_);
    if (state_ == nullptr)
        return false;

    frameSize_ = static_cast<int>(dfvst_get_frame_length(state_));
    runtimeSampleRate_ = static_cast<double>(dfvst_get_sample_rate(state_));

    if (frameSize_ <= 0 || runtimeSampleRate_ <= 0.0
        || static_cast<int>(dfvst_get_channel_count(state_)) != channelCount_)
    {
        shutdown();
        return false;
    }

    reduceMaskApplied_ = reduceMask_;
    frameInput_.assign(static_cast<size_t>(frameSize_) * static_cast<size_t>(channelCount_), 0.0f);
    frameOutput_.assign(static_cast<size_t>(frameSize_) * static_cast<size_t>(channelCount_), 0.0f);
    inputResampler_.reset(sampleRate_, runtimeSampleRate_, static_cast<size_t>(channelCount_));
    outputResampler_.reset(runtimeSampleRate_, sampleRate_, static_cast<size_t>(channelCount_));

    for (auto& channelState : channelStates_)
    {
        channelState.inputQueue.clear();
        channelState.outputQueue.clear();
        channelState.inputQueue.reserve(static_cast<size_t>(frameSize_) * queueReserveMultiplier);
        channelState.outputQueue.reserve(static_cast<size_t>(frameSize_) * queueReserveMultiplier);
        resizeChannelBuffers(channelState);
    }

    applyParameters(true);
    primed_ = false;
    ready_ = true;
    return true;
}

void DenoiseEngine::shutdown()
{
    if (state_ != nullptr)
    {
        dfvst_free(state_);
        state_ = nullptr;
    }

    ready_ = false;
    initAttempted_ = false;
    primed_ = false;
    frameSize_ = 0;
    runtimeSampleRate_ = fallbackTargetSampleRate;
    reduceMaskApplied_ = -1;
    frameInput_.clear();
    frameOutput_.clear();
    inputResampler_.clear();
    outputResampler_.clear();

    for (auto& channelState : channelStates_)
    {
        channelState.inputQueue.clear();
        channelState.outputQueue.clear();
    }
}

void DenoiseEngine::applyParameters(bool force)
{
    if (state_ == nullptr)
        return;

    if (force || std::abs(attenLimDb_ - attenLimApplied_) > 1.0e-3f)
    {
        dfvst_set_atten_lim(state_, attenLimDb_);
        attenLimApplied_ = attenLimDb_;
    }

    if (force || std::abs(postFilterBeta_ - postFilterApplied_) > 1.0e-6f)
    {
        dfvst_set_post_filter_beta(state_, postFilterBeta_);
        postFilterApplied_ = postFilterBeta_;
    }
}

void DenoiseEngine::ensureChannelStates(int channelCount)
{
    channelCount_ = std::max(channelCount, 0);
    channelStates_.resize(static_cast<size_t>(channelCount_));
    resampledInput_.resize(static_cast<size_t>(channelCount_));
    resampledOutput_.resize(static_cast<size_t>(channelCount_));
    channelReadPointers_.resize(static_cast<size_t>(channelCount_));
    channelWritePointers_.resize(static_cast<size_t>(channelCount_));

    for (auto& channelState : channelStates_)
        resizeChannelBuffers(channelState);
}

void DenoiseEngine::resizeChannelBuffers(ChannelState& channelState)
{
    channelState.scratchInput.resize(static_cast<size_t>(maximumBlockSize_));
    channelState.scratchOutput.resize(static_cast<size_t>(maximumBlockSize_));

    const auto blockSize = static_cast<size_t>(maximumBlockSize_);
    if (blockSize == 0 || sampleRate_ <= 0.0)
        return;

    channelState.scratchInput.reserve(blockSize);
    channelState.scratchOutput.reserve(blockSize);
}

void DenoiseEngine::FloatQueue::clear()
{
    data_.clear();
    readPosition_ = 0;
}

void DenoiseEngine::FloatQueue::reserve(size_t count)
{
    data_.reserve(count);
}

size_t DenoiseEngine::FloatQueue::size() const
{
    return data_.size() - readPosition_;
}

float DenoiseEngine::FloatQueue::get(size_t index) const
{
    jassert(index < size());
    return data_[readPosition_ + index];
}

void DenoiseEngine::FloatQueue::discard(size_t count)
{
    readPosition_ += std::min(count, size());
    compact();
}

void DenoiseEngine::FloatQueue::push(const float* data, size_t count)
{
    if (data == nullptr || count == 0)
        return;

    data_.insert(data_.end(), data, data + count);
}

size_t DenoiseEngine::FloatQueue::pop(float* destination, size_t count)
{
    const auto available = size();
    const auto toCopy = std::min(count, available);

    if (toCopy > 0)
    {
        std::memcpy(destination, data_.data() + readPosition_, toCopy * sizeof(float));
        readPosition_ += toCopy;
        compact();
    }

    return toCopy;
}

void DenoiseEngine::FloatQueue::compact()
{
    if (readPosition_ == 0)
        return;

    if (readPosition_ > 4096 || readPosition_ > data_.size() / 2)
    {
        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(readPosition_));
        readPosition_ = 0;
    }
}

void DenoiseEngine::SharedLinearResampler::reset(double inputSampleRate, double outputSampleRate, size_t channelCount)
{
    inputSamplesPerOutputSample_ = 1.0;

    if (inputSampleRate > 0.0 && outputSampleRate > 0.0)
        inputSamplesPerOutputSample_ = inputSampleRate / outputSampleRate;

    ensureChannelCount(channelCount);
    clear();
}

void DenoiseEngine::SharedLinearResampler::clear()
{
    for (auto& inputQueue : inputQueues_)
        inputQueue.clear();

    sourcePosition_ = 0.0;
}

void DenoiseEngine::SharedLinearResampler::reserve(size_t count)
{
    for (auto& inputQueue : inputQueues_)
        inputQueue.reserve(count);
}

void DenoiseEngine::SharedLinearResampler::push(const std::vector<const float*>& channelData, size_t count)
{
    jassert(channelData.size() == inputQueues_.size());

    if (channelData.size() != inputQueues_.size())
        return;

    for (size_t channel = 0; channel < inputQueues_.size(); ++channel)
        inputQueues_[channel].push(channelData[channel], count);
}

void DenoiseEngine::SharedLinearResampler::drainAvailable(std::vector<std::vector<float>>& destination)
{
    destination.resize(inputQueues_.size());
    for (auto& channelDestination : destination)
        channelDestination.clear();

    if (inputQueues_.empty())
        return;

    const auto availableInput = inputQueues_.front().size();
    const auto estimatedCount = inputSamplesPerOutputSample_ > 0.0
        ? static_cast<size_t>(std::ceil((static_cast<double>(availableInput) + 1.0) / inputSamplesPerOutputSample_))
        : 0;

    for (auto& channelDestination : destination)
    {
        if (channelDestination.capacity() < estimatedCount)
            channelDestination.reserve(estimatedCount);
    }

    while (canProduce())
    {
        const auto baseIndex = static_cast<size_t>(sourcePosition_);
        const auto fraction = static_cast<float>(sourcePosition_ - static_cast<double>(baseIndex));

        for (size_t channel = 0; channel < inputQueues_.size(); ++channel)
        {
            const auto first = inputQueues_[channel].get(baseIndex);
            const auto second = inputQueues_[channel].get(baseIndex + 1);
            destination[channel].push_back(first + fraction * (second - first));
        }

        sourcePosition_ += inputSamplesPerOutputSample_;
        discardConsumedInput();
    }
}

size_t DenoiseEngine::SharedLinearResampler::produce(const std::vector<float*>& destination, size_t maxOutputSamples)
{
    if (destination.size() != inputQueues_.size() || maxOutputSamples == 0)
        return 0;

    size_t produced = 0;

    while (produced < maxOutputSamples && canProduce())
    {
        const auto baseIndex = static_cast<size_t>(sourcePosition_);
        const auto fraction = static_cast<float>(sourcePosition_ - static_cast<double>(baseIndex));

        for (size_t channel = 0; channel < inputQueues_.size(); ++channel)
        {
            const auto first = inputQueues_[channel].get(baseIndex);
            const auto second = inputQueues_[channel].get(baseIndex + 1);
            destination[channel][produced] = first + fraction * (second - first);
        }

        ++produced;
        sourcePosition_ += inputSamplesPerOutputSample_;
        discardConsumedInput();
    }

    return produced;
}

bool DenoiseEngine::SharedLinearResampler::hasBufferedInput() const
{
    return canProduce();
}

bool DenoiseEngine::SharedLinearResampler::canProduce() const
{
    if (inputQueues_.empty())
        return false;

    auto available = inputQueues_.front().size();
    for (const auto& inputQueue : inputQueues_)
        available = std::min(available, inputQueue.size());

    if (available < 2)
        return false;

    const auto baseIndex = static_cast<size_t>(sourcePosition_);
    return baseIndex + 1 < available;
}

void DenoiseEngine::SharedLinearResampler::ensureChannelCount(size_t channelCount)
{
    inputQueues_.resize(channelCount);
}

void DenoiseEngine::SharedLinearResampler::discardConsumedInput()
{
    const auto discardCount = static_cast<size_t>(sourcePosition_);
    if (discardCount > 0)
    {
        for (auto& inputQueue : inputQueues_)
            inputQueue.discard(discardCount);

        sourcePosition_ -= static_cast<double>(discardCount);
    }
}
}

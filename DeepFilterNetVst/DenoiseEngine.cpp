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
}

void DenoiseEngine::prepare()
{
    shutdown();
}

void DenoiseEngine::reset()
{
    for (auto& channelState : channelStates_)
    {
        channelState.inputResampler.clear();
        channelState.outputResampler.clear();
        channelState.inputQueue.clear();
        channelState.outputQueue.clear();
    }

    primed_ = false;
    std::fill(frameInput_.begin(), frameInput_.end(), 0.0f);
    std::fill(frameOutput_.begin(), frameOutput_.end(), 0.0f);
}

void DenoiseEngine::release()
{
    shutdown();
}

void DenoiseEngine::updateParameters(float attenLimDb, float postFilterBeta)
{
    attenLimDb_ = juce::jlimit(0.0f, 100.0f, attenLimDb);
    postFilterBeta_ = juce::jlimit(0.0f, 0.05f, postFilterBeta);
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

        channelState.inputResampler.push(channelState.scratchInput.data(), static_cast<size_t>(numSamples));
        channelState.inputResampler.drainAvailable(channelState.resampledInput);

        if (!channelState.resampledInput.empty())
            channelState.inputQueue.push(channelState.resampledInput.data(), channelState.resampledInput.size());
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

    for (auto& channelState : channelStates_)
    {
        const auto processedSamplesAvailable = channelState.outputQueue.size();
        if (processedSamplesAvailable == 0)
            continue;

        channelState.resampledOutput.resize(processedSamplesAvailable);
        const auto popped = channelState.outputQueue.pop(channelState.resampledOutput.data(), processedSamplesAvailable);
        channelState.outputResampler.push(channelState.resampledOutput.data(), popped);
    }

    if (!primed_)
    {
        primed_ = std::all_of(channelStates_.begin(),
                              channelStates_.end(),
                              [](const ChannelState& channelState)
                              {
                                  return channelState.outputResampler.hasBufferedInput();
                              });
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

        const auto written = channelState.outputResampler.produce(channelState.scratchOutput.data(),
                                                                  static_cast<size_t>(numSamples));
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
    state_ = dfvst_create(static_cast<size_t>(channelCount_), attenLimDb_, postFilterBeta_, runtimeReduceMask);
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

    frameInput_.assign(static_cast<size_t>(frameSize_) * static_cast<size_t>(channelCount_), 0.0f);
    frameOutput_.assign(static_cast<size_t>(frameSize_) * static_cast<size_t>(channelCount_), 0.0f);

    for (auto& channelState : channelStates_)
    {
        channelState.inputResampler.reset(sampleRate_, runtimeSampleRate_);
        channelState.outputResampler.reset(runtimeSampleRate_, sampleRate_);
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
    frameInput_.clear();
    frameOutput_.clear();

    for (auto& channelState : channelStates_)
    {
        channelState.inputResampler.clear();
        channelState.outputResampler.clear();
        channelState.inputQueue.clear();
        channelState.outputQueue.clear();
        channelState.resampledInput.clear();
        channelState.resampledOutput.clear();
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

    for (auto& channelState : channelStates_)
        resizeChannelBuffers(channelState);
}

void DenoiseEngine::resizeChannelBuffers(ChannelState& channelState)
{
    channelState.scratchInput.resize(static_cast<size_t>(maximumBlockSize_));
    channelState.scratchOutput.resize(static_cast<size_t>(maximumBlockSize_));

    const auto blockSize = static_cast<size_t>(maximumBlockSize_);
    const auto runtimeRate = runtimeSampleRate_ > 0.0 ? runtimeSampleRate_ : fallbackTargetSampleRate;
    if (blockSize == 0 || sampleRate_ <= 0.0)
        return;

    channelState.inputResampler.reserve(blockSize + 8);
    channelState.outputResampler.reserve(estimateResampledCount(blockSize, sampleRate_, runtimeRate) + 8);
    channelState.resampledInput.reserve(estimateResampledCount(blockSize, sampleRate_, runtimeRate));
    channelState.resampledOutput.reserve(estimateResampledCount(blockSize, runtimeRate, sampleRate_));
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

void DenoiseEngine::StreamingLinearResampler::reset(double inputSampleRate, double outputSampleRate)
{
    inputSamplesPerOutputSample_ = 1.0;

    if (inputSampleRate > 0.0 && outputSampleRate > 0.0)
        inputSamplesPerOutputSample_ = inputSampleRate / outputSampleRate;

    clear();
}

void DenoiseEngine::StreamingLinearResampler::clear()
{
    inputQueue_.clear();
    sourcePosition_ = 0.0;
}

void DenoiseEngine::StreamingLinearResampler::reserve(size_t count)
{
    inputQueue_.reserve(count);
}

void DenoiseEngine::StreamingLinearResampler::push(const float* data, size_t count)
{
    inputQueue_.push(data, count);
}

void DenoiseEngine::StreamingLinearResampler::drainAvailable(std::vector<float>& destination)
{
    destination.clear();

    const auto availableInput = inputQueue_.size();
    const auto estimatedCount = inputSamplesPerOutputSample_ > 0.0
        ? static_cast<size_t>(std::ceil((static_cast<double>(availableInput) + 1.0) / inputSamplesPerOutputSample_))
        : 0;

    if (destination.capacity() < estimatedCount)
        destination.reserve(estimatedCount);

    while (canProduce())
        destination.push_back(produceOne());
}

size_t DenoiseEngine::StreamingLinearResampler::produce(float* destination, size_t maxOutputSamples)
{
    if (destination == nullptr || maxOutputSamples == 0)
        return 0;

    size_t produced = 0;

    while (produced < maxOutputSamples && canProduce())
        destination[produced++] = produceOne();

    return produced;
}

bool DenoiseEngine::StreamingLinearResampler::hasBufferedInput() const
{
    return inputQueue_.size() > 0;
}

bool DenoiseEngine::StreamingLinearResampler::canProduce() const
{
    const auto available = inputQueue_.size();
    if (available < 2)
        return false;

    const auto baseIndex = static_cast<size_t>(sourcePosition_);
    return baseIndex + 1 < available;
}

float DenoiseEngine::StreamingLinearResampler::produceOne()
{
    const auto baseIndex = static_cast<size_t>(sourcePosition_);
    const auto fraction = static_cast<float>(sourcePosition_ - static_cast<double>(baseIndex));
    const auto first = inputQueue_.get(baseIndex);
    const auto second = inputQueue_.get(baseIndex + 1);
    const auto value = first + fraction * (second - first);

    sourcePosition_ += inputSamplesPerOutputSample_;

    const auto discardCount = static_cast<size_t>(sourcePosition_);
    if (discardCount > 0)
    {
        inputQueue_.discard(discardCount);
        sourcePosition_ -= static_cast<double>(discardCount);
    }

    return value;
}
}

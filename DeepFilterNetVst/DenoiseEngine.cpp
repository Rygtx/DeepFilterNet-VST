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

void DenoiseEngine::prepare(int channelCount)
{
    shutdown();

    if (channelCount > 0)
        (void) ensureInitialized(channelCount);
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

    const auto modelLatencyHostSamples = (static_cast<double>(frameSize_) * sampleRate_) / runtimeSampleRate_;
    const auto inputResamplerDelayHostSamples =
        (static_cast<double>(inputResampler_.getOutputDelay()) * sampleRate_) / runtimeSampleRate_;
    const auto outputResamplerDelayHostSamples = static_cast<double>(outputResampler_.getOutputDelay());

    return juce::jmax(0,
                      juce::roundToInt(std::ceil(modelLatencyHostSamples
                                                 + inputResamplerDelayHostSamples
                                                 + outputResamplerDelayHostSamples))
                          + 1);
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
    if (!inputResampler_.reset(SharedRubatoResampler::Mode::fixedOut,
                               sampleRate_,
                               runtimeSampleRate_,
                               static_cast<size_t>(frameSize_),
                               static_cast<size_t>(channelCount_))
        || !outputResampler_.reset(SharedRubatoResampler::Mode::fixedIn,
                                   runtimeSampleRate_,
                                   sampleRate_,
                                   static_cast<size_t>(frameSize_),
                                   static_cast<size_t>(channelCount_)))
    {
        shutdown();
        return false;
    }

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
    inputResampler_.release();
    outputResampler_.release();

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

DenoiseEngine::SharedRubatoResampler::~SharedRubatoResampler()
{
    destroyState();
}

bool DenoiseEngine::SharedRubatoResampler::reset(Mode mode,
                                                 double inputSampleRate,
                                                 double outputSampleRate,
                                                 size_t chunkSize,
                                                 size_t channelCount)
{
    release();
    ensureChannelCount(channelCount);

    mode_ = mode;
    channelCount_ = channelCount;

    if (channelCount_ == 0 || chunkSize == 0 || inputSampleRate <= 0.0 || outputSampleRate <= 0.0)
        return false;

    passthrough_ = std::abs(inputSampleRate - outputSampleRate) <= 1.0e-6;
    if (passthrough_)
    {
        inputFramesMax_ = chunkSize;
        inputFramesNext_ = chunkSize;
        outputFramesMax_ = chunkSize;
        outputDelay_ = 0;
        processInput_.assign(channelCount_ * inputFramesMax_, 0.0f);
        processOutput_.assign(channelCount_ * outputFramesMax_, 0.0f);
        clear();
        return true;
    }

    const auto inputRate = static_cast<size_t>(juce::roundToInt(inputSampleRate));
    const auto outputRate = static_cast<size_t>(juce::roundToInt(outputSampleRate));

    if (mode_ == Mode::fixedIn)
    {
        state_ = dfvst_resampler_create_fixed_in(inputRate, outputRate, chunkSize, 1, channelCount_);
    }
    else
    {
        state_ = dfvst_resampler_create_fixed_out(inputRate, outputRate, chunkSize, 1, channelCount_);
    }

    if (state_ == nullptr)
    {
        release();
        return false;
    }

    refreshFrameCounts();
    processInput_.assign(channelCount_ * inputFramesMax_, 0.0f);
    processOutput_.assign(channelCount_ * outputFramesMax_, 0.0f);
    clear();
    return true;
}

void DenoiseEngine::SharedRubatoResampler::clear()
{
    for (auto& inputQueue : inputQueues_)
        inputQueue.clear();

    for (auto& outputQueue : outputQueues_)
        outputQueue.clear();

    if (state_ != nullptr)
    {
        dfvst_resampler_reset(state_);
        refreshFrameCounts();
    }
}

void DenoiseEngine::SharedRubatoResampler::release()
{
    destroyState();

    for (auto& inputQueue : inputQueues_)
        inputQueue.clear();

    for (auto& outputQueue : outputQueues_)
        outputQueue.clear();

    inputQueues_.clear();
    outputQueues_.clear();
    processInput_.clear();
    processOutput_.clear();
    channelCount_ = 0;
    passthrough_ = false;
    inputFramesMax_ = 0;
    inputFramesNext_ = 0;
    outputFramesMax_ = 0;
    outputDelay_ = 0;
}

void DenoiseEngine::SharedRubatoResampler::reserve(size_t count)
{
    for (auto& inputQueue : inputQueues_)
        inputQueue.reserve(count);

    for (auto& outputQueue : outputQueues_)
        outputQueue.reserve(count);
}

void DenoiseEngine::SharedRubatoResampler::push(const std::vector<const float*>& channelData, size_t count)
{
    jassert(channelData.size() == channelCount_);

    if (channelData.size() != channelCount_ || count == 0)
        return;

    if (passthrough_)
    {
        for (size_t channel = 0; channel < channelCount_; ++channel)
            outputQueues_[channel].push(channelData[channel], count);

        return;
    }

    for (size_t channel = 0; channel < channelCount_; ++channel)
        inputQueues_[channel].push(channelData[channel], count);

    processAvailableInput();
}

void DenoiseEngine::SharedRubatoResampler::drainAvailable(std::vector<std::vector<float>>& destination)
{
    destination.resize(channelCount_);
    for (auto& channelDestination : destination)
        channelDestination.clear();

    processAvailableInput();

    const auto available = getAvailableOutputSamples();
    if (available == 0)
        return;

    for (size_t channel = 0; channel < channelCount_; ++channel)
    {
        destination[channel].resize(available);
        const auto popped = outputQueues_[channel].pop(destination[channel].data(), available);
        juce::ignoreUnused(popped);
        jassert(popped == available);
    }
}

size_t DenoiseEngine::SharedRubatoResampler::produce(const std::vector<float*>& destination, size_t maxOutputSamples)
{
    if (destination.size() != channelCount_ || maxOutputSamples == 0)
        return 0;

    processAvailableInput();

    const auto available = std::min(maxOutputSamples, getAvailableOutputSamples());
    if (available == 0)
        return 0;

    for (size_t channel = 0; channel < channelCount_; ++channel)
    {
        const auto popped = outputQueues_[channel].pop(destination[channel], available);
        juce::ignoreUnused(popped);
        jassert(popped == available);
    }

    return available;
}

bool DenoiseEngine::SharedRubatoResampler::hasBufferedInput()
{
    processAvailableInput();
    return getAvailableOutputSamples() > 0;
}

size_t DenoiseEngine::SharedRubatoResampler::getOutputDelay() const
{
    return outputDelay_;
}

void DenoiseEngine::SharedRubatoResampler::destroyState()
{
    if (state_ != nullptr)
    {
        dfvst_resampler_free(state_);
        state_ = nullptr;
    }
}

void DenoiseEngine::SharedRubatoResampler::ensureChannelCount(size_t channelCount)
{
    inputQueues_.resize(channelCount);
    outputQueues_.resize(channelCount);
}

void DenoiseEngine::SharedRubatoResampler::refreshFrameCounts()
{
    if (state_ == nullptr)
        return;

    inputFramesMax_ = dfvst_resampler_get_input_frames_max(state_);
    inputFramesNext_ = dfvst_resampler_get_input_frames_next(state_);
    outputFramesMax_ = dfvst_resampler_get_output_frames_max(state_);
    outputDelay_ = dfvst_resampler_get_output_delay(state_);
}

void DenoiseEngine::SharedRubatoResampler::processAvailableInput()
{
    if (passthrough_ || state_ == nullptr)
        return;

    while (canProcessInput())
    {
        processInput_.resize(channelCount_ * inputFramesNext_);
        processOutput_.resize(channelCount_ * outputFramesMax_);

        for (size_t channel = 0; channel < channelCount_; ++channel)
        {
            auto* channelInput = processInput_.data() + channel * inputFramesNext_;
            const auto popped = inputQueues_[channel].pop(channelInput, inputFramesNext_);
            juce::ignoreUnused(popped);
            jassert(popped == inputFramesNext_);
        }

        const auto produced = dfvst_resampler_process(state_,
                                                      processInput_.data(),
                                                      inputFramesNext_,
                                                      processOutput_.data(),
                                                      outputFramesMax_);
        jassert(produced > 0);
        if (produced == 0)
            break;

        for (size_t channel = 0; channel < channelCount_; ++channel)
        {
            const auto* channelOutput = processOutput_.data() + channel * outputFramesMax_;
            outputQueues_[channel].push(channelOutput, produced);
        }

        refreshFrameCounts();
    }
}

size_t DenoiseEngine::SharedRubatoResampler::getAvailableOutputSamples() const
{
    if (outputQueues_.empty())
        return 0;

    auto available = outputQueues_.front().size();
    for (const auto& outputQueue : outputQueues_)
        available = std::min(available, outputQueue.size());

    return available;
}

bool DenoiseEngine::SharedRubatoResampler::canProcessInput() const
{
    if (state_ == nullptr || inputQueues_.empty() || inputFramesNext_ == 0 || outputFramesMax_ == 0)
        return false;

    auto available = inputQueues_.front().size();
    for (const auto& inputQueue : inputQueues_)
        available = std::min(available, inputQueue.size());

    return available >= inputFramesNext_;
}
}

#include "DenoiseEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dfvst
{
namespace
{
template <typename T>
T getFunction(juce::DynamicLibrary& library, const char* name)
{
    return reinterpret_cast<T>(library.getFunction(name));
}

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
    scratchInput_.resize(static_cast<size_t>(maximumBlockSize_));
    scratchOutput_.resize(static_cast<size_t>(maximumBlockSize_));

    const auto blockSize = static_cast<size_t>(maximumBlockSize_);
    if (blockSize == 0 || sampleRate_ <= 0.0)
        return;

    inputResampler_.reserve(blockSize + 8);
    outputResampler_.reserve(estimateResampledCount(blockSize, sampleRate_, targetSampleRate) + 8);
    resampledInput_.reserve(estimateResampledCount(blockSize, sampleRate_, targetSampleRate));
    resampledOutput_.reserve(estimateResampledCount(blockSize, targetSampleRate, sampleRate_));
}

void DenoiseEngine::prepare()
{
    shutdown();
    ensureInitialized();
}

void DenoiseEngine::reset()
{
    inputResampler_.clear();
    outputResampler_.clear();
    inputQueue_.clear();
    outputQueue_.clear();
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

    if (!ready_ && !ensureInitialized())
        return;

    if (scratchInput_.size() < static_cast<size_t>(numSamples))
        scratchInput_.resize(static_cast<size_t>(numSamples));
    if (scratchOutput_.size() < static_cast<size_t>(numSamples))
        scratchOutput_.resize(static_cast<size_t>(numSamples));

    const auto* left = buffer.getReadPointer(0);
    const auto* right = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;

    if (right != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
            scratchInput_[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
    }
    else
    {
        std::memcpy(scratchInput_.data(), left, static_cast<size_t>(numSamples) * sizeof(float));
    }

    inputResampler_.push(scratchInput_.data(), static_cast<size_t>(numSamples));
    inputResampler_.drainAvailable(resampledInput_);
    if (!resampledInput_.empty())
        inputQueue_.push(resampledInput_.data(), resampledInput_.size());

    while (inputQueue_.size() >= static_cast<size_t>(frameSize_))
    {
        inputQueue_.pop(frameInput_.data(), static_cast<size_t>(frameSize_));
        api_.processFrame(state_, frameInput_.data(), frameOutput_.data());
        outputQueue_.push(frameOutput_.data(), static_cast<size_t>(frameSize_));
    }

    const auto processedSamplesAvailable = outputQueue_.size();
    if (processedSamplesAvailable > 0)
    {
        resampledOutput_.resize(processedSamplesAvailable);
        const auto popped = outputQueue_.pop(resampledOutput_.data(), processedSamplesAvailable);
        outputResampler_.push(resampledOutput_.data(), popped);
    }

    if (!primed_ && outputResampler_.hasBufferedInput())
        primed_ = true;

    if (!primed_)
    {
        buffer.clear();
        return;
    }

    const auto written = outputResampler_.produce(scratchOutput_.data(), static_cast<size_t>(numSamples));
    if (written < static_cast<size_t>(numSamples))
    {
        std::fill(scratchOutput_.begin() + static_cast<std::ptrdiff_t>(written),
                  scratchOutput_.begin() + numSamples,
                  0.0f);
    }

    for (int channel = 0; channel < numChannels; ++channel)
        buffer.copyFrom(channel, 0, scratchOutput_.data(), numSamples);
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
    if (!ready_ || sampleRate_ <= 0.0)
        return 0;

    const auto hostSamples = (static_cast<double>(frameSize_) * sampleRate_) / targetSampleRate;
    return juce::jmax(0, juce::roundToInt(std::ceil(hostSamples)) + 1);
}

bool DenoiseEngine::DfApi::load(const juce::File& libraryFile)
{
    if (!libraryFile.existsAsFile())
        return false;

    if (!library.open(libraryFile.getFullPathName()))
        return false;

    create = getFunction<create_t>(library, "df_create");
    getFrameLength = getFunction<get_frame_length_t>(library, "df_get_frame_length");
    setAttenLim = getFunction<set_atten_lim_t>(library, "df_set_atten_lim");
    setPostFilterBeta = getFunction<set_post_filter_beta_t>(library, "df_set_post_filter_beta");
    processFrame = getFunction<process_frame_t>(library, "df_process_frame");
    freeState = getFunction<free_t>(library, "df_free");

    if (!create || !getFrameLength || !setAttenLim || !setPostFilterBeta || !processFrame || !freeState)
    {
        unload();
        return false;
    }

    return true;
}

void DenoiseEngine::DfApi::unload()
{
    library.close();
    create = nullptr;
    getFrameLength = nullptr;
    setAttenLim = nullptr;
    setPostFilterBeta = nullptr;
    processFrame = nullptr;
    freeState = nullptr;
}

bool DenoiseEngine::ensureInitialized()
{
    if (ready_)
        return true;

    if (initAttempted_ || !isSampleRateSupported())
        return false;

    initAttempted_ = true;
    runtimeAssets_ = EmbeddedRuntimeAssets::ensureExtracted();
    if (!runtimeAssets_.has_value())
        return false;

    if (!api_.load(runtimeAssets_->dfDll))
        return false;

    const auto modelPath = runtimeAssets_->modelArchive.getFullPathName();
    state_ = api_.create(modelPath.toRawUTF8(), attenLimDb_, nullptr);
    if (state_ == nullptr)
    {
        api_.unload();
        return false;
    }

    frameSize_ = static_cast<int>(api_.getFrameLength(state_));
    if (frameSize_ <= 0)
    {
        shutdown();
        return false;
    }

    frameInput_.assign(static_cast<size_t>(frameSize_), 0.0f);
    frameOutput_.assign(static_cast<size_t>(frameSize_), 0.0f);
    inputResampler_.reset(sampleRate_, targetSampleRate);
    outputResampler_.reset(targetSampleRate, sampleRate_);
    inputQueue_.clear();
    outputQueue_.clear();
    inputQueue_.reserve(static_cast<size_t>(frameSize_) * queueReserveMultiplier);
    outputQueue_.reserve(static_cast<size_t>(frameSize_) * queueReserveMultiplier);
    if (maximumBlockSize_ > 0)
        setMaximumBlockSize(maximumBlockSize_);
    applyParameters(true);
    primed_ = false;
    ready_ = true;
    return true;
}

void DenoiseEngine::shutdown()
{
    if (state_ != nullptr)
    {
        api_.freeState(state_);
        state_ = nullptr;
    }

    api_.unload();
    ready_ = false;
    initAttempted_ = false;
    primed_ = false;
    frameSize_ = 0;
    inputResampler_.clear();
    outputResampler_.clear();
    inputQueue_.clear();
    outputQueue_.clear();
    resampledInput_.clear();
    resampledOutput_.clear();
    frameInput_.clear();
    frameOutput_.clear();
}

void DenoiseEngine::applyParameters(bool force)
{
    if (state_ == nullptr)
        return;

    if (force || std::abs(attenLimDb_ - attenLimApplied_) > 1.0e-3f)
    {
        api_.setAttenLim(state_, attenLimDb_);
        attenLimApplied_ = attenLimDb_;
    }

    if (force || std::abs(postFilterBeta_ - postFilterApplied_) > 1.0e-6f)
    {
        api_.setPostFilterBeta(state_, postFilterBeta_);
        postFilterApplied_ = postFilterBeta_;
    }
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

#pragma once

#include "DeepFilterRuntimeBridge.h"

#include <JuceHeader.h>
#include <cstddef>
#include <vector>

namespace dfvst
{
class DenoiseEngine
{
public:
    DenoiseEngine() = default;
    ~DenoiseEngine();

    void setSampleRate(double sampleRate);
    void setMaximumBlockSize(int maximumBlockSize);
    void prepare(int channelCount);
    void reset();
    void release();

    void updateParameters(float attenLimDb, float postFilterBeta, int reduceMask);
    void process(juce::AudioBuffer<float>& buffer);

    bool isSampleRateSupported() const;
    bool isReady() const;
    int getLatencySamples() const;

private:
    class FloatQueue
    {
    public:
        void clear();
        void reserve(size_t count);
        size_t size() const;
        float get(size_t index) const;
        void discard(size_t count);
        void push(const float* data, size_t count);
        size_t pop(float* destination, size_t count);

    private:
        void compact();

        std::vector<float> data_;
        size_t readPosition_ = 0;
    };

    class SharedRubatoResampler
    {
    public:
        enum class Mode
        {
            fixedIn,
            fixedOut,
        };

        ~SharedRubatoResampler();

        bool reset(Mode mode, double inputSampleRate, double outputSampleRate, size_t chunkSize, size_t channelCount);
        void clear();
        void release();
        void reserve(size_t count);
        void push(const std::vector<const float*>& channelData, size_t count);
        void drainAvailable(std::vector<std::vector<float>>& destination);
        size_t produce(const std::vector<float*>& destination, size_t maxOutputSamples);
        bool hasBufferedInput();
        size_t getOutputDelay() const;

    private:
        void destroyState();
        void ensureChannelCount(size_t channelCount);
        void refreshFrameCounts();
        void processAvailableInput();
        size_t getAvailableOutputSamples() const;
        bool canProcessInput() const;

        DfVstResamplerState* state_ = nullptr;
        Mode mode_ = Mode::fixedIn;
        size_t channelCount_ = 0;
        bool passthrough_ = false;
        size_t inputFramesMax_ = 0;
        size_t inputFramesNext_ = 0;
        size_t outputFramesMax_ = 0;
        size_t outputDelay_ = 0;
        std::vector<FloatQueue> inputQueues_;
        std::vector<FloatQueue> outputQueues_;
        std::vector<float> processInput_;
        std::vector<float> processOutput_;
    };

    struct ChannelState
    {
        FloatQueue inputQueue;
        FloatQueue outputQueue;
        std::vector<float> scratchInput;
        std::vector<float> scratchOutput;
    };

    bool ensureInitialized(int channelCount);
    void shutdown();
    void applyParameters(bool force);
    void ensureChannelStates(int channelCount);
    void resizeChannelBuffers(ChannelState& channelState);

    static constexpr double fallbackTargetSampleRate = 48000.0;
    static constexpr size_t queueReserveMultiplier = 8;
    static constexpr int defaultReduceMask = 0; // ReduceMask::NONE

    DfVstBridgeState* state_ = nullptr;
    double sampleRate_ = 0.0;
    double runtimeSampleRate_ = fallbackTargetSampleRate;
    int maximumBlockSize_ = 0;
    int frameSize_ = 0;
    int channelCount_ = 0;
    bool ready_ = false;
    bool initAttempted_ = false;
    bool primed_ = false;
    float attenLimDb_ = 100.0f;
    float postFilterBeta_ = 0.0f;
    float attenLimApplied_ = 100.0f;
    float postFilterApplied_ = 0.0f;
    int reduceMask_ = defaultReduceMask;
    int reduceMaskApplied_ = -1;
    SharedRubatoResampler inputResampler_;
    SharedRubatoResampler outputResampler_;
    std::vector<ChannelState> channelStates_;
    std::vector<std::vector<float>> resampledInput_;
    std::vector<std::vector<float>> resampledOutput_;
    std::vector<const float*> channelReadPointers_;
    std::vector<float*> channelWritePointers_;
    std::vector<float> frameInput_;
    std::vector<float> frameOutput_;
};
}

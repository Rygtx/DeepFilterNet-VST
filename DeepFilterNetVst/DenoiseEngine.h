#pragma once

#include "DeepFilterRuntimeBridge.h"

#include <JuceHeader.h>
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
    void prepare();
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

    class StreamingLinearResampler
    {
    public:
        void reset(double inputSampleRate, double outputSampleRate);
        void clear();
        void reserve(size_t count);
        void push(const float* data, size_t count);
        void drainAvailable(std::vector<float>& destination);
        size_t produce(float* destination, size_t maxOutputSamples);
        bool hasBufferedInput() const;

    private:
        bool canProduce() const;
        float produceOne();

        FloatQueue inputQueue_;
        double inputSamplesPerOutputSample_ = 1.0;
        double sourcePosition_ = 0.0;
    };

    struct ChannelState
    {
        StreamingLinearResampler inputResampler;
        StreamingLinearResampler outputResampler;
        FloatQueue inputQueue;
        FloatQueue outputQueue;
        std::vector<float> scratchInput;
        std::vector<float> scratchOutput;
        std::vector<float> resampledInput;
        std::vector<float> resampledOutput;
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
    std::vector<ChannelState> channelStates_;
    std::vector<float> frameInput_;
    std::vector<float> frameOutput_;
};
}

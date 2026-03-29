#pragma once

#include "RuntimeAssets.h"

#include <JuceHeader.h>
#include <optional>
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

    void updateParameters(float attenLimDb, float postFilterBeta);
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

    struct DfApi
    {
        using create_t = void* (__cdecl*)(const char* path, float attenLimDb, void* logLevel);
        using get_frame_length_t = size_t(__cdecl*)(void* state);
        using set_atten_lim_t = void(__cdecl*)(void* state, float limDb);
        using set_post_filter_beta_t = void(__cdecl*)(void* state, float beta);
        using process_frame_t = float(__cdecl*)(void* state, const float* input, float* output);
        using free_t = void(__cdecl*)(void* state);

        bool load(const juce::File& libraryFile);
        void unload();

        juce::DynamicLibrary library;
        create_t create = nullptr;
        get_frame_length_t getFrameLength = nullptr;
        set_atten_lim_t setAttenLim = nullptr;
        set_post_filter_beta_t setPostFilterBeta = nullptr;
        process_frame_t processFrame = nullptr;
        free_t freeState = nullptr;
    };

    bool ensureInitialized();
    void shutdown();
    void applyParameters(bool force);

    static constexpr double targetSampleRate = 48000.0;
    static constexpr size_t queueReserveMultiplier = 8;

    DfApi api_;
    void* state_ = nullptr;
    std::optional<RuntimeAssetPaths> runtimeAssets_;
    double sampleRate_ = 0.0;
    int maximumBlockSize_ = 0;
    int frameSize_ = 0;
    bool ready_ = false;
    bool initAttempted_ = false;
    bool primed_ = false;
    float attenLimDb_ = 100.0f;
    float postFilterBeta_ = 0.0f;
    float attenLimApplied_ = 100.0f;
    float postFilterApplied_ = 0.0f;
    StreamingLinearResampler inputResampler_;
    StreamingLinearResampler outputResampler_;
    FloatQueue inputQueue_;
    FloatQueue outputQueue_;
    std::vector<float> scratchInput_;
    std::vector<float> scratchOutput_;
    std::vector<float> resampledInput_;
    std::vector<float> resampledOutput_;
    std::vector<float> frameInput_;
    std::vector<float> frameOutput_;
};
}

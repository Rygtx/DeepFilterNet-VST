#pragma once

#include <cstddef>

extern "C"
{
struct DfVstBridgeState;
struct DfVstResamplerState;

DfVstBridgeState* dfvst_create(std::size_t channels, float attenLimDb, float postFilterBeta, int reduceMask);
void dfvst_free(DfVstBridgeState* state);
std::size_t dfvst_get_frame_length(const DfVstBridgeState* state);
std::size_t dfvst_get_sample_rate(const DfVstBridgeState* state);
std::size_t dfvst_get_channel_count(const DfVstBridgeState* state);
void dfvst_set_atten_lim(DfVstBridgeState* state, float attenLimDb);
void dfvst_set_post_filter_beta(DfVstBridgeState* state, float postFilterBeta);
float dfvst_process_frame(DfVstBridgeState* state, const float* input, float* output);

DfVstResamplerState* dfvst_resampler_create_fixed_in(std::size_t inputSampleRate,
                                                     std::size_t outputSampleRate,
                                                     std::size_t chunkSizeIn,
                                                     std::size_t subChunks,
                                                     std::size_t channels);
DfVstResamplerState* dfvst_resampler_create_fixed_out(std::size_t inputSampleRate,
                                                      std::size_t outputSampleRate,
                                                      std::size_t chunkSizeOut,
                                                      std::size_t subChunks,
                                                      std::size_t channels);
void dfvst_resampler_free(DfVstResamplerState* state);
void dfvst_resampler_reset(DfVstResamplerState* state);
std::size_t dfvst_resampler_get_input_frames_max(const DfVstResamplerState* state);
std::size_t dfvst_resampler_get_input_frames_next(const DfVstResamplerState* state);
std::size_t dfvst_resampler_get_output_frames_max(const DfVstResamplerState* state);
std::size_t dfvst_resampler_get_output_frames_next(const DfVstResamplerState* state);
std::size_t dfvst_resampler_get_output_delay(const DfVstResamplerState* state);
std::size_t dfvst_resampler_process(DfVstResamplerState* state,
                                    const float* input,
                                    std::size_t inputFrames,
                                    float* output,
                                    std::size_t outputCapacityFrames);
}

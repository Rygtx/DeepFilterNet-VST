#pragma once

#include <cstddef>

extern "C"
{
struct DfVstBridgeState;

DfVstBridgeState* dfvst_create(std::size_t channels, float attenLimDb, float postFilterBeta, int reduceMask);
void dfvst_free(DfVstBridgeState* state);
std::size_t dfvst_get_frame_length(const DfVstBridgeState* state);
std::size_t dfvst_get_sample_rate(const DfVstBridgeState* state);
std::size_t dfvst_get_channel_count(const DfVstBridgeState* state);
void dfvst_set_atten_lim(DfVstBridgeState* state, float attenLimDb);
void dfvst_set_post_filter_beta(DfVstBridgeState* state, float postFilterBeta);
float dfvst_process_frame(DfVstBridgeState* state, const float* input, float* output);
}

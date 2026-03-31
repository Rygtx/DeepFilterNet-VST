use std::boxed::Box;
use std::ffi::c_float;

use deep_filter::tract::{DfParams, DfTract, ReduceMask, RuntimeParams};
use ndarray::prelude::*;
use rubato::{FftFixedIn, FftFixedOut, Resampler};

pub struct BridgeState {
    model: DfTract,
}

enum RubatoResamplerKind {
    FixedIn(FftFixedIn<f32>),
    FixedOut(FftFixedOut<f32>),
}

pub struct ResamplerState {
    channels: usize,
    kind: RubatoResamplerKind,
    input_buffer: Vec<Vec<f32>>,
    output_buffer: Vec<Vec<f32>>,
}

impl BridgeState {
    fn new(channels: usize, atten_lim_db: f32, post_filter_beta: f32, reduce_mask: i32) -> Option<Self> {
        let mut runtime_params = RuntimeParams::default_with_ch(channels)
            .with_atten_lim(atten_lim_db)
            .with_thresholds(-15.0, 35.0, 35.0)
            .with_post_filter(post_filter_beta);

        if let Ok(mask) = reduce_mask.try_into() {
            runtime_params = runtime_params.with_mask_reduce(mask);
        }

        let model = DfTract::new(DfParams::default(), &runtime_params).ok()?;
        Some(Self { model })
    }
}

impl ResamplerState {
    fn new_fixed_in(
        input_sample_rate: usize,
        output_sample_rate: usize,
        chunk_size_in: usize,
        sub_chunks: usize,
        channels: usize,
    ) -> Option<Self> {
        let resampler =
            FftFixedIn::<f32>::new(input_sample_rate, output_sample_rate, chunk_size_in, sub_chunks, channels)
                .ok()?;
        let input_buffer = resampler.input_buffer_allocate(true);
        let output_buffer = resampler.output_buffer_allocate(true);

        Some(Self {
            channels,
            kind: RubatoResamplerKind::FixedIn(resampler),
            input_buffer,
            output_buffer,
        })
    }

    fn new_fixed_out(
        input_sample_rate: usize,
        output_sample_rate: usize,
        chunk_size_out: usize,
        sub_chunks: usize,
        channels: usize,
    ) -> Option<Self> {
        let resampler =
            FftFixedOut::<f32>::new(input_sample_rate, output_sample_rate, chunk_size_out, sub_chunks, channels)
                .ok()?;
        let input_buffer = resampler.input_buffer_allocate(true);
        let output_buffer = resampler.output_buffer_allocate(true);

        Some(Self {
            channels,
            kind: RubatoResamplerKind::FixedOut(resampler),
            input_buffer,
            output_buffer,
        })
    }

    fn input_frames_max(&self) -> usize {
        match &self.kind {
            RubatoResamplerKind::FixedIn(resampler) => resampler.input_frames_max(),
            RubatoResamplerKind::FixedOut(resampler) => resampler.input_frames_max(),
        }
    }

    fn input_frames_next(&self) -> usize {
        match &self.kind {
            RubatoResamplerKind::FixedIn(resampler) => resampler.input_frames_next(),
            RubatoResamplerKind::FixedOut(resampler) => resampler.input_frames_next(),
        }
    }

    fn output_frames_max(&self) -> usize {
        match &self.kind {
            RubatoResamplerKind::FixedIn(resampler) => resampler.output_frames_max(),
            RubatoResamplerKind::FixedOut(resampler) => resampler.output_frames_max(),
        }
    }

    fn output_frames_next(&self) -> usize {
        match &self.kind {
            RubatoResamplerKind::FixedIn(resampler) => resampler.output_frames_next(),
            RubatoResamplerKind::FixedOut(resampler) => resampler.output_frames_next(),
        }
    }

    fn output_delay(&self) -> usize {
        match &self.kind {
            RubatoResamplerKind::FixedIn(resampler) => resampler.output_delay(),
            RubatoResamplerKind::FixedOut(resampler) => resampler.output_delay(),
        }
    }

    fn reset(&mut self) {
        match &mut self.kind {
            RubatoResamplerKind::FixedIn(resampler) => resampler.reset(),
            RubatoResamplerKind::FixedOut(resampler) => resampler.reset(),
        }
    }

    fn process(&mut self, input: &[f32], input_frames: usize, output: &mut [f32], output_capacity_frames: usize) -> Option<usize> {
        if self.channels == 0 {
            return None;
        }

        if input.len() < self.channels * input_frames || output.len() < self.channels * output_capacity_frames {
            return None;
        }

        if input_frames < self.input_frames_next() || output_capacity_frames < self.output_frames_next() {
            return None;
        }

        for channel in 0..self.channels {
            let input_start = channel * input_frames;
            let input_end = input_start + input_frames;
            self.input_buffer[channel].resize(input_frames, 0.0);
            self.input_buffer[channel].copy_from_slice(&input[input_start..input_end]);
            self.output_buffer[channel].resize(output_capacity_frames, 0.0);
        }

        let (_, produced) = match &mut self.kind {
            RubatoResamplerKind::FixedIn(resampler) => {
                resampler
                    .process_into_buffer(&self.input_buffer, &mut self.output_buffer, None)
                    .ok()?
            }
            RubatoResamplerKind::FixedOut(resampler) => {
                resampler
                    .process_into_buffer(&self.input_buffer, &mut self.output_buffer, None)
                    .ok()?
            }
        };

        for channel in 0..self.channels {
            let output_start = channel * output_capacity_frames;
            let output_end = output_start + produced;
            output[output_start..output_end].copy_from_slice(&self.output_buffer[channel][..produced]);
        }

        Some(produced)
    }
}

fn mask_from_i32(value: i32) -> Result<ReduceMask, ()> {
    value.try_into()
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_create(
    channels: usize,
    atten_lim_db: f32,
    post_filter_beta: f32,
    reduce_mask: i32,
) -> *mut BridgeState {
    if channels == 0 {
        return std::ptr::null_mut();
    }

    if mask_from_i32(reduce_mask).is_err() {
        return std::ptr::null_mut();
    }

    match BridgeState::new(channels, atten_lim_db, post_filter_beta, reduce_mask) {
        Some(state) => Box::into_raw(Box::new(state)),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_free(state: *mut BridgeState) {
    if !state.is_null() {
        let _ = Box::from_raw(state);
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_get_frame_length(state: *const BridgeState) -> usize {
    state.as_ref().map(|s| s.model.hop_size).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_get_sample_rate(state: *const BridgeState) -> usize {
    state.as_ref().map(|s| s.model.sr).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_get_channel_count(state: *const BridgeState) -> usize {
    state.as_ref().map(|s| s.model.ch).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_set_atten_lim(state: *mut BridgeState, atten_lim_db: f32) {
    if let Some(state) = state.as_mut() {
        state.model.set_atten_lim(atten_lim_db);
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_set_post_filter_beta(state: *mut BridgeState, post_filter_beta: f32) {
    if let Some(state) = state.as_mut() {
        state.model.set_pf_beta(post_filter_beta);
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_process_frame(
    state: *mut BridgeState,
    input: *const c_float,
    output: *mut c_float,
) -> c_float {
    let Some(state) = state.as_mut() else {
        return -15.0;
    };

    if input.is_null() || output.is_null() {
        return -15.0;
    }

    let channel_count = state.model.ch;
    let frame_length = state.model.hop_size;
    let total_len = channel_count * frame_length;

    let input = std::slice::from_raw_parts(input, total_len);
    let output = std::slice::from_raw_parts_mut(output, total_len);
    let input = ArrayView2::from_shape((channel_count, frame_length), input)
        .expect("invalid input frame shape");
    let output = ArrayViewMut2::from_shape((channel_count, frame_length), output)
        .expect("invalid output frame shape");

    state.model.process(input, output).unwrap_or(-15.0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_create_fixed_in(
    input_sample_rate: usize,
    output_sample_rate: usize,
    chunk_size_in: usize,
    sub_chunks: usize,
    channels: usize,
) -> *mut ResamplerState {
    if input_sample_rate == 0 || output_sample_rate == 0 || chunk_size_in == 0 || sub_chunks == 0 || channels == 0 {
        return std::ptr::null_mut();
    }

    match ResamplerState::new_fixed_in(input_sample_rate, output_sample_rate, chunk_size_in, sub_chunks, channels) {
        Some(state) => Box::into_raw(Box::new(state)),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_create_fixed_out(
    input_sample_rate: usize,
    output_sample_rate: usize,
    chunk_size_out: usize,
    sub_chunks: usize,
    channels: usize,
) -> *mut ResamplerState {
    if input_sample_rate == 0 || output_sample_rate == 0 || chunk_size_out == 0 || sub_chunks == 0 || channels == 0 {
        return std::ptr::null_mut();
    }

    match ResamplerState::new_fixed_out(input_sample_rate, output_sample_rate, chunk_size_out, sub_chunks, channels) {
        Some(state) => Box::into_raw(Box::new(state)),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_free(state: *mut ResamplerState) {
    if !state.is_null() {
        let _ = Box::from_raw(state);
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_reset(state: *mut ResamplerState) {
    if let Some(state) = state.as_mut() {
        state.reset();
    }
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_get_input_frames_max(state: *const ResamplerState) -> usize {
    state.as_ref().map(|s| s.input_frames_max()).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_get_input_frames_next(state: *const ResamplerState) -> usize {
    state.as_ref().map(|s| s.input_frames_next()).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_get_output_frames_max(state: *const ResamplerState) -> usize {
    state.as_ref().map(|s| s.output_frames_max()).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_get_output_frames_next(state: *const ResamplerState) -> usize {
    state.as_ref().map(|s| s.output_frames_next()).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_get_output_delay(state: *const ResamplerState) -> usize {
    state.as_ref().map(|s| s.output_delay()).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn dfvst_resampler_process(
    state: *mut ResamplerState,
    input: *const c_float,
    input_frames: usize,
    output: *mut c_float,
    output_capacity_frames: usize,
) -> usize {
    let Some(state) = state.as_mut() else {
        return 0;
    };

    if input.is_null() || output.is_null() || input_frames == 0 || output_capacity_frames == 0 {
        return 0;
    }

    let input = std::slice::from_raw_parts(input, state.channels * input_frames);
    let output = std::slice::from_raw_parts_mut(output, state.channels * output_capacity_frames);

    state.process(input, input_frames, output, output_capacity_frames).unwrap_or(0)
}

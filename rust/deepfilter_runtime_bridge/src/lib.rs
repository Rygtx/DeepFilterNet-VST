use std::boxed::Box;
use std::ffi::c_float;

use deep_filter::tract::{DfParams, DfTract, ReduceMask, RuntimeParams};
use ndarray::prelude::*;

pub struct BridgeState {
    model: DfTract,
}

impl BridgeState {
    fn new(channels: usize, atten_lim_db: f32, post_filter_beta: f32, reduce_mask: i32) -> Option<Self> {
        let mut runtime_params = RuntimeParams::default_with_ch(channels).with_atten_lim(atten_lim_db);

        if post_filter_beta > 0.0 {
            runtime_params = runtime_params.with_post_filter(post_filter_beta);
        }

        if let Ok(mask) = reduce_mask.try_into() {
            runtime_params = runtime_params.with_mask_reduce(mask);
        }

        let model = DfTract::new(DfParams::default(), &runtime_params).ok()?;
        Some(Self { model })
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

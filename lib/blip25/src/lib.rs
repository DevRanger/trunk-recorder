// Narrow C ABI for TR. Codec state and allocation ownership stay in Rust.
use blip25_vocoder::vocoder::{FrameStatus, Rate, Vocoder, FRAME_SAMPLES};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub struct Decoder {
    vocoder: Vocoder,
    count: usize,
}

#[no_mangle]
pub extern "C" fn tr_blip25_create(phase: u32) -> *mut Decoder {
    let rate = match phase {
        1 => Rate::FullRate4400x4400,
        2 => Rate::HalfRate2450x2450,
        _ => return std::ptr::null_mut(),
    };
    catch_unwind(|| {
        Box::into_raw(Box::new(Decoder {
            vocoder: Vocoder::new(rate),
            count: rate.info_vector_count(),
        }))
    })
    .unwrap_or(std::ptr::null_mut())
}

/// The handle must be live and exclusively owned by this caller.
#[no_mangle]
pub unsafe extern "C" fn tr_blip25_reset(handle: *mut Decoder) -> i32 {
    if handle.is_null() {
        return -1;
    }
    catch_unwind(AssertUnwindSafe(|| (*handle).vocoder.reset()))
        .map(|_| 0)
        .unwrap_or(-1)
}

/// All non-null buffers must be valid, disjoint, and of their stated length.
/// No Rust-owned memory or unwinding crosses the C ABI.
#[no_mangle]
pub unsafe extern "C" fn tr_blip25_decode(
    handle: *mut Decoder,
    info: *const u16,
    count: usize,
    corrected_errors: u32,
    lost: i32,
    mute: i32,
    output: *mut i16,
    output_count: usize,
) -> i32 {
    if handle.is_null() || info.is_null() || output.is_null() || output_count != FRAME_SAMPLES {
        return -1;
    }
    let decoder = &mut *handle;
    if count != decoder.count {
        return -1;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let info = std::slice::from_raw_parts(info, count);
        let pcm: &mut [i16; FRAME_SAMPLES] = std::slice::from_raw_parts_mut(output, output_count)
            .try_into()
            .expect("validated output length");
        let status = FrameStatus::new(corrected_errors, mute != 0).with_lost(lost != 0);
        decoder.vocoder.decode_info_into(info, status, pcm).map(|_| 0).unwrap_or(-1)
    }))
    .unwrap_or(-1)
}

/// Pass only a handle returned by create, exactly once. Null is accepted.
#[no_mangle]
pub unsafe extern "C" fn tr_blip25_destroy(handle: *mut Decoder) {
    if !handle.is_null() {
        drop(Box::from_raw(handle));
    }
}

//! Transaction-ordering tests for the GPU-frame path.
//!
//! These drive both callbacks directly, so they need no decoder, no bitstream
//! and no GPU: they pin the state machine only ("does this decode call end up
//! with the last decoded frame, and exactly one delivery"). The per-iteration
//! reset inside `do_decode()` still requires a real VideoToolbox decode and is
//! verified on hardware, not here.

use super::*;
use crate::ffmpeg_ram::pixelbuffer::test_support::{create_buffer, release_buffer};

const W: i32 = 16;
const H: i32 = 16;

/// An output that asked for GPU frames; delivery is opt-in per call, so a bare
/// `DecodeOutput::new()` would reject everything.
fn output_wanting_gpu(wanted: bool) -> DecodeOutput {
    let mut output = DecodeOutput::new();
    output.want_gpu = wanted;
    output
}

/// Push one software (NV12) frame through the real callback.
fn software_frame(output: &mut DecodeOutput) {
    let mut y = vec![0u8; (W * H) as usize];
    let mut uv = vec![0u8; (W * H / 2) as usize];
    let mut linesizes = [0 as c_int; AV_NUM_DATA_POINTERS as _];
    linesizes[0] = W;
    linesizes[1] = W;
    let mut datas = [std::ptr::null_mut::<u8>(); AV_NUM_DATA_POINTERS as _];
    datas[0] = y.as_mut_ptr();
    datas[1] = uv.as_mut_ptr();
    unsafe {
        Decoder::callback(
            output as *mut _ as *const c_void,
            W,
            H,
            AVPixelFormat::AV_PIX_FMT_NV12 as c_int,
            linesizes.as_mut_ptr(),
            datas.as_mut_ptr(),
            1,
        )
    };
}

/// Deliver one GPU frame through the real callback; returns its verdict.
fn direct_frame(output: &mut DecodeOutput, pixfmt: u32, raw: *mut c_void) -> c_int {
    let frame = FfiPixelBufferFrame {
        pixel_buffer: raw,
        width: W,
        height: H,
        pixfmt,
    };
    unsafe { Decoder::pixelbuffer_callback(output as *mut _ as *mut c_void, &frame) }
}

#[test]
fn want_gpu_false_rejects_gpu_frames() {
    // Opt-in: a caller that does not ask for GPU frames must keep getting
    // software frames, so the surface is refused even when it could be
    // delivered. This is the regression that a Cargo.lock bump would otherwise
    // push into every consumer.
    let Some(raw) = create_buffer(crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F) else {
        eprintln!("skip: CVPixelBufferCreate failed in this environment");
        return;
    };
    let mut output = DecodeOutput::new();
    assert!(
        !output.want_gpu,
        "a decoder without a caller request must not want GPU frames"
    );
    assert_eq!(
        direct_frame(
            &mut output,
            crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F,
            raw
        ),
        PIXELBUFFER_REJECTED
    );
    assert!(
        output.pixelbuffer.is_none(),
        "a rejected frame must retain nothing"
    );

    // ... and it accepts once the call asks for it.
    output.want_gpu = true;
    assert_eq!(
        direct_frame(
            &mut output,
            crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F,
            raw
        ),
        PIXELBUFFER_ACCEPTED
    );
    assert!(output.pixelbuffer.is_some());
    release_buffer(raw);
}

#[test]
fn software_then_direct_keeps_the_gpu_frame() {
    let Some(raw) = create_buffer(crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F) else {
        eprintln!("skip: CVPixelBufferCreate failed in this environment");
        return;
    };
    let mut output = output_wanting_gpu(true);
    software_frame(&mut output);
    assert_eq!(output.frames.len(), 1);
    assert!(output.pixelbuffer.is_none());

    assert_eq!(
        direct_frame(
            &mut output,
            crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F,
            raw
        ),
        PIXELBUFFER_ACCEPTED
    );
    // The GPU frame arrived last, so it is the output of this call.
    assert!(output.pixelbuffer.is_some());
    assert_eq!(
        output.frames.len(),
        1,
        "software frames are not cleared by a GPU frame"
    );

    output.clear();
    assert!(output.pixelbuffer.is_none());
    assert!(output.frames.is_empty());
    release_buffer(raw);
}

#[test]
fn direct_then_software_clears_the_pending_gpu_frame() {
    let Some(raw) = create_buffer(crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420V) else {
        eprintln!("skip: CVPixelBufferCreate failed in this environment");
        return;
    };
    let mut output = output_wanting_gpu(true);
    assert_eq!(
        direct_frame(
            &mut output,
            crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420V,
            raw
        ),
        PIXELBUFFER_ACCEPTED
    );
    assert!(output.pixelbuffer.is_some());

    software_frame(&mut output);
    // A software frame wins in the same transaction: no stale GPU frame may
    // survive, otherwise the renderer would show the previous one.
    assert!(output.pixelbuffer.is_none());
    assert_eq!(output.frames.len(), 1);
    release_buffer(raw);
}

#[test]
fn unsupported_format_is_rejected() {
    // 32BGRA: a real buffer, but not directly deliverable.
    let Some(raw) = create_buffer(0x4247_5241) else {
        eprintln!("skip: CVPixelBufferCreate failed in this environment");
        return;
    };
    let mut output = output_wanting_gpu(true);
    assert_eq!(
        direct_frame(&mut output, 0x4247_5241, raw),
        PIXELBUFFER_REJECTED
    );
    assert!(
        output.pixelbuffer.is_none(),
        "a rejected frame must retain nothing"
    );
    release_buffer(raw);
}

#[test]
fn unknown_software_format_is_dropped() {
    // `DecodeFrame.pixfmt` is a two-variant enum, so an unknown format has to be
    // rejected before a frame is built: P010 (123) arrives here whenever a 10-bit
    // stream takes the software path.
    let mut y = vec![0u8; (W * H) as usize];
    let mut linesizes = [0 as c_int; AV_NUM_DATA_POINTERS as _];
    linesizes[0] = W;
    let mut datas = [std::ptr::null_mut::<u8>(); AV_NUM_DATA_POINTERS as _];
    datas[0] = y.as_mut_ptr();
    let mut output = DecodeOutput::new();
    unsafe {
        Decoder::callback(
            &mut output as *mut _ as *const c_void,
            W,
            H,
            123,
            linesizes.as_mut_ptr(),
            datas.as_mut_ptr(),
            1,
        )
    };
    assert!(
        output.frames.is_empty(),
        "an unknown format must not produce a frame"
    );
}

#[test]
fn null_descriptor_is_rejected() {
    let mut output = output_wanting_gpu(true);
    assert_eq!(
        direct_frame(
            &mut output,
            crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F,
            std::ptr::null_mut()
        ),
        PIXELBUFFER_REJECTED
    );
    assert!(output.pixelbuffer.is_none());

    // A null obj must be rejected as well, not dereferenced.
    let frame = FfiPixelBufferFrame {
        pixel_buffer: std::ptr::null_mut(),
        width: 0,
        height: 0,
        pixfmt: 0,
    };
    assert_eq!(
        unsafe { Decoder::pixelbuffer_callback(std::ptr::null_mut(), &frame) },
        PIXELBUFFER_REJECTED
    );
}

#[test]
fn clear_drops_the_pending_frame_of_the_previous_call() {
    let Some(raw) = create_buffer(crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F) else {
        eprintln!("skip: CVPixelBufferCreate failed in this environment");
        return;
    };
    let mut output = output_wanting_gpu(true);
    assert_eq!(
        direct_frame(
            &mut output,
            crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F,
            raw
        ),
        PIXELBUFFER_ACCEPTED
    );
    assert!(output.pixelbuffer.is_some());
    output.clear();
    // The next call must not answer with this frame.
    assert!(output.pixelbuffer.is_none());
    release_buffer(raw);
}

#[test]
fn take_moves_the_gpu_frame_out_of_the_decoder() {
    let Some(raw) = create_buffer(crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F) else {
        eprintln!("skip: CVPixelBufferCreate failed in this environment");
        return;
    };
    let mut output = output_wanting_gpu(true);
    software_frame(&mut output);
    direct_frame(
        &mut output,
        crate::ffmpeg_ram::pixelbuffer::PIXEL_FORMAT_420F,
        raw,
    );

    let decoded = output.take();
    assert_eq!(decoded.frames.len(), 1);
    assert!(decoded.pixelbuffer.is_some());
    // Taken means taken: the buffer no longer belongs to the decoder, so the
    // next call's clear() has nothing of this call's to drop.
    assert!(output.pixelbuffer.is_none());
    release_buffer(raw);
}

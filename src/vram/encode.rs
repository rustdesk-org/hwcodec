use crate::{
    common::Driver::*,
    ffmpeg::init_av_log,
    vram::{ffmpeg, DynamicContext, EncodeContext, FeatureContext},
};
use log::trace;
use std::{
    fmt::Display, os::raw::{c_int, c_void}, slice::from_raw_parts
};

pub struct Encoder {
    codec: *mut c_void,
    frames: *mut Vec<EncodeFrame>,
    pub ctx: EncodeContext,
}

unsafe impl Send for Encoder {}
unsafe impl Sync for Encoder {}

impl Encoder {
    pub fn new(ctx: EncodeContext) -> Result<Self, ()> {
        init_av_log();
        if ctx.d.width % 2 == 1 || ctx.d.height % 2 == 1 {
            return Err(());
        }
        if ctx.f.driver != FFMPEG {
            return Err(());
        }
        unsafe {
            let codec = ffmpeg::ffmpeg_vram_new_encoder(
                ctx.d.device.unwrap_or(std::ptr::null_mut()),
                ctx.f.luid,
                ctx.f.data_format as i32,
                ctx.d.width,
                ctx.d.height,
                ctx.d.kbitrate,
                ctx.d.framerate,
                ctx.d.gop,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Self {
                codec,
                frames: Box::into_raw(Box::new(Vec::<EncodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn encode(&mut self, tex: *mut c_void, ms: i64) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let result = ffmpeg::ffmpeg_vram_encode(
                self.codec,
                tex,
                Some(Self::callback),
                self.frames as *mut _ as *mut c_void,
                ms,
            );
            if result != 0 {
                Err(result)
            } else {
                Ok(&mut *self.frames)
            }
        }
    }

    /// Re-encode the last successful input with the caller-supplied timestamp `ms`.
    ///
    /// Reuses the encoder-owned hardware frame without another capture-texture copy or
    /// conversion. The original capture texture may be released after `encode` returns.
    /// `ms` is the new input PTS; it is not inherited from the original capture.
    ///
    /// A new encoder cannot repeat until a normal `encode` succeeds. A failed normal
    /// encode invalidates the cached input, whereas a repeat failure leaves it available
    /// for another attempt unless the pre-submit check detects device loss, which also
    /// invalidates it. Producing no output is reported as an error, even when FFmpeg
    /// accepted the input; retrying does not guarantee recovery from a device failure.
    ///
    /// Changing bitrate preserves the cached input. Resolution or codec changes require
    /// a new encoder and therefore a new successful normal encode. A capture gap that
    /// does not call `encode` leaves the cached input available.
    ///
    /// Serialize calls on the same encoder, including bitrate changes and destruction.
    /// Safe Rust enforces this through mutable borrowing; direct C callers must enforce
    /// it themselves. Returned packets borrow internal storage and are cleared by the
    /// next normal or repeat encode; copy packets that need to outlive that call.
    pub fn encode_repeat(&mut self, ms: i64) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let result = ffmpeg::ffmpeg_vram_encode_repeat(
                self.codec,
                Some(Self::callback),
                self.frames as *mut _ as *mut c_void,
                ms,
            );
            if result != 0 {
                Err(result)
            } else {
                Ok(&mut *self.frames)
            }
        }
    }

    extern "C" fn callback(data: *const u8, size: c_int, key: i32, obj: *const c_void, pts: i64) {
        unsafe {
            let frames = &mut *(obj as *mut Vec<EncodeFrame>);
            frames.push(EncodeFrame {
                data: from_raw_parts(data, size as usize).to_vec(),
                pts,
                key,
            });
        }
    }

    pub fn set_bitrate(&mut self, kbs: i32) -> Result<(), i32> {
        unsafe {
            match ffmpeg::ffmpeg_vram_set_bitrate(self.codec, kbs) {
                0 => Ok(()),
                err => Err(err),
            }
        }
    }

    pub fn set_framerate(&mut self, framerate: i32) -> Result<(), i32> {
        unsafe {
            match ffmpeg::ffmpeg_vram_set_framerate(self.codec, framerate) {
                0 => Ok(()),
                err => Err(err),
            }
        }
    }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            ffmpeg::ffmpeg_vram_destroy_encoder(self.codec);
            self.codec = std::ptr::null_mut();
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}

pub struct EncodeFrame {
    pub data: Vec<u8>,
    pub pts: i64,
    pub key: i32,
}

impl Display for EncodeFrame {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "encode len:{}, key:{}", self.data.len(), self.key)
    }
}

pub fn available(d: DynamicContext) -> Vec<FeatureContext> {
    use log::debug;

    let mut natives: Vec<_> = vec![];
    natives.append(
        &mut ffmpeg::possible_support_encoders()
            .drain(..)
            .map(|n| (FFMPEG, n))
            .collect(),
    );
    let inputs: Vec<EncodeContext> = natives
        .drain(..)
        .map(|(driver, n)| EncodeContext {
            f: FeatureContext {
                driver: driver.clone(),
                vendor: driver, // Initially set vendor same as driver, will be updated by test results
                data_format: n.format,
                luid: 0,
            },
            d,
        })
        .collect();

    let mut outputs = Vec::<EncodeContext>::new();
    let mut exclude_luid_formats = Vec::<(i64, i32)>::new();

    for input in inputs {
        debug!(
            "Testing vram encoder: driver={:?}, format={:?}",
            input.f.driver, input.f.data_format
        );

        let mut luids: Vec<i64> = vec![0; crate::vram::MAX_ADATERS];
        let mut vendors: Vec<i32> = vec![0; crate::vram::MAX_ADATERS];
        let mut desc_count: i32 = 0;

        let (excluded_luids, exclude_formats): (Vec<i64>, Vec<i32>) = exclude_luid_formats
            .iter()
            .map(|(luid, format)| (*luid, *format))
            .unzip();

        let result = unsafe {
            ffmpeg::ffmpeg_vram_test_encode(
                luids.as_mut_ptr(),
                vendors.as_mut_ptr(),
                luids.len() as _,
                &mut desc_count,
                input.f.data_format as i32,
                input.d.width,
                input.d.height,
                input.d.kbitrate,
                input.d.framerate,
                input.d.gop,
                excluded_luids.as_ptr(),
                exclude_formats.as_ptr(),
                exclude_luid_formats.len() as i32,
            )
        };

        if result == 0 {
            if desc_count as usize <= luids.len() {
                debug!(
                    "vram encoder test passed: driver={:?}, adapters={}",
                    input.f.driver, desc_count
                );
                for i in 0..desc_count as usize {
                    let mut input = input.clone();
                    input.f.luid = luids[i];
                    input.f.vendor = match vendors[i] {
                        0 => NV,
                        1 => AMF,
                        2 => MFX,
                        _ => {
                            log::error!(
                                "Unexpected vendor value encountered: {}. Skipping.",
                                vendors[i]
                            );
                            continue;
                        },
                    };
                    exclude_luid_formats.push((luids[i], input.f.data_format as i32));
                    outputs.push(input);
                }
            }
        } else {
            debug!(
                "vram encoder test failed: driver={:?}, error={}",
                input.f.driver, result
            );
        }
    }

    let result: Vec<_> = outputs.drain(..).map(|e| e.f).collect();
    result
}

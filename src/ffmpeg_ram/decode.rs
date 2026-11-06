#[cfg(any(target_os = "windows", target_os = "linux", target_os = "macos"))]
use super::Priority;
use crate::common::TEST_TIMEOUT_MS;
use crate::ffmpeg::{init_av_log, AVHWDeviceType::*};

#[cfg(target_os = "macos")]
use crate::ffmpeg_ram::{
    ffmpeg_ram_set_pixelbuffer_callback,
    pixelbuffer::{
        is_direct_deliverable, PixelBufferFrame, PIXELBUFFER_ACCEPTED, PIXELBUFFER_REJECTED,
    },
    FfiPixelBufferFrame,
};
use crate::{
    common::DataFormat::*,
    ffmpeg::{AVHWDeviceType, AVPixelFormat},
    ffmpeg_ram::{
        ffmpeg_ram_decode, ffmpeg_ram_free_decoder, ffmpeg_ram_new_decoder, CodecInfo,
        AV_NUM_DATA_POINTERS,
    },
};
use log::error;
use std::{
    ffi::{c_void, CString},
    os::raw::c_int,
    slice::from_raw_parts,
    time::Instant,
    vec,
};

#[derive(Debug, Clone)]
pub struct DecodeContext {
    pub name: String,
    pub device_type: AVHWDeviceType,
    pub thread_count: i32,
}

pub struct DecodeFrame {
    pub pixfmt: AVPixelFormat,
    pub width: i32,
    pub height: i32,
    pub data: Vec<Vec<u8>>,
    pub linesize: Vec<i32>,
    pub key: bool,
}

impl std::fmt::Display for DecodeFrame {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut s = String::from("data:");
        for data in self.data.iter() {
            s.push_str(format!("{} ", data.len()).as_str());
        }
        s.push_str(", linesize:");
        for linesize in self.linesize.iter() {
            s.push_str(format!("{} ", linesize).as_str());
        }

        write!(
            f,
            "fixfmt:{}, width:{}, height:{},key:{}, {}",
            self.pixfmt as i32, self.width, self.height, self.key, s,
        )
    }
}

/// What one `decode()` call produced.
///
/// Both fields belong to that call. Nothing reads the decoder afterwards, so a
/// frame from a previous call is not expressible: the software frames borrow
/// the decoder (and therefore cannot outlive the next `decode()`), while the
/// GPU frame is taken out of it and can be handed to the renderer.
pub struct Decoded<'a> {
    /// The software frames this call produced.
    pub frames: &'a [DecodeFrame],
    /// The GPU frame this call delivered straight to us, if the caller asked
    /// for it and the decoder accepted it.
    ///
    /// `None` means this call produced no direct frame: the caller passed
    /// `want_gpu = false`, the format is not directly deliverable, or a
    /// software frame won in the same transaction.
    #[cfg(target_os = "macos")]
    pub pixelbuffer: Option<PixelBufferFrame>,
}

/// Per-decode output state. Also the `obj` handed to both the software
/// callback and the pixel-buffer callback, so they share one transaction view:
/// a software frame clears the pending GPU frame of the same call, which makes
/// "last decoded frame wins" hold across mixed frames.
pub(crate) struct DecodeOutput {
    frames: Vec<DecodeFrame>,
    #[cfg(target_os = "macos")]
    pixelbuffer: Option<PixelBufferFrame>,
    /// Whether the running call asked for the GPU frame.
    ///
    /// Written by `decode()` for the duration of one call and read by the
    /// pixel-buffer callback. `false` makes the callback reject every frame:
    /// delivery is opt-in, because accepting one makes the decoder skip the
    /// software transfer for that frame.
    #[cfg(target_os = "macos")]
    want_gpu: bool,
}

impl DecodeOutput {
    fn new() -> Self {
        Self {
            frames: vec![],
            #[cfg(target_os = "macos")]
            pixelbuffer: None,
            #[cfg(target_os = "macos")]
            want_gpu: false,
        }
    }

    fn clear(&mut self) {
        self.frames.clear();
        // Transaction boundary: never answer with a frame from a previous call.
        #[cfg(target_os = "macos")]
        {
            self.pixelbuffer = None;
        }
    }

    /// Hand this call's result to the caller: the software frames are borrowed
    /// back from the buffer, the GPU frame is taken out of it.
    fn take(&mut self) -> Decoded<'_> {
        #[cfg(target_os = "macos")]
        let pixelbuffer = self.pixelbuffer.take();
        Decoded {
            frames: &self.frames,
            #[cfg(target_os = "macos")]
            pixelbuffer,
        }
    }
}

pub struct Decoder {
    codec: *mut c_void,
    output: *mut DecodeOutput,
    pub ctx: DecodeContext,
}

unsafe impl Send for Decoder {}
unsafe impl Sync for Decoder {}

impl Decoder {
    pub fn new(ctx: DecodeContext) -> Result<Self, ()> {
        init_av_log();
        unsafe {
            let codec = ffmpeg_ram_new_decoder(
                CString::new(ctx.name.as_str()).map_err(|_| ())?.as_ptr(),
                ctx.device_type as _,
                ctx.thread_count,
                Some(Decoder::callback),
            );

            if codec.is_null() {
                return Err(());
            }

            #[cfg(target_os = "macos")]
            ffmpeg_ram_set_pixelbuffer_callback(codec, Some(Decoder::pixelbuffer_callback));

            Ok(Decoder {
                codec,
                output: Box::into_raw(Box::new(DecodeOutput::new())),
                ctx,
            })
        }
    }

    /// Decode one packet.
    ///
    /// `want_gpu` answers "can the caller present a directly delivered GPU
    /// frame?". The C++ decoder only skips its software transfer when the
    /// pixel-buffer callback accepts a frame *during* this call, so the answer
    /// has to arrive with the packet: a frame accepted here and dropped further
    /// up would leave the renderer with neither a GPU frame nor a software one.
    /// `false` keeps the software path, and platforms without pixel buffers
    /// ignore the argument.
    fn decode_inner(&mut self, packet: &[u8], want_gpu: bool) -> Result<Decoded<'_>, i32> {
        #[cfg(not(target_os = "macos"))]
        let _ = want_gpu;
        // No `&mut DecodeOutput` may be alive while `ffmpeg_ram_decode` runs: the C
        // side calls `callback` synchronously, and that callback reaches this same
        // allocation through the raw pointer and takes its own reference to it.
        // Two live `&mut`s to one object violate the aliasing rules even when one
        // thread touches both, so the clear and the read each borrow through the
        // raw pointer for the length of a statement, and the FFI call runs with no
        // borrow alive at all.
        let output = self.output;
        unsafe {
            (*output).clear();
            #[cfg(target_os = "macos")]
            {
                (*output).want_gpu = want_gpu;
            }
            let ret = ffmpeg_ram_decode(
                self.codec,
                packet.as_ptr(),
                packet.len() as c_int,
                output as *const c_void,
            );

            if ret < 0 {
                // A failed call can still have had a GPU frame accepted: the C++ side
                // calls `pixelbuffer_callback` and then fails on later error paths such
                // as HEVC's "could not find ref with POC". That frame is pinned in
                // `output` and would otherwise keep its IOSurface alive until the next
                // decode or the decoder's drop, so clear it here as well.
                (*output).clear();
                Err(ret)
            } else {
                Ok((&mut *output).take())
            }
        }
    }

    /// Decode one packet and return only the software frames.
    ///
    /// This is the signature this crate has always exposed: existing callers
    /// keep working unchanged, and non-macOS targets never see the GPU-frame
    /// argument.
    pub fn decode(&mut self, packet: &[u8]) -> Result<&[DecodeFrame], i32> {
        self.decode_inner(packet, false).map(|d| d.frames)
    }

    /// macOS only: also ask for the decoded frame as a GPU surface.
    ///
    /// `want_gpu` has to arrive with the packet, because the decoder decides
    /// *during* the call whether to skip the software transfer. It stays off
    /// every other platform's signature.
    #[cfg(target_os = "macos")]
    pub fn decode_gpu(&mut self, packet: &[u8], want_gpu: bool) -> Result<Decoded<'_>, i32> {
        self.decode_inner(packet, want_gpu)
    }

    unsafe extern "C" fn callback(
        obj: *const c_void,
        width: c_int,
        height: c_int,
        pixfmt: c_int,
        linesizes: *mut c_int,
        datas: *mut *mut u8,
        key: c_int,
    ) {
        // A panic here aborts the process: the release profile is `panic = "abort"`,
        // so `catch_unwind` would be inert and this callback has to be non-panicking
        // by construction. That means validating every pointer and length the C side
        // hands over before building a slice from it -- `from_raw_parts` on a null
        // pointer, or on a negative linesize cast to usize, is undefined behaviour
        // rather than a panic, and no unwinding story would catch it either.
        if obj.is_null() || datas.is_null() || linesizes.is_null() {
            error!("decode callback got a null object or plane array");
            return;
        }
        if width <= 0 || height <= 0 {
            error!(
                "decode callback got a non-positive frame size {}x{}",
                width, height
            );
            return;
        }

        let output = &mut *(obj as *mut DecodeOutput);
        let datas = from_raw_parts(datas, AV_NUM_DATA_POINTERS as _);
        let linesizes = from_raw_parts(linesizes, AV_NUM_DATA_POINTERS as _);

        // Every plane this format reads is dereferenced below, so validate the
        // pointer and the stride first: a null plane or a non-positive stride is a
        // producer bug, and dropping the frame beats reading unmapped memory.
        let planes = if pixfmt == AVPixelFormat::AV_PIX_FMT_YUV420P as c_int {
            3
        } else if pixfmt == AVPixelFormat::AV_PIX_FMT_NV12 as c_int {
            2
        } else {
            0
        };
        for i in 0..planes {
            if datas[i].is_null() {
                error!("decode callback got a null plane {}", i);
                return;
            }
            if linesizes[i] <= 0 {
                error!(
                    "decode callback got a non-positive linesize {} on plane {}",
                    linesizes[i], i
                );
                return;
            }
        }

        let mut frame = DecodeFrame {
            pixfmt: std::mem::transmute(pixfmt),
            width,
            height,
            data: vec![],
            linesize: vec![],
            key: key != 0,
        };

        if pixfmt == AVPixelFormat::AV_PIX_FMT_YUV420P as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height) as usize).to_vec();
            let u = from_raw_parts(datas[1], (linesizes[1] * height / 2) as usize).to_vec();
            let v = from_raw_parts(datas[2], (linesizes[2] * height / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(u);
            frame.data.push(v);

            frame.linesize.push(linesizes[0]);
            frame.linesize.push(linesizes[1]);
            frame.linesize.push(linesizes[2]);

            // A software frame wins over a GPU frame of the same transaction.
            #[cfg(target_os = "macos")]
            {
                output.pixelbuffer = None;
            }
            output.frames.push(frame);
        } else if pixfmt == AVPixelFormat::AV_PIX_FMT_NV12 as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height) as usize).to_vec();
            let uv = from_raw_parts(datas[1], (linesizes[1] * height / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(uv);

            frame.linesize.push(linesizes[0]);
            frame.linesize.push(linesizes[1]);

            // A software frame wins over a GPU frame of the same transaction.
            #[cfg(target_os = "macos")]
            {
                output.pixelbuffer = None;
            }
            output.frames.push(frame);
        } else {
            error!("unsupported pixfmt {}", pixfmt as i32);
        }
    }

    /// Borrowed-descriptor callback from the decoder.
    ///
    /// Accepting means "retained and stored"; rejecting means "nothing kept",
    /// so the decoder delivers the same decoded frame through the software
    /// path. This must never panic: a panic across the FFI boundary aborts the
    /// process in release builds (`panic = "abort"`).
    #[cfg(target_os = "macos")]
    unsafe extern "C" fn pixelbuffer_callback(
        obj: *mut c_void,
        frame: *const FfiPixelBufferFrame,
    ) -> c_int {
        if obj.is_null() || frame.is_null() {
            return PIXELBUFFER_REJECTED;
        }
        let output = &mut *(obj as *mut DecodeOutput);
        // Rejecting here is what makes the C++ side fall back to its software
        // transfer on this very frame, so the renderer never has a frame taken
        // away from it.
        //
        // Delivery is opt-in: a call that did not ask for the GPU frame keeps
        // the software transfer, exactly like before this API existed.
        if !output.want_gpu {
            return PIXELBUFFER_REJECTED;
        }
        let frame = &*frame;
        if !is_direct_deliverable(frame.pixfmt) || frame.pixel_buffer.is_null() {
            return PIXELBUFFER_REJECTED;
        }
        match PixelBufferFrame::retain_from_borrowed(frame) {
            Some(pixelbuffer) => {
                output.pixelbuffer = Some(pixelbuffer);
                PIXELBUFFER_ACCEPTED
            }
            None => PIXELBUFFER_REJECTED,
        }
    }

    pub fn available_decoders() -> Vec<CodecInfo> {
        use log::debug;

        #[allow(unused_mut)]
        let mut codecs: Vec<CodecInfo> = vec![];
        // windows disable nvdec to avoid gpu stuck
        #[cfg(target_os = "linux")]
        {
            let (nv, _, _) = crate::common::supported_gpu(false);
            debug!("Linux GPU support detected - NV: {}", nv);
            if nv {
                codecs.push(CodecInfo {
                    name: "h264".to_owned(),
                    format: H264,
                    hwdevice: AV_HWDEVICE_TYPE_CUDA,
                    priority: Priority::Good as _,
                    ..Default::default()
                });
                codecs.push(CodecInfo {
                    name: "hevc".to_owned(),
                    format: H265,
                    hwdevice: AV_HWDEVICE_TYPE_CUDA,
                    priority: Priority::Good as _,
                    ..Default::default()
                });
            }
        }

        #[cfg(target_os = "windows")]
        {
            codecs.append(&mut vec![
                CodecInfo {
                    name: "h264".to_owned(),
                    format: H264,
                    hwdevice: AV_HWDEVICE_TYPE_D3D11VA,
                    priority: Priority::Best as _,
                    ..Default::default()
                },
                CodecInfo {
                    name: "hevc".to_owned(),
                    format: H265,
                    hwdevice: AV_HWDEVICE_TYPE_D3D11VA,
                    priority: Priority::Best as _,
                    ..Default::default()
                },
            ]);
        }

        #[cfg(target_os = "linux")]
        {
            codecs.append(&mut vec![
                CodecInfo {
                    name: "h264".to_owned(),
                    format: H264,
                    hwdevice: AV_HWDEVICE_TYPE_VAAPI,
                    priority: Priority::Good as _,
                    ..Default::default()
                },
                CodecInfo {
                    name: "hevc".to_owned(),
                    format: H265,
                    hwdevice: AV_HWDEVICE_TYPE_VAAPI,
                    priority: Priority::Good as _,
                    ..Default::default()
                },
            ]);
        }

        #[cfg(target_os = "macos")]
        {
            let (_, _, h264, h265) = crate::common::get_video_toolbox_codec_support();
            debug!(
                "VideoToolbox decode support - H264: {}, H265: {}",
                h264, h265
            );
            if h264 {
                codecs.push(CodecInfo {
                    name: "h264".to_owned(),
                    format: H264,
                    hwdevice: AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                    priority: Priority::Best as _,
                    ..Default::default()
                });
            }
            if h265 {
                codecs.push(CodecInfo {
                    name: "hevc".to_owned(),
                    format: H265,
                    hwdevice: AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                    priority: Priority::Best as _,
                    ..Default::default()
                });
            }
        }

        let mut res = Vec::<CodecInfo>::new();
        let buf264 = &crate::common::DATA_H264_720P[..];
        let buf265 = &crate::common::DATA_H265_720P[..];

        for codec in codecs {
            // Skip if this format already exists in results
            if res
                .iter()
                .any(|existing: &CodecInfo| existing.format == codec.format)
            {
                continue;
            }

            debug!(
                "Testing decoder: {} (hwdevice: {:?})",
                codec.name, codec.hwdevice
            );

            let c = DecodeContext {
                name: codec.name.clone(),
                device_type: codec.hwdevice,
                thread_count: 4,
            };

            match Decoder::new(c) {
                Ok(mut decoder) => {
                    debug!("Decoder {} created successfully", codec.name);
                    let data = match codec.format {
                        H264 => buf264,
                        H265 => buf265,
                        _ => {
                            log::error!("Unsupported format: {:?}, skipping", codec.format);
                            continue;
                        }
                    };

                    let start = Instant::now();

                    match decoder.decode_inner(data, false) {
                        Ok(_) => {
                            let elapsed = start.elapsed().as_millis();

                            if elapsed < TEST_TIMEOUT_MS as _ {
                                debug!("Decoder {} test passed", codec.name);
                                res.push(codec);
                            } else {
                                debug!(
                                    "Decoder {} test failed - timeout: {}ms",
                                    codec.name, elapsed
                                );
                            }
                        }
                        Err(err) => {
                            debug!("Decoder {} test failed with error: {}", codec.name, err);
                        }
                    }
                }
                Err(_) => {
                    debug!("Failed to create decoder {}", codec.name);
                }
            }
        }

        let soft = CodecInfo::soft();
        if let Some(c) = soft.h264 {
            res.push(c);
        }
        if let Some(c) = soft.h265 {
            res.push(c);
        }

        res
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        unsafe {
            // Remove the callback first: after this no callback can reach the
            // output state, so freeing it below cannot race a decode.
            #[cfg(target_os = "macos")]
            ffmpeg_ram_set_pixelbuffer_callback(self.codec, None);
            ffmpeg_ram_free_decoder(self.codec);
            self.codec = std::ptr::null_mut();
            let _ = Box::from_raw(self.output);
        }
    }
}

/// Transaction-ordering tests for the GPU-frame path.
///
/// They live in their own file so the production side of this module reads
/// as the feature and nothing else.
#[cfg(all(test, target_os = "macos"))]
#[path = "tests/decode_output.rs"]
mod output_tests;

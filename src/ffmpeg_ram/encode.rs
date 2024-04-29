use crate::{
    common::{
        DataFormat::{self, *},
        Driver,
    },
    ffmpeg::{av_log_get_level, av_log_set_level, AVPixelFormat, AV_LOG_ERROR, AV_LOG_PANIC},
    ffmpeg_ram::{
        ffmpeg_linesize_offset_length, ffmpeg_ram_encode, ffmpeg_ram_free_encoder,
        ffmpeg_ram_new_encoder, ffmpeg_ram_set_bitrate, CodecInfo, Quality, RateControl,
        AV_NUM_DATA_POINTERS,
    },
};
use log::{error, trace};
use std::{
    ffi::{c_void, CString},
    fmt::Display,
    os::raw::c_int,
    slice,
    sync::{Arc, Mutex},
    thread,
    time::Instant,
};

use super::Priority;

#[derive(Debug, Clone, PartialEq)]
pub struct EncodeContext {
    pub name: String,
    pub width: i32,
    pub height: i32,
    pub pixfmt: AVPixelFormat,
    pub align: i32,
    pub bitrate: i32,
    pub timebase: [i32; 2],
    pub gop: i32,
    pub quality: Quality,
    pub rc: RateControl,
    pub thread_count: i32,
}

pub struct EncodeFrame {
    pub data: Vec<u8>,
    pub pts: i64,
    pub key: i32,
}

impl Display for EncodeFrame {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "encode len:{}, pts:{}", self.data.len(), self.pts)
    }
}

pub struct Encoder {
    codec: *mut c_void,
    frames: *mut Vec<EncodeFrame>,
    pub ctx: EncodeContext,
    pub linesize: Vec<i32>,
    pub offset: Vec<i32>,
    pub length: i32,
    start: Instant,
}

impl Encoder {
    pub fn new(ctx: EncodeContext) -> Result<Self, ()> {
        unsafe {
            let mut linesize = Vec::<i32>::new();
            linesize.resize(AV_NUM_DATA_POINTERS as _, 0);
            let mut offset = Vec::<i32>::new();
            offset.resize(AV_NUM_DATA_POINTERS as _, 0);
            let mut length = Vec::<i32>::new();
            length.resize(1, 0);
            let gpu = std::env::var("RUSTDESK_HWCODEC_NVENC_GPU")
                .unwrap_or("-1".to_owned())
                .parse()
                .unwrap_or(-1);
            let codec = ffmpeg_ram_new_encoder(
                CString::new(ctx.name.as_str()).map_err(|_| ())?.as_ptr(),
                ctx.width,
                ctx.height,
                ctx.pixfmt as c_int,
                ctx.align,
                ctx.bitrate as _,
                ctx.timebase[0],
                ctx.timebase[1],
                ctx.gop,
                ctx.quality as _,
                ctx.rc as _,
                ctx.thread_count,
                gpu,
                linesize.as_mut_ptr(),
                offset.as_mut_ptr(),
                length.as_mut_ptr(),
                Some(Encoder::callback),
            );

            if codec.is_null() {
                return Err(());
            }

            Ok(Encoder {
                codec,
                frames: Box::into_raw(Box::new(Vec::<EncodeFrame>::new())),
                ctx,
                linesize,
                offset,
                length: length[0],
                start: Instant::now(),
            })
        }
    }

    pub fn encode(&mut self, data: &[u8]) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let result = ffmpeg_ram_encode(
                self.codec,
                (*data).as_ptr(),
                data.len() as _,
                self.frames as *const _ as *const c_void,
                self.start.elapsed().as_millis() as _,
            );
            if result != 0 {
                if av_log_get_level() >= AV_LOG_ERROR as _ {
                    error!("Error encode: {}", result);
                }
                return Err(result);
            }
            Ok(&mut *self.frames)
        }
    }

    extern "C" fn callback(data: *const u8, size: c_int, pts: i64, key: i32, obj: *const c_void) {
        unsafe {
            let frames = &mut *(obj as *mut Vec<EncodeFrame>);
            frames.push(EncodeFrame {
                data: slice::from_raw_parts(data, size as _).to_vec(),
                pts,
                key,
            });
        }
    }

    pub fn set_bitrate(&mut self, bitrate: i32) -> Result<(), ()> {
        let ret = unsafe { ffmpeg_ram_set_bitrate(self.codec, bitrate) };
        if ret == 0 {
            Ok(())
        } else {
            Err(())
        }
    }

    pub fn format_from_name(name: String) -> Result<DataFormat, ()> {
        if name.contains("h264") {
            return Ok(H264);
        } else if name.contains("hevc") {
            return Ok(H265);
        }
        Err(())
    }

    pub fn available_encoders(ctx: EncodeContext, sdk: Option<String>) -> Vec<CodecInfo> {
        static mut INSTANCE: Vec<CodecInfo> = vec![];
        static mut CACHED_CTX: Option<EncodeContext> = None;

        unsafe {
            if CACHED_CTX.clone().take() != Some(ctx.clone()) {
                CACHED_CTX = Some(ctx.clone());
                INSTANCE = Encoder::available_encoders_(ctx, sdk);
            }
            INSTANCE.clone()
        }
    }

    fn available_encoders_(ctx: EncodeContext, _sdk: Option<String>) -> Vec<CodecInfo> {
        if !(cfg!(windows) || cfg!(target_os = "linux")) {
            return vec![];
        }

        let log_level;
        unsafe {
            log_level = av_log_get_level();
            av_log_set_level(AV_LOG_PANIC as _);
        };
        let contains = |_driver: Driver, _format: DataFormat| {
            #[cfg(all(windows, feature = "vram"))]
            {
                if let Some(_sdk) = _sdk.as_ref() {
                    if !_sdk.is_empty() {
                        if let Ok(available) = crate::vram::Available::deserialize(_sdk.as_str()) {
                            return available.contains(true, _driver, _format);
                        }
                    }
                }
            }
            true
        };

        let (_nv, amf, _vpl) = crate::common::supported_gpu(true);
        let mut codecs = vec![];
        #[cfg(windows)]
        codecs.push(CodecInfo {
            name: "h264_mf".to_owned(),
            format: H264,
            priority: Priority::Good as _,
            ..Default::default()
        });
        #[cfg(windows)]
        codecs.push(CodecInfo {
            name: "hevc_mf".to_owned(),
            format: H265,
            priority: Priority::Good as _,
            ..Default::default()
        });
        // windows disable nvenc to avoid gpu stuck
        #[cfg(target_os = "linux")]
        if _nv && contains(Driver::NV, H264) {
            codecs.push(CodecInfo {
                name: "h264_nvenc".to_owned(),
                format: H264,
                priority: Priority::Good as _,
                ..Default::default()
            });
        }
        #[cfg(target_os = "linux")]
        if _nv && contains(Driver::NV, H265) {
            codecs.push(CodecInfo {
                name: "hevc_nvenc".to_owned(),
                format: H265,
                priority: Priority::Good as _,
                ..Default::default()
            });
        }
        if amf && contains(Driver::AMF, H264) {
            codecs.push(CodecInfo {
                name: "h264_amf".to_owned(),
                format: H264,
                priority: Priority::Best as _,
                ..Default::default()
            });
        }
        if amf {
            // sdk not use h265
            codecs.push(CodecInfo {
                name: "hevc_amf".to_owned(),
                format: H265,
                priority: Priority::Best as _,
                ..Default::default()
            });
        }
        #[cfg(target_os = "linux")]
        {
            if _vpl && contains(Driver::VPL, H264) {
                codecs.push(CodecInfo {
                    name: "h264_qsv".to_owned(),
                    format: H264,
                    priority: Priority::Best as _,
                    ..Default::default()
                });
            }
            if _vpl && contains(Driver::VPL, H265) {
                codecs.push(CodecInfo {
                    name: "hevc_qsv".to_owned(),
                    format: H265,
                    priority: Priority::Best as _,
                    ..Default::default()
                });
            }
        }

        // qsv doesn't support yuv420p
        codecs.retain(|c| {
            let ctx = ctx.clone();
            if ctx.pixfmt == AVPixelFormat::AV_PIX_FMT_YUV420P && c.name.contains("qsv") {
                return false;
            }
            return true;
        });

        let infos = Arc::new(Mutex::new(Vec::<CodecInfo>::new()));
        let mut res = vec![];

        let start = Instant::now();
        let cu_mutex = Arc::new(Mutex::new(0));
        if let Ok(yuv) = Encoder::dummy_yuv(ctx.clone()) {
            log::debug!("prepare yuv {:?}", start.elapsed());
            let yuv = Arc::new(yuv);
            let mut handles = vec![];
            for codec in codecs {
                let yuv = yuv.clone();
                let infos = infos.clone();
                let cu_mutex = cu_mutex.clone();
                let handle = thread::spawn(move || {
                    let _cu_lock;
                    if codec.name.contains("nvenc") {
                        _cu_lock = cu_mutex.lock().unwrap();
                    }
                    let c = EncodeContext {
                        name: codec.name.clone(),
                        ..ctx
                    };
                    let start = Instant::now();
                    if let Ok(mut encoder) = Encoder::new(c) {
                        log::debug!("{} new {:?}", codec.name, start.elapsed());
                        let start = Instant::now();
                        if let Ok(_) = encoder.encode(&yuv) {
                            log::debug!("{} encode {:?}", codec.name, start.elapsed());
                            infos.lock().unwrap().push(codec);
                        } else {
                            log::debug!("{} encode failed {:?}", codec.name, start.elapsed());
                        }
                    } else {
                        log::debug!("{} new failed {:?}", codec.name, start.elapsed());
                    }
                });
                handles.push(handle);
            }
            for handle in handles {
                handle.join().ok();
            }
            res = infos.lock().unwrap().clone();
        }

        unsafe {
            av_log_set_level(log_level);
        }

        res
    }

    fn dummy_yuv(ctx: EncodeContext) -> Result<Vec<u8>, ()> {
        let mut yuv = vec![];
        if let Ok((_, _, len)) = ffmpeg_linesize_offset_length(
            ctx.pixfmt,
            ctx.width as _,
            ctx.height as _,
            ctx.align as _,
        ) {
            yuv.resize(len as _, 0);
            return Ok(yuv);
        }

        Err(())
    }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            ffmpeg_ram_free_encoder(self.codec);
            self.codec = std::ptr::null_mut();
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}

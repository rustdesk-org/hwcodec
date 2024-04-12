use crate::{
    common::{
        DataFormat::{self, *},
        Driver,
    },
    ffmpeg::{
        av_log_get_level, av_log_set_level,
        AVHWDeviceType::{self, *},
        AVPixelFormat, AV_LOG_ERROR, AV_LOG_PANIC,
    },
    ffmpeg_ram::{
        ffmpeg_ram_decode, ffmpeg_ram_free_decoder, ffmpeg_ram_new_decoder, CodecInfo,
        AV_NUM_DATA_POINTERS,
    },
};
use log::{error, trace};
use std::{
    ffi::{c_void, CString},
    os::raw::c_int,
    slice::from_raw_parts,
    sync::{Arc, Mutex},
    thread,
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

pub struct Decoder {
    codec: *mut c_void,
    frames: *mut Vec<DecodeFrame>,
    pub ctx: DecodeContext,
}

unsafe impl Send for Decoder {}
unsafe impl Sync for Decoder {}

impl Decoder {
    pub fn new(ctx: DecodeContext) -> Result<Self, ()> {
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

            Ok(Decoder {
                codec,
                frames: Box::into_raw(Box::new(Vec::<DecodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn decode(&mut self, packet: &[u8]) -> Result<&mut Vec<DecodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let ret = ffmpeg_ram_decode(
                self.codec,
                packet.as_ptr(),
                packet.len() as c_int,
                self.frames as *const _ as *const c_void,
            );

            if ret < 0 {
                if av_log_get_level() >= AV_LOG_ERROR as _ {
                    error!("Error decode: {}", ret);
                }
                Err(ret)
            } else {
                Ok(&mut *self.frames)
            }
        }
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
        let frames = &mut *(obj as *mut Vec<DecodeFrame>);
        let datas = from_raw_parts(datas, AV_NUM_DATA_POINTERS as _);
        let linesizes = from_raw_parts(linesizes, AV_NUM_DATA_POINTERS as _);

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

            frames.push(frame);
        } else if pixfmt == AVPixelFormat::AV_PIX_FMT_NV12 as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height) as usize).to_vec();
            let uv = from_raw_parts(datas[1], (linesizes[1] * height / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(uv);

            frame.linesize.push(linesizes[0]);
            frame.linesize.push(linesizes[1]);

            frames.push(frame);
        } else {
            error!("unsupported pixfmt {}", pixfmt as i32);
        }
    }

    pub fn available_decoders(sdk: Option<String>) -> Vec<CodecInfo> {
        use std::{mem::MaybeUninit, sync::Once};

        static mut INSTANCE: MaybeUninit<Vec<CodecInfo>> = MaybeUninit::uninit();
        static ONCE: Once = Once::new();

        ONCE.call_once(|| unsafe {
            INSTANCE
                .as_mut_ptr()
                .write(Decoder::available_decoders_(sdk));
        });
        unsafe { (&*INSTANCE.as_ptr()).clone() }
    }

    fn available_decoders_(_sdk: Option<String>) -> Vec<CodecInfo> {
        let log_level;
        unsafe {
            log_level = av_log_get_level();
            av_log_set_level(AV_LOG_PANIC as _);
        };

        let (nv, _, _) = crate::common::supported_gpu(false);
        let contains = |_driver: Driver, _format: DataFormat| {
            #[cfg(all(windows, feature = "vram"))]
            {
                if let Some(_sdk) = _sdk.as_ref() {
                    if !_sdk.is_empty() {
                        if let Ok(available) = crate::native::Available::deserialize(_sdk.as_str())
                        {
                            return available.contains(false, _driver, _format);
                        }
                    }
                }
            }
            true
        };
        let mut codecs = vec![];
        if nv && contains(Driver::NV, H264) {
            codecs.push(CodecInfo {
                name: "h264".to_owned(),
                format: H264,
                hwdevice: AV_HWDEVICE_TYPE_CUDA,
                score: 94,
            });
        }
        if nv && contains(Driver::NV, H265) {
            codecs.push(CodecInfo {
                name: "hevc".to_owned(),
                format: H265,
                hwdevice: AV_HWDEVICE_TYPE_CUDA,
                score: 95,
            });
        }

        #[cfg(target_os = "windows")]
        {
            codecs.append(&mut vec![
                CodecInfo {
                    name: "h264".to_owned(),
                    format: H264,
                    hwdevice: AV_HWDEVICE_TYPE_D3D11VA,
                    score: 91,
                },
                CodecInfo {
                    name: "hevc".to_owned(),
                    format: H265,
                    hwdevice: AV_HWDEVICE_TYPE_D3D11VA,
                    score: 91,
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
                    score: 70,
                },
                CodecInfo {
                    name: "hevc".to_owned(),
                    format: H265,
                    hwdevice: AV_HWDEVICE_TYPE_VAAPI,
                    score: 70,
                },
            ]);
        }

        let infos = Arc::new(Mutex::new(Vec::<CodecInfo>::new()));
        let buf264 = Arc::new(crate::common::DATA_H264_720P);
        let buf265 = Arc::new(crate::common::DATA_H265_720P);
        let mut handles = vec![];
        for codec in codecs {
            let infos = infos.clone();
            let buf264 = buf264.clone();
            let buf265 = buf265.clone();
            let handle = thread::spawn(move || {
                let c = DecodeContext {
                    name: codec.name.clone(),
                    device_type: codec.hwdevice,
                    thread_count: 4,
                };
                let start = Instant::now();
                if let Ok(mut decoder) = Decoder::new(c) {
                    log::debug!(
                        "name:{} device:{:?} new:{:?}",
                        codec.name.clone(),
                        codec.hwdevice,
                        start.elapsed()
                    );
                    let data = match codec.format {
                        H264 => &buf264[..],
                        H265 => &buf265[..],
                        _ => {
                            log::error!("unsupported format: {:?}", codec.format);
                            return;
                        }
                    };
                    let start = Instant::now();
                    if let Ok(_) = decoder.decode(data) {
                        log::debug!(
                            "name:{} device:{:?} decode:{:?}",
                            codec.name,
                            codec.hwdevice,
                            start.elapsed()
                        );
                        infos.lock().unwrap().push(codec);
                    } else {
                        log::debug!(
                            "name:{} device:{:?} decode failed:{:?}",
                            codec.name,
                            codec.hwdevice,
                            start.elapsed()
                        );
                    }
                } else {
                    log::debug!(
                        "name:{} device:{:?} new failed:{:?}",
                        codec.name.clone(),
                        codec.hwdevice,
                        start.elapsed()
                    );
                }
            });

            handles.push(handle);
        }
        for handle in handles {
            handle.join().ok();
        }
        #[cfg(windows)]
        let res = infos.lock().unwrap().clone();
        #[cfg(target_os = "linux")]
        let mut res = infos.lock().unwrap().clone();

        #[cfg(target_os = "linux")]
        {
            // VAAPI is slow on nvidia, but fast on amd
            if res.iter().all(|c| c.hwdevice != AV_HWDEVICE_TYPE_CUDA) {
                if std::process::Command::new("sh")
                    .arg("-c")
                    .arg("lsmod | grep amdgpu >/dev/null 2>&1")
                    .status()
                    .map_or(false, |status| status.code() == Some(0))
                {
                    res.iter_mut()
                        .filter(|c| c.hwdevice == AV_HWDEVICE_TYPE_VAAPI)
                        .map(|c| c.score = 91)
                        .count();
                }
            }
        }

        unsafe {
            av_log_set_level(log_level);
        }

        res
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        unsafe {
            ffmpeg_ram_free_decoder(self.codec);
            self.codec = std::ptr::null_mut();
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}

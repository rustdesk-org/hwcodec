use crate::{
    common::{AdapterDesc, DataFormat::*, Driver::*},
    vram::{amf, ffmpeg, inner::DecodeCalls, mfx, nv, DecodeContext},
};
use log::{error, trace};
use std::{
    ffi::c_void,
    sync::{Arc, Mutex},
    thread,
};

pub struct Decoder {
    calls: DecodeCalls,
    codec: *mut c_void,
    frames: *mut Vec<DecodeFrame>,
    pub ctx: DecodeContext,
}

unsafe impl Send for Decoder {}
unsafe impl Sync for Decoder {}

impl Decoder {
    pub fn new(ctx: DecodeContext) -> Result<Self, ()> {
        let calls = match ctx.driver {
            NV => nv::decode_calls(),
            AMF => amf::decode_calls(),
            MFX => mfx::decode_calls(),
            FFMPEG => ffmpeg::decode_calls(),
        };
        unsafe {
            let codec = (calls.new)(
                ctx.device.unwrap_or(std::ptr::null_mut()),
                ctx.luid,
                ctx.api as i32,
                ctx.data_format as i32,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Self {
                calls,
                codec,
                frames: Box::into_raw(Box::new(Vec::<DecodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn decode(&mut self, packet: &[u8]) -> Result<&mut Vec<DecodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let ret = (self.calls.decode)(
                self.codec,
                packet.as_ptr() as _,
                packet.len() as _,
                Some(Self::callback),
                self.frames as *mut _ as *mut c_void,
            );

            if ret != 0 {
                error!("Error decode: {}", ret);
                Err(ret)
            } else {
                Ok(&mut *self.frames)
            }
        }
    }

    unsafe extern "C" fn callback(texture: *mut c_void, obj: *const c_void) {
        let frames = &mut *(obj as *mut Vec<DecodeFrame>);

        let frame = DecodeFrame { texture };
        frames.push(frame);
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        unsafe {
            (self.calls.destroy)(self.codec);
            self.codec = std::ptr::null_mut();
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}

pub struct DecodeFrame {
    pub texture: *mut c_void,
}

pub fn available() -> Vec<DecodeContext> {
    let mut codecs: Vec<_> = vec![];
    // disable nv sdk decode
    // codecs.append(
    //     &mut nv::possible_support_decoders()
    //         .drain(..)
    //         .map(|n| (NV, n))
    //         .collect(),
    // );
    codecs.append(
        &mut ffmpeg::possible_support_decoders()
            .drain(..)
            .map(|n| (FFMPEG, n))
            .collect(),
    );
    codecs.append(
        &mut amf::possible_support_decoders()
            .drain(..)
            .map(|n| (AMF, n))
            .collect(),
    );
    codecs.append(
        &mut mfx::possible_support_decoders()
            .drain(..)
            .map(|n| (MFX, n))
            .collect(),
    );
    let inputs = codecs.drain(..).map(|(driver, n)| DecodeContext {
        device: None,
        driver,
        data_format: n.data_format,
        api: n.api,
        luid: 0,
    });
    let outputs = Arc::new(Mutex::new(Vec::<DecodeContext>::new()));
    let buf264 = Arc::new(crate::common::DATA_H264_720P);
    let buf265 = Arc::new(crate::common::DATA_H265_720P);
    let mut handles = vec![];
    let mutex = Arc::new(Mutex::new(0));
    for input in inputs {
        let outputs = outputs.clone();
        let buf264 = buf264.clone();
        let buf265 = buf265.clone();
        let mutex = mutex.clone();
        let handle = thread::spawn(move || {
            let _lock;
            if input.driver == NV || input.driver == FFMPEG {
                _lock = mutex.lock().unwrap();
            }
            let test = match input.driver {
                NV => nv::decode_calls().test,
                AMF => amf::decode_calls().test,
                MFX => mfx::decode_calls().test,
                FFMPEG => ffmpeg::decode_calls().test,
            };
            let mut descs: Vec<AdapterDesc> = vec![];
            descs.resize(crate::vram::MAX_ADATERS, unsafe { std::mem::zeroed() });
            let mut desc_count: i32 = 0;
            let data = match input.data_format {
                H264 => &buf264[..],
                H265 => &buf265[..],
                _ => return,
            };
            if 0 == unsafe {
                test(
                    descs.as_mut_ptr() as _,
                    descs.len() as _,
                    &mut desc_count,
                    input.api as _,
                    input.data_format as i32,
                    data.as_ptr() as *mut u8,
                    data.len() as _,
                )
            } {
                if desc_count as usize <= descs.len() {}
                for i in 0..desc_count as usize {
                    let mut input = input.clone();
                    input.luid = descs[i].luid;
                    outputs.lock().unwrap().push(input);
                }
            }
        });

        handles.push(handle);
    }
    for handle in handles {
        handle.join().ok();
    }
    let x = outputs.lock().unwrap().clone();
    x
}

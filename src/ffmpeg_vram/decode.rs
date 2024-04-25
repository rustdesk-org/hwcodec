use crate::{
    common::{AdapterDesc, DataFormat::*, Driver::*},
    ffmpeg_vram::ffmpeg_vram_free_decoder,
};
use log::{error, trace};
use std::{
    ffi::c_void,
    sync::{Arc, Mutex},
    thread,
};

use super::{ffmpeg_vram_decode, ffmpeg_vram_new_decoder, DecodeContext};

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
            let codec = ffmpeg_vram_new_decoder(
                ctx.device.unwrap_or(std::ptr::null_mut()),
                ctx.luid,
                ctx.api as i32,
                ctx.data_format as i32,
                ctx.output_shared_handle,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Self {
                codec,
                frames: Box::into_raw(Box::new(Vec::<DecodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn decode(&mut self, packet: &[u8]) -> Result<&mut Vec<DecodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let ret = ffmpeg_vram_decode(
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
            ffmpeg_vram_free_decoder(self.codec);
            self.codec = std::ptr::null_mut();
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}

pub struct DecodeFrame {
    pub texture: *mut c_void,
}

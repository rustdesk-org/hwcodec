#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/ffmpeg_vram_ffi.rs"));

use crate::{
    common::{DataFormat::*, API::*},
    vram::inner::{DecodeCalls, InnerDecodeContext},
};

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: ffmpeg_vram_new_decoder,
        decode: ffmpeg_vram_decode,
        destroy: ffmpeg_vram_destroy_decoder,
        test: ffmpeg_vram_test_decode,
    }
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    let mut devices = vec![];
    #[cfg(windows)]
    devices.append(&mut vec![API_DX11]);
    let codecs = vec![H264, H265];
    let mut v = vec![];
    for device in devices.iter() {
        for codec in codecs.iter() {
            v.push(InnerDecodeContext {
                api: device.clone(),
                data_format: codec.clone(),
            });
        }
    }
    v
}

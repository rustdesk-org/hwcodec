#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/ffmpeg_vram_ffi.rs"));

use crate::{
    common::DataFormat::*,
    vram::inner::{InnerDecodeContext, InnerEncodeContext},
};

pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    let dataFormats = vec![H264, H265];
    let mut v = vec![];
    for dataFormat in dataFormats.iter() {
        v.push(InnerEncodeContext {
            format: dataFormat.clone(),
        });
    }
    v
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    let codecs = vec![H264, H265];
    let mut v = vec![];
    for codec in codecs.iter() {
        v.push(InnerDecodeContext {
            data_format: codec.clone(),
        });
    }
    v
}

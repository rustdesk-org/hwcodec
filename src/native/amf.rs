#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/amf_ffi.rs"));

use crate::{
    common::{DataFormat::*, FormatMASK::*, API::*},
    native::inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: amf_new_encoder,
        encode: amf_encode,
        destroy: amf_destroy_encoder,
        test: amf_test_encode,
        set_bitrate: amf_set_bitrate,
        set_framerate: amf_set_framerate,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: amf_new_decoder,
        decode: amf_decode,
        destroy: amf_destroy_decoder,
        test: amf_test_decode,
    }
}

pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    let mask = MASK_H264 as i32 | MASK_H265 as i32;
    let mask = unsafe { amf_encode_driver_support(mask) };
    if mask < 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let mut codecs = vec![];
    if mask & MASK_H264 as i32 != 0 {
        codecs.push(H264);
    }
    if mask & MASK_H265 as i32 != 0 {
        codecs.push(H265);
    }
    let mut v = vec![];
    for device in devices.iter() {
        for codec in codecs.iter() {
            v.push(InnerEncodeContext {
                api: device.clone(),
                format: codec.clone(),
            });
        }
    }
    v
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    let mask = MASK_H264 as i32;
    let mask = unsafe { amf_decode_driver_support(mask) };
    if mask < 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    // https://github.com/GPUOpen-LibrariesAndSDKs/AMF/issues/432#issuecomment-1873141122
    let mut codecs = vec![];
    if mask & MASK_H264 as i32 != 0 {
        codecs.push(H264);
    }
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

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/nv_ffi.rs"));

use crate::{
    common::{DataFormat::*, FormatMASK::*, API::*},
    native::inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: nv_new_encoder,
        encode: nv_encode,
        destroy: nv_destroy_encoder,
        test: nv_test_encode,
        set_bitrate: nv_set_bitrate,
        set_framerate: nv_set_framerate,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: nv_new_decoder,
        decode: nv_decode,
        destroy: nv_destroy_decoder,
        test: nv_test_decode,
    }
}

pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    let mask = MASK_H264 as i32 | MASK_H265 as i32;
    let mask = unsafe { nv_encode_driver_support(mask) };
    if mask < 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let mut dataFormats = vec![];
    if mask & MASK_H264 as i32 != 0 {
        dataFormats.push(H264);
    }
    if mask & MASK_H265 as i32 != 0 {
        dataFormats.push(H265);
    }
    let mut v = vec![];
    for device in devices.iter() {
        for dataFormat in dataFormats.iter() {
            v.push(InnerEncodeContext {
                api: device.clone(),
                format: dataFormat.clone(),
            });
        }
    }
    v
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    let mask = MASK_H264 as i32 | MASK_H265 as i32;
    let mask = unsafe { nv_decode_driver_support(mask) };
    if mask < 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let mut dataFormats = vec![];
    if mask & MASK_H264 as i32 != 0 {
        dataFormats.push(H264);
    }
    if mask & MASK_H265 as i32 != 0 {
        dataFormats.push(H265);
    }
    let mut v = vec![];
    for device in devices.iter() {
        for dataFormat in dataFormats.iter() {
            v.push(InnerDecodeContext {
                api: device.clone(),
                data_format: dataFormat.clone(),
            });
        }
    }
    v
}

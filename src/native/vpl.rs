#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/vpl_ffi.rs"));

use crate::{
    common::{DataFormat::*, API::*},
    native::inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: vpl_new_encoder,
        encode: vpl_encode,
        destroy: vpl_destroy_encoder,
        test: vpl_test_encode,
        set_bitrate: vpl_set_bitrate,
        set_framerate: vpl_set_framerate,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: vpl_new_decoder,
        decode: vpl_decode,
        destroy: vpl_destroy_decoder,
        test: vpl_test_decode,
    }
}

pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    if unsafe { vpl_driver_support() } != 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let dataFormats = vec![H264, H265];
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
    if unsafe { vpl_driver_support() } != 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let dataFormats = vec![H264, H265];
    let mut v = vec![];
    for device in devices.iter() {
        for dataFormat in dataFormats.iter() {
            v.push(InnerDecodeContext {
                api: device.clone(),
                dataFormat: dataFormat.clone(),
            });
        }
    }
    v
}

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(unused)]
include!(concat!(env!("OUT_DIR"), "/amf_ffi.rs"));

use crate::{
    common::{DataFormat::*, API::*},
    vram::inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
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

// to-do: hardware ability
pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    if unsafe { amf_driver_support() } != 0 {
        return vec![];
    }
    let mut devices = vec![];
    #[cfg(windows)]
    devices.append(&mut vec![API_DX11]);
    #[cfg(target_os = "linux")]
    devices.append(&mut vec![API_OPENCL, API_VULKAN]);
    let codecs = vec![H264, H265];

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
    if unsafe { amf_driver_support() } != 0 {
        return vec![];
    }
    let mut devices = vec![];
    #[cfg(windows)]
    devices.append(&mut vec![API_DX11]);
    #[cfg(target_os = "linux")]
    devices.append(&mut vec![OPENCL, VULKAN]);
    // https://github.com/GPUOpen-LibrariesAndSDKs/AMF/issues/432#issuecomment-1873141122
    let codecs = vec![H264];

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

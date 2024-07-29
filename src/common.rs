#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use serde_derive::{Deserialize, Serialize};
include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub(crate) const DATA_H264_720P: &[u8] = include_bytes!("res/720p.h264");
pub(crate) const DATA_H265_720P: &[u8] = include_bytes!("res/720p.h265");

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum Driver {
    NV,
    AMF,
    MFX,
    FFMPEG,
}

#[cfg(target_os = "linux")]
pub(crate) fn linux_supported_gpu() -> (bool, bool, bool) {
    use std::ffi::c_int;
    extern "C" {
        pub(crate) fn linux_support_nv() -> c_int;
        pub(crate) fn linux_support_amd() -> c_int;
        pub(crate) fn linux_support_intel() -> c_int;
    }
    return (
        linux_support_nv() == 0,
        linux_support_amd() == 0,
        linux_support_intel() == 0,
    );
}

#[cfg(target_os = "macos")]
pub(crate) fn get_video_toolbox_codec_support() -> (bool, bool, bool, bool) {
    use std::ffi::c_void;

    extern "C" {
        fn checkVideoToolboxSupport(
            h264_encode: *mut i32,
            h265_encode: *mut i32,
            h264_decode: *mut i32,
            h265_decode: *mut i32,
        ) -> c_void;
    }

    let mut h264_encode = 0;
    let mut h265_encode = 0;
    let mut h264_decode = 0;
    let mut h265_decode = 0;
    unsafe {
        checkVideoToolboxSupport(
            &mut h264_encode as *mut _,
            &mut h265_encode as *mut _,
            &mut h264_decode as *mut _,
            &mut h265_decode as *mut _,
        );
    }
    (
        h264_encode == 1,
        h265_encode == 1,
        h264_decode == 1,
        h265_decode == 1,
    )
}

pub fn get_gpu_signature() -> u64 {
    #[cfg(any(windows, target_os = "macos"))]
    {
        extern "C" {
            pub fn GetHwcodecGpuSignature() -> u64;
        }
        unsafe { GetHwcodecGpuSignature() }
    }
    #[cfg(not(any(windows, target_os = "macos")))]
    {
        0
    }
}

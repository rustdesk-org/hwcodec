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

pub(crate) fn supported_gpu(_encode: bool) -> (bool, bool, bool) {
    #[cfg(target_os = "linux")]
    use std::ffi::c_int;
    #[cfg(target_os = "linux")]
    extern "C" {
        pub(crate) fn linux_support_nv() -> c_int;
        pub(crate) fn linux_support_amd() -> c_int;
        pub(crate) fn linux_support_intel() -> c_int;
    }

    #[allow(unused_unsafe)]
    unsafe {
        #[cfg(windows)]
        {
            #[cfg(feature = "vram")]
            return (
                _encode && crate::vram::nv::nv_encode_driver_support() == 0
                    || !_encode && crate::vram::nv::nv_decode_driver_support() == 0,
                crate::vram::amf::amf_driver_support() == 0,
                crate::vram::mfx::mfx_driver_support() == 0,
            );
            #[cfg(not(feature = "vram"))]
            return (true, true, true);
        }

        #[cfg(target_os = "linux")]
        return (
            linux_support_nv() == 0,
            linux_support_amd() == 0,
            linux_support_intel() == 0,
        );
        #[allow(unreachable_code)]
        (false, false, false)
    }
}

pub fn get_gpu_signature() -> u64 {
    #[cfg(windows)]
    {
        extern "C" {
            pub fn GetHwcodecGpuSignature() -> u64;
        }
        unsafe { GetHwcodecGpuSignature() }
    }
    #[cfg(not(windows))]
    {
        0
    }
}

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use serde_derive::{Deserialize, Serialize};
include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub(crate) const DATA_H264_720P: &[u8] = include_bytes!("res/720p.h264");
pub(crate) const DATA_H265_720P: &[u8] = include_bytes!("res/720p.h265");

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum Driver {
    #[cfg(feature = "nv")]
    NV,
    AMF,
    VPL,
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
            {
                #[cfg(feature = "nv")]
                let nv = _encode && crate::native::nv::nv_encode_driver_support() == 0
                    || !_encode && crate::native::nv::nv_decode_driver_support() == 0;
                #[cfg(not(feature = "nv"))]
                let nv = false;
                return (
                    nv,
                    crate::native::amf::amf_driver_support() == 0,
                    crate::native::vpl::vpl_driver_support() == 0,
                );
            }
            #[cfg(not(feature = "vram"))]
            return (cfg!(feature = "nv"), true, true);
        }
        #[cfg(target_os = "linux")]
        {
            #[cfg(feature = "nv")]
            let nv = linux_support_nv() == 0;
            #[cfg(not(feature = "nv"))]
            let nv = false;
            return (nv, linux_support_amd() == 0, linux_support_intel() == 0);
        }
        #[allow(unreachable_code)]
        (false, false, false)
    }
}

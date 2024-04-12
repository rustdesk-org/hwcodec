#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use serde_derive::{Deserialize, Serialize};
include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub(crate) fn query_capabilities(_encode: bool, mask: i32) -> (i32, i32, i32) {
    #[allow(unused_unsafe)]
    unsafe {
        #[cfg(all(windows, feature = "vram"))]
        {
            let intel = if crate::native::vpl::vpl_driver_support() == 0 {
                mask
            } else {
                -1
            };
            let nv_func = if _encode {
                crate::native::nv::nv_encode_driver_support
            } else {
                crate::native::nv::nv_decode_driver_support
            };
            let amd_func = if _encode {
                crate::native::amf::amf_encode_driver_support
            } else {
                crate::native::amf::amf_decode_driver_support
            };
            return (nv_func(mask), amd_func(mask), intel);
        }
        #[cfg(target_os = "linux")]
        {
            use std::ffi::c_int;
            extern "C" {
                pub(crate) fn linux_support_nv() -> c_int;
                pub(crate) fn linux_support_amd() -> c_int;
                pub(crate) fn linux_support_intel() -> c_int;
            }

            let nv = if linux_support_nv() == 0 { mask } else { -1 };
            let amd = if linux_support_amd() == 0 { mask } else { -1 };
            let intel = if linux_support_intel() == 0 { mask } else { -1 };
            return (nv, amd, intel);
        }
        #[allow(unreachable_code)]
        (mask, mask, mask)
    }
}

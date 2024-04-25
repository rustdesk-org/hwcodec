#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use crate::common::{DataFormat, Driver, API};
use crate::ffmpeg::{
    AVHWDeviceType::{self, *},
    AVPixelFormat,
};
use serde_derive::{Deserialize, Serialize};
use std::ffi::{c_int, c_void};

include!(concat!(env!("OUT_DIR"), "/ffmpeg_vram_ffi.rs"));

pub mod decode;

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct DecodeContext {
    #[serde(skip)]
    pub device: Option<*mut c_void>,
    pub driver: Driver,
    pub luid: i64,
    pub api: API,
    pub data_format: DataFormat,
    pub output_shared_handle: bool,
}

unsafe impl Send for DecodeContext {}
unsafe impl Sync for DecodeContext {}

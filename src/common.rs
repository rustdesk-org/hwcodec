#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
use std::ffi::c_void;

use serde_derive::{Deserialize, Serialize};
include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub use serde;
pub use serde_derive;

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum EncodeDriver {
    NVENC,
    AMF,
    VPL,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum DecodeDriver {
    CUVID,
    AMF,
    VPL,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct FeatureContext {
    pub driver: EncodeDriver,
    pub luid: i64,
    pub api: API,
    pub data_format: DataFormat,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
pub struct DynamicContext {
    #[serde(skip)]
    pub device: Option<*mut c_void>,
    pub width: i32,
    pub height: i32,
    pub kbitrate: i32,
    pub framerate: i32,
    pub gop: i32,
}

unsafe impl Send for DynamicContext {}
unsafe impl Sync for DynamicContext {}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct EncodeContext {
    pub f: FeatureContext,
    pub d: DynamicContext,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct DecodeContext {
    #[serde(skip)]
    pub device: Option<*mut c_void>,
    pub driver: DecodeDriver,
    pub luid: i64,
    pub api: API,
    pub data_format: DataFormat,
    pub output_shared_handle: bool,
}

unsafe impl Send for DecodeContext {}
unsafe impl Sync for DecodeContext {}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct Available {
    pub e: Vec<FeatureContext>,
    pub d: Vec<DecodeContext>,
}

impl Available {
    pub fn serialize(&self) -> Result<String, ()> {
        match serde_json::to_string_pretty(self) {
            Ok(s) => Ok(s),
            Err(_) => Err(()),
        }
    }

    pub fn deserialize(s: &str) -> Result<Self, ()> {
        match serde_json::from_str(s) {
            Ok(c) => Ok(c),
            Err(_) => Err(()),
        }
    }
}

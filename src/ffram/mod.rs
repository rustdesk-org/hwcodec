#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use crate::common::DataFormat;
use crate::ffmpeg::{AVHWDeviceType, AVPixelFormat};
use serde_derive::{Deserialize, Serialize};
use std::ffi::c_int;

include!(concat!(env!("OUT_DIR"), "/ffram_ffi.rs"));

pub mod decode;
pub mod encode;

#[derive(Debug, Eq, PartialEq, Clone, Serialize, Deserialize)]
pub struct CodecInfo {
    pub name: String,
    pub format: DataFormat,
    pub score: i32,
    pub hwdevice: AVHWDeviceType,
}

impl Default for CodecInfo {
    fn default() -> Self {
        Self {
            name: Default::default(),
            format: DataFormat::H264,
            score: Default::default(),
            hwdevice: AVHWDeviceType::AV_HWDEVICE_TYPE_NONE,
        }
    }
}

impl CodecInfo {
    pub fn score(coders: Vec<CodecInfo>) -> CodecInfos {
        let mut h264: Option<CodecInfo> = None;
        let mut h265: Option<CodecInfo> = None;

        for coder in coders {
            match coder.format {
                DataFormat::H264 => match &h264 {
                    Some(old) => {
                        if old.score < coder.score {
                            h264 = Some(coder)
                        }
                    }
                    None => h264 = Some(coder),
                },
                DataFormat::H265 => match &h265 {
                    Some(old) => {
                        if old.score < coder.score {
                            h265 = Some(coder)
                        }
                    }
                    None => h265 = Some(coder),
                },
                _ => {
                    log::error!("CodecInfo::score() called with non H264 or H265 format");
                }
            }
        }
        CodecInfos { h264, h265 }
    }
}

#[derive(Debug, Eq, PartialEq, Clone, Serialize, Deserialize)]
pub struct CodecInfos {
    pub h264: Option<CodecInfo>,
    pub h265: Option<CodecInfo>,
}

impl CodecInfos {
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

pub fn ffmpeg_linesize_offset_length(
    pixfmt: AVPixelFormat,
    width: usize,
    height: usize,
    align: usize,
) -> Result<(Vec<i32>, Vec<i32>, i32), ()> {
    let mut linesize = Vec::<c_int>::new();
    linesize.resize(AV_NUM_DATA_POINTERS as _, 0);
    let mut offset = Vec::<c_int>::new();
    offset.resize(AV_NUM_DATA_POINTERS as _, 0);
    let mut length = Vec::<c_int>::new();
    length.resize(1, 0);
    unsafe {
        if ffram_get_linesize_offset_length(
            pixfmt as _,
            width as _,
            height as _,
            align as _,
            linesize.as_mut_ptr(),
            offset.as_mut_ptr(),
            length.as_mut_ptr(),
        ) == 0
        {
            return Ok((linesize, offset, length[0]));
        }
    }

    Err(())
}

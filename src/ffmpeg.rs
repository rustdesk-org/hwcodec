#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/ffi.rs"));

use std::os::raw::c_int;
use std::result::Result;

#[derive(Debug, Eq, PartialEq, Clone, Copy)]
pub enum DataFormat {
    H264,
    H265,
}

#[derive(Debug, Eq, PartialEq, Clone, Copy)]
pub enum Vendor {
    NVIDIA,
    AMD,
    INTEL,
    OTHER,
}

#[derive(Debug, Eq, PartialEq, Clone)]
pub struct CodecInfo {
    pub name: String,
    pub format: DataFormat,
    pub vendor: Vendor,
    pub score: i32,
    pub hwdevice: AVHWDeviceType,
}

impl Default for CodecInfo {
    fn default() -> Self {
        Self {
            name: Default::default(),
            format: DataFormat::H264,
            vendor: Vendor::OTHER,
            score: Default::default(),
            hwdevice: AVHWDeviceType::AV_HWDEVICE_TYPE_NONE,
        }
    }
}

impl CodecInfo {
    pub fn score(coders: Vec<CodecInfo>) -> (Option<CodecInfo>, Option<CodecInfo>) {
        let mut coder_h264: Option<CodecInfo> = None;
        let mut coder_h265: Option<CodecInfo> = None;

        for coder in coders {
            match coder.format {
                DataFormat::H264 => match &coder_h264 {
                    Some(old) => {
                        if old.score < coder.score {
                            coder_h264 = Some(coder)
                        }
                    }
                    None => coder_h264 = Some(coder),
                },
                DataFormat::H265 => match &coder_h265 {
                    Some(old) => {
                        if old.score < coder.score {
                            coder_h265 = Some(coder)
                        }
                    }
                    None => coder_h265 = Some(coder),
                },
            }
        }
        (coder_h264, coder_h265)
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
        if get_linesize_offset_length(
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

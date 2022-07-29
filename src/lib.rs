#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/ffi.rs"));

pub mod decode;
pub mod encode;
pub mod ffmpeg;
pub mod mux;

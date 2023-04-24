use std::ffi::CStr;
use std::io::Write;
use std::os::raw::c_char;
use std::sync::mpsc;
use crate::init_ffmpeg_logger_;

use log::{debug, error, info, trace, warn};

pub fn init_ffmpeg_logger(){
    unsafe {
        init_ffmpeg_logger_();
    }
}

#[no_mangle]
fn log_error_rust(msg: *const c_char) {
    let msg = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    error!("{}", msg);
}

#[no_mangle]
fn log_debug_rust(msg: *const c_char) {
    let msg = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    debug!("{}", msg);
}

#[no_mangle]
fn log_info_rust(msg: *const c_char) {
    let msg = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    info!("{}", msg);
}

#[no_mangle]
fn log_trace_rust(msg: *const c_char) {
    let msg = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    trace!("{}", msg);
}

#[no_mangle]
fn log_warn_rust(msg: *const c_char) {
    let msg = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    warn!("{}", msg);
}

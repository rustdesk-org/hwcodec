use log::{error, trace};

use crate::{av_log_get_level, free_muxer, new_muxer, write_tail, write_video_frame, AV_LOG_ERROR};
use std::ffi::{c_void, CString};

#[derive(Debug, Clone, PartialEq)]
pub struct MuxContext {
    pub filename: String,
    pub width: usize,
    pub height: usize,
    pub is265: bool,
    pub framerate: usize,
}

pub struct Muxer {
    inner: Box<c_void>,
    pub ctx: MuxContext,
}

unsafe impl Send for Muxer {}
unsafe impl Sync for Muxer {}

impl Muxer {
    pub fn new(ctx: MuxContext) -> Result<Self, ()> {
        unsafe {
            let inner = new_muxer(
                CString::new(ctx.filename.as_str())
                    .map_err(|_| ())?
                    .as_ptr(),
                ctx.width as _,
                ctx.height as _,
                if ctx.is265 { 1 } else { 0 },
                ctx.framerate as _,
            );
            if inner.is_null() {
                return Err(());
            }

            Ok(Muxer {
                inner: Box::from_raw(inner as *mut c_void),
                ctx,
            })
        }
    }

    pub fn write_video(&mut self, data: &[u8], pts: i64) -> Result<(), i32> {
        unsafe {
            let result =
                write_video_frame(&mut *self.inner, (*data).as_ptr(), data.len() as _, pts);
            if result != 0 {
                if av_log_get_level() >= AV_LOG_ERROR as _ {
                    error!("Error write_video: {}", result);
                }
                return Err(result);
            }
            Ok(())
        }
    }

    pub fn write_tail(&mut self) -> Result<(), i32> {
        unsafe {
            let result = write_tail(&mut *self.inner);
            if result != 0 {
                if av_log_get_level() >= AV_LOG_ERROR as _ {
                    error!("Error write_tail: {}", result);
                }
                return Err(result);
            }
            Ok(())
        }
    }
}

impl Drop for Muxer {
    fn drop(&mut self) {
        unsafe {
            free_muxer(self.inner.as_mut());
            trace!("Muxer dropped");
        }
    }
}

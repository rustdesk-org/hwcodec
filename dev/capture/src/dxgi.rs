#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::os::raw::c_void;

include!(concat!(env!("OUT_DIR"), "/capture_ffi.rs"));

pub struct Capturer {
    inner: Box<c_void>,
}

impl Capturer {
    pub fn new(luid: i64) -> Result<Self, ()> {
        let inner = unsafe { dxgi_new_capturer(luid) };
        if inner.is_null() {
            Err(())
        } else {
            Ok(Self {
                inner: unsafe { Box::from_raw(inner) },
            })
        }
    }

    pub unsafe fn device(&mut self) -> *mut c_void {
        dxgi_device(self.inner.as_mut())
    }

    pub unsafe fn width(&self) -> i32 {
        dxgi_width(self.inner.as_ref())
    }

    pub unsafe fn height(&self) -> i32 {
        dxgi_height(self.inner.as_ref())
    }

    pub unsafe fn capture(&mut self, wait_ms: i32) -> *mut c_void {
        dxgi_capture(self.inner.as_mut(), wait_ms)
    }

    pub unsafe fn drop(&mut self) {
        destroy_dxgi_capturer(self.inner.as_mut());
    }
}

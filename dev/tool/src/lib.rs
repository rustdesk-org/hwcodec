#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::os::raw::c_void;

include!(concat!(env!("OUT_DIR"), "/tool_ffi.rs"));

pub struct Tool {
    inner: Box<c_void>,
}

impl Tool {
    pub fn new(luid: i64) -> Result<Self, ()> {
        let inner = unsafe { tool_new(luid) };
        if inner.is_null() {
            Err(())
        } else {
            Ok(Self {
                inner: unsafe { Box::from_raw(inner) },
            })
        }
    }

    pub fn device(&mut self) -> *mut c_void {
        unsafe { tool_device(self.inner.as_mut()) }
    }

    pub fn get_texture(&mut self, width: i32, height: i32) -> *mut c_void {
        unsafe { tool_get_texture(self.inner.as_mut(), width, height) }
    }

    pub fn get_texture_size(&mut self, texture: *mut c_void) -> (i32, i32) {
        let mut width = 0;
        let mut height = 0;
        unsafe { tool_get_texture_size(self.inner.as_mut(), texture, &mut width, &mut height) }
        (width, height)
    }
}

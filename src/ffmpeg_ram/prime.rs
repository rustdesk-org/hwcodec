use super::{ffmpeg_vaapi_prime_decode, ffmpeg_vaapi_prime_free, ffmpeg_vaapi_prime_new};
use std::os::raw::{c_int, c_void};

pub const GPU_FRAME_PRIME: i32 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PrimeFrame {
    pub kind: i32,
    pub n_fds: i32,
    pub fds: [i32; 4],
    pub fourcc: u32,
    pub modifier: u64,
    pub width: i32,
    pub height: i32,
    pub n_planes: i32,
    pub pitches: [i32; 4],
    pub offsets: [i32; 4],
    pub obj_indices: [i32; 4],
}

impl Default for PrimeFrame {
    fn default() -> Self {
        Self {
            kind: GPU_FRAME_PRIME,
            n_fds: 0,
            fds: [-1; 4],
            fourcc: 0,
            modifier: 0,
            width: 0,
            height: 0,
            n_planes: 0,
            pitches: [0; 4],
            offsets: [0; 4],
            obj_indices: [0; 4],
        }
    }
}

impl PrimeFrame {
    pub fn close_fds(&mut self) {
        for i in 0..self.n_fds.max(0) as usize {
            let fd = self.fds[i];
            if fd >= 0 {
                unsafe {
                    libc::close(fd);
                }
                self.fds[i] = -1;
            }
        }
        self.n_fds = 0;
    }
}

mod libc {
    extern "C" {
        pub fn close(fd: i32) -> i32;
    }
}

pub struct VaapiPrimeDecoder {
    ctx: *mut c_void,
    pub last: PrimeFrame,
}

unsafe impl Send for VaapiPrimeDecoder {}

impl VaapiPrimeDecoder {
    pub fn new(hevc: bool) -> Result<Self, ()> {
        let ctx = unsafe { ffmpeg_vaapi_prime_new(if hevc { 1 } else { 0 }) };
        if ctx.is_null() {
            return Err(());
        }
        Ok(Self {
            ctx,
            last: PrimeFrame::default(),
        })
    }

    pub fn decode(&mut self, data: &[u8]) -> Result<bool, ()> {
        self.last.close_fds();
        let mut out = PrimeFrame::default();
        let ok = unsafe {
            ffmpeg_vaapi_prime_decode(
                self.ctx,
                data.as_ptr(),
                data.len() as c_int,
                &mut out as *mut PrimeFrame as *mut _,
            )
        };
        if ok == 1 {
            self.last = out;
            Ok(true)
        } else {
            Ok(false)
        }
    }
}

impl Drop for VaapiPrimeDecoder {
    fn drop(&mut self) {
        self.last.close_fds();
        if !self.ctx.is_null() {
            unsafe { ffmpeg_vaapi_prime_free(self.ctx) };
            self.ctx = std::ptr::null_mut();
        }
    }
}

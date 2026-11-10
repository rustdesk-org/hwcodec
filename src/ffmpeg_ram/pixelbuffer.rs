//! macOS GPU-frame delivery: an owned `CVPixelBuffer` wrapper.
//!
//! The decoder hands out a *borrowed* descriptor for the duration of one
//! callback (see `cpp/ffmpeg_ram/ffmpeg_ram_ffi.h`). The renderer that wants to
//! keep the frame must retain it, which is what [`PixelBufferFrame`] does: it
//! owns exactly one retain and releases it on drop.
//!
//! Ownership rules (see the proposal, section "Ownership and thread handoff"):
//! - no `Copy`: a bitwise copy would duplicate ownership
//! - `Clone` retains, `Drop` releases
//! - there is no public `release()`; the only way to free is to drop
//! - moving across threads is fine: only the handle moves, the pixel data is
//!   never dereferenced here

use super::FfiPixelBufferFrame;
use std::ffi::c_void;
use std::os::raw::c_int;
use std::ptr::NonNull;

/// `kCVPixelFormatType_420YpCbCr8BiPlanarFullRange` ('420f').
pub(crate) const PIXEL_FORMAT_420F: u32 = 0x3432_3066;
/// `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange` ('420v').
pub(crate) const PIXEL_FORMAT_420V: u32 = 0x3432_3076;

/// The decoder took the frame over; it will skip the software transfer.
pub(crate) const PIXELBUFFER_ACCEPTED: c_int = 1;
/// The decoder did not keep the frame; it falls back to the software path for
/// the very same decoded frame.
pub(crate) const PIXELBUFFER_REJECTED: c_int = 0;

extern "C" {
    fn CVPixelBufferRetain(pixel_buffer: *mut c_void) -> *mut c_void;
    fn CVPixelBufferRelease(pixel_buffer: *mut c_void);
}

/// Whether a `CVPixelBuffer` pixel format can be sampled directly by the
/// renderer (the Flutter engine supports 8-bit bi-planar NV12 in both ranges;
/// P010 and everything else must go through the software path).
pub(crate) fn is_direct_deliverable(pixfmt: u32) -> bool {
    pixfmt == PIXEL_FORMAT_420F || pixfmt == PIXEL_FORMAT_420V
}

/// An owned reference to a decoded `CVPixelBuffer`.
pub struct PixelBufferFrame {
    buffer: NonNull<c_void>,
    width: i32,
    height: i32,
    pixfmt: u32,
}

impl PixelBufferFrame {
    /// Take ownership of a borrowed descriptor by retaining the buffer.
    ///
    /// Returns `None` (and retains nothing) when the descriptor holds no
    /// buffer. The caller's reference is not consumed.
    ///
    /// # Safety
    /// `frame.pixel_buffer` must be a valid `CVPixelBufferRef`.
    pub unsafe fn retain_from_borrowed(frame: &FfiPixelBufferFrame) -> Option<Self> {
        let buffer = NonNull::new(frame.pixel_buffer)?;
        let retained = CVPixelBufferRetain(buffer.as_ptr());
        NonNull::new(retained).map(|buffer| Self {
            buffer,
            width: frame.width,
            height: frame.height,
            pixfmt: frame.pixfmt,
        })
    }

    /// Borrowed view of the underlying `CVPixelBufferRef`; valid as long as
    /// `self` is alive.
    pub fn as_ptr(&self) -> *mut c_void {
        self.buffer.as_ptr()
    }

    /// Pixel width of the delivered surface.
    pub fn width(&self) -> i32 {
        self.width
    }

    /// Pixel height of the delivered surface.
    pub fn height(&self) -> i32 {
        self.height
    }

    /// CoreVideo pixel format of the surface (`PIXEL_FORMAT_420F` or `PIXEL_FORMAT_420V`).
    pub fn pixfmt(&self) -> u32 {
        self.pixfmt
    }
}

impl Clone for PixelBufferFrame {
    fn clone(&self) -> Self {
        // Clone must retain: two owned wrappers, two references.
        unsafe {
            CVPixelBufferRetain(self.buffer.as_ptr());
        }
        Self {
            buffer: self.buffer,
            width: self.width,
            height: self.height,
            pixfmt: self.pixfmt,
        }
    }
}

impl Drop for PixelBufferFrame {
    fn drop(&mut self) {
        unsafe { CVPixelBufferRelease(self.buffer.as_ptr()) }
    }
}

// SAFETY: the wrapper only owns a CoreVideo reference-counted handle. Moving it
// across threads never dereferences pixel data and performs no reads or writes;
// `CVPixelBuffer` itself supports retain/release from any thread. Serializing
// access to the pixel contents is the renderer's responsibility (the plugin
// locks its own state queue).
unsafe impl Send for PixelBufferFrame {}

/// Real CoreVideo objects for tests: the ownership contract is only meaningful
/// against an actual reference-counted `CVPixelBuffer`.
#[cfg(test)]
pub(crate) mod test_support {
    use super::*;

    extern "C" {
        fn CVPixelBufferCreate(
            allocator: *const c_void,
            width: usize,
            height: usize,
            pixel_format_type: u32,
            pixel_buffer_attributes: *const c_void,
            pixel_buffer_out: *mut *mut c_void,
        ) -> i32;
        pub fn CFGetRetainCount(cf: *const c_void) -> isize;
    }

    /// Create a real CVPixelBuffer of the requested format (null attributes).
    pub fn create_buffer(pixfmt: u32) -> Option<*mut c_void> {
        let mut raw: *mut c_void = std::ptr::null_mut();
        let status =
            unsafe { CVPixelBufferCreate(std::ptr::null(), 16, 16, pixfmt, std::ptr::null(), &mut raw) };
        if status == 0 && !raw.is_null() {
            Some(raw)
        } else {
            None
        }
    }

    /// Release the reference returned by `create_buffer`.
    pub fn release_buffer(raw: *mut c_void) {
        unsafe { CVPixelBufferRelease(raw) }
    }
}

#[cfg(test)]
mod tests {
    use super::test_support::{create_buffer, release_buffer, CFGetRetainCount};
    use super::*;

    fn descriptor(raw: *mut c_void, pixfmt: u32) -> FfiPixelBufferFrame {
        FfiPixelBufferFrame {
            pixel_buffer: raw,
            width: 16,
            height: 16,
            pixfmt,
        }
    }

    #[test]
    fn fmt_detection() {
        assert!(is_direct_deliverable(PIXEL_FORMAT_420F));
        assert!(is_direct_deliverable(PIXEL_FORMAT_420V));
        // 32BGRA and P010 must not be delivered directly.
        assert!(!is_direct_deliverable(0x4247_5241));
        assert!(!is_direct_deliverable(0x7834_3230));
        assert!(!is_direct_deliverable(0));
    }

    #[test]
    fn null_descriptor_retains_nothing() {
        let frame = FfiPixelBufferFrame {
            pixel_buffer: std::ptr::null_mut(),
            width: 0,
            height: 0,
            pixfmt: PIXEL_FORMAT_420F,
        };
        assert!(unsafe { PixelBufferFrame::retain_from_borrowed(&frame) }.is_none());
    }

    #[test]
    fn retain_clone_drop_balance() {
        let Some(raw) = create_buffer(PIXEL_FORMAT_420F) else {
            eprintln!("skip: CVPixelBufferCreate failed in this environment");
            return;
        };
        let desc = descriptor(raw, PIXEL_FORMAT_420F);

        let base = unsafe { CFGetRetainCount(raw) };
        let owned = unsafe { PixelBufferFrame::retain_from_borrowed(&desc) }.expect("owned frame");
        assert_eq!(unsafe { CFGetRetainCount(raw) }, base + 1, "retain_from_borrowed must add one reference");
        assert_eq!((owned.width(), owned.height()), (16, 16));
        assert_eq!(owned.pixfmt(), PIXEL_FORMAT_420F);

        let cloned = owned.clone();
        assert_eq!(unsafe { CFGetRetainCount(raw) }, base + 2, "clone must retain");
        drop(cloned);
        assert_eq!(unsafe { CFGetRetainCount(raw) }, base + 1, "dropping a clone releases one reference");
        drop(owned);
        assert_eq!(unsafe { CFGetRetainCount(raw) }, base, "drop must release the owned reference");

        // release the reference created by CVPixelBufferCreate itself
        release_buffer(raw);
    }
}

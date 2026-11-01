//! Requires a VAAPI encoder and a render node supporting GBM, GLES3 and DMA-BUF.
//! Run with: cargo test --features linux-dmabuf --test linux_dmabuf -- --ignored
#![cfg(all(target_os = "linux", feature = "linux-dmabuf"))]
use hwcodec::{
    common::{Quality::Quality_Default, RateControl::RC_CBR},
    ffmpeg::{AVHWDeviceType::AV_HWDEVICE_TYPE_NONE, AVPixelFormat},
    ffmpeg_ram::{
        decode::{DecodeContext, Decoder},
        encode::{DmaBufFrame, EncodeContext, Encoder},
    },
};
use std::{
    ffi::c_void,
    fs::OpenOptions,
    os::fd::{AsRawFd, FromRawFd, OwnedFd},
};

#[link(name = "gbm")]
extern "C" {
    fn gbm_create_device(fd: i32) -> *mut c_void;
    fn gbm_device_destroy(dev: *mut c_void);
    fn gbm_bo_create(dev: *mut c_void, w: u32, h: u32, format: u32, flags: u32) -> *mut c_void;
    fn gbm_bo_destroy(bo: *mut c_void);
    fn gbm_bo_map(
        bo: *mut c_void,
        x: u32,
        y: u32,
        w: u32,
        h: u32,
        flags: u32,
        stride: *mut u32,
        map_data: *mut *mut c_void,
    ) -> *mut u8;
    fn gbm_bo_unmap(bo: *mut c_void, map_data: *mut c_void);
    fn gbm_bo_get_fd(bo: *mut c_void) -> i32;
    fn gbm_bo_get_stride(bo: *mut c_void) -> u32;
    fn gbm_bo_get_modifier(bo: *mut c_void) -> u64;
}

#[test]
#[ignore = "requires real VAAPI/GBM/GLES3 hardware"]
fn dmabuf_roundtrip_preserves_colour_orientation_and_ram_fallback() {
    let node = std::env::var("DRM_RENDER_NODE").unwrap_or("/dev/dri/renderD128".into());
    let render = OpenOptions::new()
        .read(true)
        .write(true)
        .open(&node)
        .unwrap();
    unsafe {
        let dev = gbm_create_device(render.as_raw_fd());
        assert!(!dev.is_null());
        let bo = gbm_bo_create(dev, 384, 216, 0x3432_5258, 4);
        assert!(!bo.is_null());
        let mut stride = 0;
        let mut cookie = std::ptr::null_mut();
        let data = gbm_bo_map(bo, 0, 0, 384, 216, 2, &mut stride, &mut cookie);
        assert!(
            !data.is_null() && data as isize != -1,
            "GBM map: {}",
            std::io::Error::last_os_error()
        );
        let colours = [
            [0, 0, 255, 255],
            [0, 255, 0, 255],
            [255, 0, 0, 255],
            [255, 255, 255, 255],
        ];
        for y in 0..216usize {
            for x in 0..384usize {
                let quadrant = (y >= 108) as usize * 2 + (x >= 192) as usize;
                std::slice::from_raw_parts_mut(data.add(y * stride as usize + x * 4), 4)
                    .copy_from_slice(&colours[quadrant]);
            }
        }
        gbm_bo_unmap(bo, cookie);
        let fd = gbm_bo_get_fd(bo);
        assert!(fd >= 0);
        let mut frame = DmaBufFrame {
            fd: OwnedFd::from_raw_fd(fd),
            render_node: node,
            width: 384,
            height: 216,
            format: 0x3432_5258,
            num_planes: 1,
            offsets: [0; 4],
            pitches: [gbm_bo_get_stride(bo), 0, 0, 0],
            modifier: gbm_bo_get_modifier(bo),
        };
        let mut encoder = Encoder::new(EncodeContext {
            name: "h264_vaapi".into(),
            mc_name: None,
            width: 384,
            height: 216,
            pixfmt: AVPixelFormat::AV_PIX_FMT_NV12,
            align: 64,
            fps: 30,
            gop: 300,
            rc: RC_CBR,
            quality: Quality_Default,
            kbs: 8000,
            q: -1,
            thread_count: 1,
        })
        .unwrap();
        let mut decoder = Decoder::new(DecodeContext {
            name: "h264".into(),
            device_type: AV_HWDEVICE_TYPE_NONE,
            thread_count: 1,
        })
        .unwrap();
        let mut decoded = 0;
        for n in 0..12 {
            if n == 6 {
                cookie = std::ptr::null_mut();
                let data = gbm_bo_map(bo, 0, 0, 384, 216, 2, &mut stride, &mut cookie);
                assert!(
                    !data.is_null() && data as isize != -1,
                    "GBM map: {}",
                    std::io::Error::last_os_error()
                );
                for y in 0..216usize {
                    for x in 0..384usize {
                        let quadrant = (y >= 108) as usize * 2 + (x >= 192) as usize;
                        std::slice::from_raw_parts_mut(data.add(y * stride as usize + x * 4), 4)
                            .copy_from_slice(&colours[quadrant ^ 3]);
                    }
                }
                gbm_bo_unmap(bo, cookie);
            }
            let packets = encoder.encode_dmabuf(&frame, n * 33).unwrap();
            assert!(
                !packets.is_empty(),
                "low-latency encoder must produce a packet"
            );
            for packet in packets {
                for output in decoder.decode(&packet.data).unwrap() {
                    assert_eq!((output.width, output.height), (384, 216));
                    for (q, (x, y)) in [(96, 54), (288, 54), (96, 162), (288, 162)]
                        .iter()
                        .enumerate()
                    {
                        let expected = [
                            [82, 90, 240],
                            [144, 54, 34],
                            [41, 240, 110],
                            [235, 128, 128],
                        ][if n >= 6 { q ^ 3 } else { q }];
                        let luma = output.data[0][y * output.linesize[0] as usize + x];
                        let (u, v) = match output.pixfmt {
                            AVPixelFormat::AV_PIX_FMT_YUV420P => (
                                output.data[1][y / 2 * output.linesize[1] as usize + x / 2],
                                output.data[2][y / 2 * output.linesize[2] as usize + x / 2],
                            ),
                            AVPixelFormat::AV_PIX_FMT_NV12 => (
                                output.data[1][y / 2 * output.linesize[1] as usize + x],
                                output.data[1][y / 2 * output.linesize[1] as usize + x + 1],
                            ),
                        };
                        for (actual, expected) in [luma, u, v].iter().zip(expected) {
                            assert!(
                                (*actual as i32 - expected).abs() <= 6,
                                "quadrant {q}: got {luma},{u},{v}"
                            );
                        }
                    }
                    decoded += 1;
                }
            }
        }
        assert!(decoded >= 10, "decoder must receive the complete stream");
        frame.width = 386;
        assert!(encoder.encode_dmabuf(&frame, 450).is_err());
        frame.width = 384;
        assert!(!encoder.encode_dmabuf(&frame, 480).unwrap().is_empty());
        // The optional GPU input does not consume or corrupt the ordinary RAM input.
        let mut yuv = vec![128; encoder.length as usize];
        yuv[..encoder.offset[0] as usize].fill(82);
        assert!(!encoder.encode(&yuv, 500).unwrap().is_empty());
        drop(encoder);
        drop(frame);
        gbm_bo_destroy(bo);
        gbm_device_destroy(dev);
    }
}

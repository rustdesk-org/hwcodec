use super::{
    decode::Decoder, encode::Encoder, DecodeContext, DynamicContext, EncodeContext, FeatureContext,
};
use crate::common::{DataFormat, Driver, MAX_GOP};

#[test]
fn removed_sdk_backends_are_rejected() {
    for driver in [Driver::NV, Driver::AMF, Driver::MFX] {
        for data_format in [DataFormat::H264, DataFormat::H265] {
            assert!(Encoder::new(EncodeContext {
                f: FeatureContext {
                    driver: driver.clone(),
                    vendor: driver.clone(),
                    luid: 0,
                    data_format,
                },
                d: DynamicContext {
                    device: None,
                    width: 1280,
                    height: 720,
                    kbitrate: 5000,
                    framerate: 30,
                    gop: MAX_GOP as _,
                },
            })
            .is_err());
            assert!(Decoder::new(DecodeContext {
                device: None,
                driver: driver.clone(),
                vendor: driver.clone(),
                luid: 0,
                data_format,
            })
            .is_err());
        }
    }
}

#[test]
#[ignore = "requires hardware H.264/H.265 encoders and decoders"]
fn available_vram_backends_use_ffmpeg() {
    let encoders = super::encode::available(DynamicContext {
        device: None,
        width: 1280,
        height: 720,
        kbitrate: 5000,
        framerate: 30,
        gop: MAX_GOP as _,
    });
    let decoders = super::decode::available();
    assert!(!encoders.is_empty(), "no hardware encoder available");
    assert!(!decoders.is_empty(), "no hardware decoder available");
    assert!(encoders.iter().all(|e| e.driver == Driver::FFMPEG));
    assert!(decoders.iter().all(|d| d.driver == Driver::FFMPEG));
    for feature in &encoders {
        let decode_context = decoders
            .iter()
            .find(|d| d.luid == feature.luid && d.data_format == feature.data_format)
            .unwrap();
        for (width, height) in [(320, 240), (640, 360)] {
            let mut tool = tool::Tool::new(feature.luid).unwrap();
            let mut encoder = Encoder::new(EncodeContext {
                f: feature.clone(),
                d: DynamicContext {
                    device: Some(tool.device()),
                    width,
                    height,
                    kbitrate: 5000,
                    framerate: 30,
                    gop: MAX_GOP as _,
                },
            })
            .unwrap();
            let mut decoder = Decoder::new(DecodeContext {
                device: Some(tool.device()),
                ..decode_context.clone()
            })
            .unwrap();
            let texture = tool.get_texture(width, height);
            assert!(!texture.is_null());
            let mut decoded = 0;
            for index in 0..8 {
                if index == 4 {
                    encoder.set_bitrate(3000).unwrap();
                }
                let frames = encoder.encode(texture, index * 100).unwrap();
                assert!(!frames.is_empty());
                for frame in frames {
                    assert_eq!(frame.pts, index * 100);
                    for picture in decoder.decode(&frame.data).unwrap() {
                        assert!(!picture.texture.is_null());
                        assert_eq!((picture.width, picture.height), (width, height));
                        decoded += 1;
                    }
                }
            }
            assert!(decoded > 0, "encoder output did not decode to a picture");
            eprintln!("PASS FFmpeg encode/decode: {feature:?}, {width}x{height}");
        }
    }
    assert!(encoders.iter().all(|e| e.vendor != Driver::FFMPEG));
    assert!(decoders.iter().all(|d| d.vendor != Driver::FFMPEG));
    eprintln!("encoders: {encoders:?}\ndecoders: {decoders:?}");
}

#[test]
#[ignore = "requires hardware H.264/H.265 encoders"]
fn repeat_requires_successful_input_and_rejects_failed_captures() {
    let d = DynamicContext {
        device: None,
        width: 640,
        height: 360,
        kbitrate: 5000,
        framerate: 30,
        gop: MAX_GOP as _,
    };
    let features = super::encode::available(d);
    assert!(!features.is_empty(), "no hardware encoder available");
    for f in features {
        let mut tool = tool::Tool::new(f.luid).unwrap();
        let mut encoder = Encoder::new(EncodeContext {
            f,
            d: DynamicContext {
                device: Some(tool.device()),
                ..d
            },
        })
        .unwrap();
        assert!(encoder.encode_repeat(0).is_err());
        let texture = tool.get_texture(d.width, d.height);
        assert!(!texture.is_null());
        assert!(!encoder.encode(texture, 0).unwrap().is_empty());
        for ms in [100, 200] {
            let frames = encoder.encode_repeat(ms).unwrap();
            assert!(!frames.is_empty());
            assert!(frames.iter().all(|frame| frame.pts == ms));
        }

        // A null capture must fail even when a previous input is available.
        assert!(encoder.encode(std::ptr::null_mut(), 300).is_err());
        assert!(encoder.encode_repeat(400).is_err());
        assert!(!encoder.encode(texture, 500).unwrap().is_empty());
        assert!(!encoder.encode_repeat(600).unwrap().is_empty());

        // A real conversion failure also invalidates the previous input.
        let too_small = tool.get_texture(2, 2);
        assert!(!too_small.is_null());
        assert!(encoder.encode(too_small, 700).is_err());
        assert!(encoder.encode_repeat(800).is_err());
        let texture = tool.get_texture(d.width, d.height);
        assert!(!texture.is_null());
        assert!(!encoder.encode(texture, 900).unwrap().is_empty());
        drop(tool);
        let frames = encoder.encode_repeat(1000).unwrap();
        assert!(!frames.is_empty());
        assert!(frames.iter().all(|frame| frame.pts == 1000));
        eprintln!("PASS repeat entry and invalidation: {:?}", encoder.ctx.f);
    }
}

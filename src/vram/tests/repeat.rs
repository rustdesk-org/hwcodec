use crate::{
    common::MAX_GOP,
    vram::{
        decode::Decoder,
        encode::{self, EncodeFrame, Encoder},
        DecodeContext, DynamicContext, EncodeContext, FeatureContext,
    },
};

fn context() -> DynamicContext {
    DynamicContext {
        device: None,
        width: 640,
        height: 360,
        kbitrate: 5000,
        framerate: 30,
        gop: MAX_GOP as _,
    }
}

fn features() -> Vec<FeatureContext> {
    let features = encode::available(context());
    assert!(!features.is_empty(), "no hardware encoder available");
    features
}

fn decoder(tool: &mut tool::Tool, feature: &FeatureContext) -> Decoder {
    Decoder::new(DecodeContext {
        device: Some(tool.device()),
        driver: feature.driver.clone(),
        vendor: feature.vendor.clone(),
        luid: feature.luid,
        data_format: feature.data_format,
    })
    .unwrap()
}

fn check_output(frames: &[EncodeFrame], decoder: &mut Decoder, ms: i64, size: (i32, i32)) {
    assert!(!frames.is_empty(), "no output at {ms}");
    let mut decoded = 0;
    for frame in frames {
        assert_eq!(frame.pts, ms);
        assert!(!frame.data.is_empty());
        for picture in decoder.decode(&frame.data).unwrap() {
            assert!(!picture.texture.is_null());
            assert_eq!((picture.width, picture.height), size);
            decoded += 1;
        }
    }
    assert!(decoded > 0, "output at {ms} did not decode to a picture");
}

#[test]
#[ignore = "requires hardware H.264/H.265 encoders and decoders"]
fn repeat_after_bitrate_changes() {
    for feature in features() {
        let mut tool = tool::Tool::new(feature.luid).unwrap();
        let d = DynamicContext {
            device: Some(tool.device()),
            ..context()
        };
        let mut encoder = Encoder::new(EncodeContext {
            f: feature.clone(),
            d,
        })
        .unwrap();
        let mut decoder = decoder(&mut tool, &feature);
        let texture = tool.get_texture(d.width, d.height);
        assert!(!texture.is_null());
        let size = (d.width, d.height);
        check_output(encoder.encode(texture, 0).unwrap(), &mut decoder, 0, size);
        let mut ms = 100;
        for kbitrate in [3000, 8000, 1000, 5000] {
            encoder.set_bitrate(kbitrate).unwrap();
            for _ in 0..3 {
                check_output(encoder.encode_repeat(ms).unwrap(), &mut decoder, ms, size);
                ms += 100;
            }
        }
        check_output(encoder.encode(texture, ms).unwrap(), &mut decoder, ms, size);
        eprintln!("PASS repeat after bitrate changes: {feature:?}");
    }
}

#[test]
#[ignore = "requires hardware H.264/H.265 encoders and decoders"]
fn repeat_after_encoder_recreation_and_format_changes() {
    let features = features();
    for feature in &features {
        let mut tool = tool::Tool::new(feature.luid).unwrap();
        let other = features
            .iter()
            .find(|f| f.luid == feature.luid && f.data_format != feature.data_format);
        let mut phases = vec![(feature, 320, 240), (feature, 640, 360)];
        if let Some(other) = other {
            phases.push((other, 640, 360));
            phases.push((other, 320, 240));
        }
        phases.push((feature, 320, 240));
        for (index, (feature, width, height)) in phases.into_iter().enumerate() {
            let mut encoder = Encoder::new(EncodeContext {
                f: feature.clone(),
                d: DynamicContext {
                    device: Some(tool.device()),
                    width,
                    height,
                    ..context()
                },
            })
            .unwrap();
            let mut decoder = decoder(&mut tool, feature);
            let ms = index as i64 * 1000;
            assert!(encoder.encode_repeat(ms).is_err());
            let texture = tool.get_texture(width, height);
            assert!(!texture.is_null());
            check_output(
                encoder.encode(texture, ms).unwrap(),
                &mut decoder,
                ms,
                (width, height),
            );
            for ms in [ms + 100, ms + 200] {
                check_output(
                    encoder.encode_repeat(ms).unwrap(),
                    &mut decoder,
                    ms,
                    (width, height),
                );
            }
            eprintln!("PASS repeat after recreation: {feature:?}, {width}x{height}");
        }
    }
}

#[test]
#[ignore = "requires hardware H.264/H.265 encoders and decoders"]
fn repeat_normal_transitions_recover_from_failed_input_under_load() {
    for feature in features() {
        let mut tool = tool::Tool::new(feature.luid).unwrap();
        let d = DynamicContext {
            device: Some(tool.device()),
            ..context()
        };
        let mut encoder = Encoder::new(EncodeContext {
            f: feature.clone(),
            d,
        })
        .unwrap();
        let mut decoder = decoder(&mut tool, &feature);
        let size = (d.width, d.height);
        let mut ms = 0;
        // Decode after each burst so decoder work does not space out submissions.
        for cycle in 0..120 {
            if cycle % 10 == 9 {
                let too_small = tool.get_texture(2, 2);
                assert!(!too_small.is_null());
                assert!(encoder.encode(too_small, ms).is_err());
                ms += 10;
                assert!(encoder.encode_repeat(ms).is_err());
                ms += 10;
            }
            let texture = tool.get_texture(d.width, d.height);
            assert!(!texture.is_null());
            let mut batches = vec![(ms, std::mem::take(encoder.encode(texture, ms).unwrap()))];
            for _ in 0..3 {
                ms += 10;
                batches.push((ms, std::mem::take(encoder.encode_repeat(ms).unwrap())));
            }
            ms += 10;
            batches.push((ms, std::mem::take(encoder.encode(texture, ms).unwrap())));
            for (pts, frames) in batches {
                check_output(&frames, &mut decoder, pts, size);
            }
            ms += 10;
        }
        eprintln!("PASS 120 normal/repeat cycles with 12 input failures: {feature:?}");
    }
}

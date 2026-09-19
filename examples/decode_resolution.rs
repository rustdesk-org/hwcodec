//! Reuse each available decoder across resolution decreases and increases.
//! Run with `cargo run --example decode_resolution --features vram -- 100`.
//! The optional argument is the number of rounds (default: 1).

use hwcodec::{
    common::DataFormat,
    ffmpeg_ram::decode::{DecodeContext, Decoder as RamDecoder},
};
use std::{env, process::ExitCode};

struct Sample {
    width: i32,
    height: i32,
    h264: &'static [u8],
    h265: &'static [u8],
}

const SAMPLES: [Sample; 3] = [
    Sample {
        width: 640,
        height: 360,
        h264: include_bytes!("res/decode_resolution/360p.h264"),
        h265: include_bytes!("res/decode_resolution/360p.h265"),
    },
    Sample {
        width: 1280,
        height: 720,
        h264: include_bytes!("../src/res/720p.h264"),
        h265: include_bytes!("../src/res/720p.h265"),
    },
    Sample {
        width: 1920,
        height: 1080,
        h264: include_bytes!("res/decode_resolution/1080p.h264"),
        h265: include_bytes!("res/decode_resolution/1080p.h265"),
    },
];

const SEQUENCE: [usize; 4] = [1, 0, 2, 1];

enum Decoder {
    Ram(RamDecoder),
    #[cfg(all(windows, feature = "vram"))]
    Vram(hwcodec::vram::decode::Decoder),
}

impl Decoder {
    fn decode(&mut self, sample: &Sample, format: DataFormat) -> Result<(), String> {
        let packet = match format {
            DataFormat::H264 => sample.h264,
            DataFormat::H265 => sample.h265,
            _ => return Err(format!("unsupported sample format: {format:?}")),
        };
        match self {
            Self::Ram(decoder) => {
                let frames = decoder.decode(packet).map_err(|e| format!("decode: {e}"))?;
                check_dimensions(sample, frames.iter().map(|f| (f.width, f.height)))
            }
            #[cfg(all(windows, feature = "vram"))]
            Self::Vram(decoder) => {
                let frames = decoder.decode(packet).map_err(|e| format!("decode: {e}"))?;
                if frames.iter().any(|frame| frame.texture.is_null()) {
                    return Err("decoder returned a null texture".to_owned());
                }
                check_dimensions(sample, frames.iter().map(|f| (f.width, f.height)))
            }
        }
    }
}

fn check_dimensions(
    sample: &Sample,
    frames: impl Iterator<Item = (i32, i32)>,
) -> Result<(), String> {
    let mut count = 0;
    for (width, height) in frames {
        if (width, height) != (sample.width, sample.height) {
            return Err(format!(
                "expected {}x{}, got {width}x{height}",
                sample.width, sample.height
            ));
        }
        count += 1;
    }
    if count != 1 {
        return Err(format!("expected one decoded frame, got {count}"));
    }
    Ok(())
}

fn run_case(label: &str, decoder: Result<Decoder, ()>, format: DataFormat, rounds: usize) -> bool {
    let result = decoder
        .map_err(|_| "decoder creation failed".to_owned())
        .and_then(|mut decoder| {
            for round in 1..=rounds {
                for (step, &index) in SEQUENCE.iter().enumerate() {
                    let sample = &SAMPLES[index];
                    decoder.decode(sample, format).map_err(|error| {
                        format!(
                            "round {round}, step {} ({}x{}): {error}",
                            step + 1,
                            sample.width,
                            sample.height
                        )
                    })?;
                }
            }
            Ok(())
        });
    match result {
        Ok(()) => {
            println!("PASS {label}: {rounds} rounds");
            true
        }
        Err(error) => {
            eprintln!("FAIL {label}: {error}");
            false
        }
    }
}

fn main() -> ExitCode {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn")).init();
    let mut args = env::args().skip(1);
    let rounds = match args.next() {
        Some(value) => match value.parse::<usize>() {
            Ok(rounds) if rounds > 0 => rounds,
            _ => {
                eprintln!("Usage: decode_resolution [positive number of rounds]");
                return ExitCode::FAILURE;
            }
        },
        None => 1,
    };
    if args.next().is_some() {
        eprintln!("Usage: decode_resolution [positive number of rounds]");
        return ExitCode::FAILURE;
    }

    println!("One decoder per case, {rounds} rounds, no reset between frames.");
    println!("Sequence: 1280x720 -> 640x360 -> 1920x1080 -> 1280x720");
    let mut total = 0;
    let mut passed = 0;
    for codec in RamDecoder::available_decoders() {
        let label = format!("RAM {} {:?}", codec.name, codec.hwdevice);
        let context = DecodeContext {
            name: codec.name,
            device_type: codec.hwdevice,
            thread_count: 1,
        };
        total += 1;
        passed += usize::from(run_case(
            &label,
            RamDecoder::new(context).map(Decoder::Ram),
            codec.format,
            rounds,
        ));
    }
    #[cfg(all(windows, feature = "vram"))]
    for context in hwcodec::vram::decode::available() {
        let label = format!(
            "VRAM {:?} {:?} LUID={} {:?}",
            context.driver, context.vendor, context.luid, context.data_format
        );
        let format = context.data_format;
        total += 1;
        passed += usize::from(run_case(
            &label,
            hwcodec::vram::decode::Decoder::new(context).map(Decoder::Vram),
            format,
            rounds,
        ));
    }
    #[cfg(all(windows, not(feature = "vram")))]
    println!("VRAM enumeration requires --features vram.");

    println!("Result: {passed}/{total} decoder configurations passed.");
    if total > 0 && passed == total {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

use hwcodec::{
    decode::{DecodeContext, Decoder},
    encode::{EncodeContext, Encoder},
    ffmpeg::{ffmpeg_linesize_offset_length, AVHWDeviceType::*, AVPixelFormat::*},
};
use log::info;
use std::{
    fs::File,
    io::{Read, Write},
};

fn main() {
    let encode_ctx = EncodeContext {
        name: String::from("h264_amf"),
        fps: 30,
        width: 1920,
        height: 1080,
        pixfmt: AV_PIX_FMT_YUV420P,
        align: 0,
    };
    let decode_ctx = DecodeContext {
        name: String::from("h264"),
        device_type: AV_HWDEVICE_TYPE_D3D11VA,
    };
    let _ = std::thread::spawn(move || test_encode_decode(encode_ctx, decode_ctx)).join();
}

fn test_encode_decode(encode_ctx: EncodeContext, decode_ctx: DecodeContext) {
    let mut yuv_file = File::open("input/1920_1080.yuv").unwrap();
    let mut encode_file = File::create("output/1920_1080.264").unwrap();
    let mut decode_file = File::create("output/1920_1080_decode.yuv").unwrap();
    let size: usize;
    if let Ok((_, _, len)) = ffmpeg_linesize_offset_length(
        encode_ctx.pixfmt,
        encode_ctx.width as _,
        encode_ctx.height as _,
        encode_ctx.align as _,
    ) {
        size = len as _;
    } else {
        return;
    }

    let mut video_encoder = Encoder::new(encode_ctx).unwrap();
    let mut video_decoder = Decoder::new(decode_ctx).unwrap();

    let mut buf = vec![0; size + 64];
    let mut encode_sum = 0;
    let mut decode_sum = 0;
    let mut counter = 0;

    let mut f = |data: &[u8]| {
        let now = std::time::Instant::now();
        if let Ok(encode_frames) = video_encoder.encode(data) {
            info!("encode:{:?}", now.elapsed());
            encode_sum += now.elapsed().as_micros();
            for encode_frame in encode_frames.iter() {
                encode_file.write_all(&encode_frame.data).unwrap();
                encode_file.flush().unwrap();

                let now = std::time::Instant::now();
                if let Ok(docode_frames) = video_decoder.decode(&encode_frame.data) {
                    info!("decode:{:?}", now.elapsed());
                    decode_sum += now.elapsed().as_micros();
                    counter += 1;
                    for decode_frame in docode_frames {
                        info!("decode_frame:{}", decode_frame);
                        for data in decode_frame.data.iter() {
                            decode_file.write_all(data).unwrap();
                            decode_file.flush().unwrap();
                        }
                    }
                }
            }
        }
    };

    loop {
        match yuv_file.read(&mut buf[..size]) {
            Ok(n) => {
                if n > 0 {
                    f(&buf[..n]);
                } else {
                    break;
                }
            }
            Err(e) => {
                println!("{:?}", e);
                break;
            }
        }
    }
    info!(
        "counter:{}, encode_sum:{}us, decode_sum:{}us, encode_avg:{}us, decode_avg:{}us",
        counter,
        encode_sum,
        decode_sum,
        encode_sum / counter,
        decode_sum / counter
    );
}

use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    decode::{DecodeContext, Decoder},
    encode::{EncodeContext, Encoder},
    ffmpeg::ffmpeg_linesize_offset_length,
    ffmpeg::AVHWDeviceType,
    AVPixelFormat::*,
    Quality::*,
    RateContorl::*,
};
use std::{
    fs::File,
    io::{Read, Write},
};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));

    let ctx = EncodeContext {
        name: String::from("h264_amf"),
        width: 1920,
        height: 1080,
        pixfmt: AV_PIX_FMT_YUV420P,
        align: 0,
        bitrate: 2_000_000,
        timebase: [1, 30],
        gop: 60,
        quality: Quality_Default,
        rc: RC_DEFAULT,
    };
    let yuvs = prepare_yuv(ctx.clone(), "input/1920_1080.yuv").unwrap();
    let mut encoder = Encoder::new(ctx).unwrap();
    bitrate(&mut encoder, &yuvs);
}

fn bitrate(encoder: &mut Encoder, yuvs: &Vec<Vec<u8>>) {
    let arr = [
        -1, 1000, 8000, 200_000, 1_000_000, 1_320_000, 2_000_000, 4_000_000,
    ];
    for v in arr.iter() {
        encoder.set_bitrate(v.clone());
        let (time, size, _encode_filename) =
            encode_decode("bitrate", format!("{}", v).as_str(), encoder, yuvs);
        log::info!("bitrate:{}, {} us, {} byte", v, time, size);
    }
}

fn encode_decode(
    para_name: &str,
    para_value: &str,
    encoder: &mut Encoder,
    yuvs: &Vec<Vec<u8>>,
) -> (usize, usize, String) {
    let dir = format!("output/{}/", para_name);
    std::fs::create_dir_all(&dir).unwrap();
    let mut filename = format!("{}/{}.", &dir, para_value);
    if encoder.ctx.name.contains("264") {
        filename += "264";
    } else {
        filename += "265";
    }

    let decode_ctx = DecodeContext {
        name: String::from("h264"),
        device_type: AVHWDeviceType::AV_HWDEVICE_TYPE_D3D11VA,
    };
    let mut video_decoder = Decoder::new(decode_ctx).unwrap();
    let mut decode_file = File::create(format!("output/{}/{}.yuv", para_name, para_value)).unwrap();

    let mut file = File::create(&filename).unwrap();
    let start = std::time::Instant::now();
    let mut size = 0;
    for yuv in yuvs.iter() {
        let h26xs = encoder.encode(yuv).unwrap();
        for h26x in h26xs.iter() {
            size += h26x.data.len();
            file.write_all(&h26x.data).unwrap();
            file.flush().unwrap();

            if let Ok(docode_frames) = video_decoder.decode(&h26x.data) {
                for decode_frame in docode_frames {
                    for data in decode_frame.data.iter() {
                        decode_file.write_all(data).unwrap();
                        decode_file.flush().unwrap();
                    }
                }
            }
        }
    }
    (
        start.elapsed().as_micros() as usize / yuvs.len(),
        size / yuvs.len(),
        filename,
    )
}

fn prepare_yuv(ctx: EncodeContext, filename: &str) -> Result<Vec<Vec<u8>>, ()> {
    let mut yuv_file = File::open(filename).unwrap();
    let size: usize;
    if let Ok((_, _, len)) =
        ffmpeg_linesize_offset_length(ctx.pixfmt, ctx.width as _, ctx.height as _, ctx.align as _)
    {
        size = len as _;
    } else {
        return Err(());
    }
    let mut yuv = vec![0; size];
    let mut yuvs = vec![];
    loop {
        match yuv_file.read(&mut yuv[..size]) {
            Ok(n) => {
                if n == size {
                    yuvs.push(yuv.to_vec());
                } else if n == 0 {
                    break;
                } else {
                    return Err(());
                }
            }
            Err(e) => {
                log::info!("{:?}", e);
                return Err(());
            }
        }
    }
    Ok(yuvs)
}

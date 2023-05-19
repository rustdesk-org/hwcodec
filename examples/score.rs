use std::{cell::Cell, fmt::Display, fs::File, io::Read};

use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    decode::{DecodeContext, Decoder},
    encode::{EncodeContext, EncodeFrame, Encoder},
    ffmpeg::*,
    AVPixelFormat,
    Quality::*,
    RateControl::*,
};

#[derive(Debug)]
struct EncoderStat {
    name: String,
    pixfmt: AVPixelFormat,
    us: usize,
    size: usize,
}

impl Display for EncoderStat {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Encoder {}, {:?}, { }us, {} byte",
            self.name, self.pixfmt, self.us, self.size
        )
    }
}

struct DecoderStat {
    name: String,
    hwdevice: AVHWDeviceType,
    us: usize,
}

impl Display for DecoderStat {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Decoder {}, {:?}, { }us, ",
            self.name, self.hwdevice, self.us,
        )
    }
}

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));

    let mut stat_encoder = Cell::new(vec![]);
    let mut stat_decoder = Cell::new(vec![]);

    log::info!("NV12");
    test(
        AVPixelFormat::AV_PIX_FMT_NV12,
        &mut stat_encoder,
        &mut stat_decoder,
    );
    log::info!("YUV420");
    test(
        AVPixelFormat::AV_PIX_FMT_YUV420P,
        &mut stat_encoder,
        &mut stat_decoder,
    );
    log::info!("SCORE");
    encode_score(stat_encoder.take());
    decode_score(stat_decoder.take());
}

fn encode_score(stats: Vec<EncoderStat>) {
    let mut avg_us = 0;
    let mut avg_size = 0;

    for stat in stats.iter() {
        avg_us += stat.us;
        avg_size += stat.size;
    }
    avg_us /= stats.len();
    avg_size /= stats.len();
    for stat in stats.iter() {
        let score_speed = 100 * avg_us / stat.us;
        let score_size = 100 * avg_size / stat.size;
        let score_quality = if stat.name.contains("hevc") { 100 } else { 80 };
        let score = (score_speed * 5 + score_size * 2 + score_quality * 3) / 10;
        log::info!("{}, score:{}", stat, score);
    }
}

fn decode_score(stats: Vec<DecoderStat>) {
    let mut avg_us = 0;

    for stat in stats.iter() {
        avg_us += stat.us;
    }
    avg_us /= stats.len();
    for stat in stats.iter() {
        let score_speed = 100 * avg_us / stat.us;
        let score = score_speed;
        log::info!("{}, score:{}", stat, score);
    }
}

fn test(
    pixfmt: AVPixelFormat,
    stat_encoder: &mut Cell<Vec<EncoderStat>>,
    stat_decoder: &mut Cell<Vec<DecoderStat>>,
) {
    let ctx = EncodeContext {
        name: String::from(""),
        width: 1920,
        height: 1080,
        pixfmt,
        align: 0,
        bitrate: 20000000,
        timebase: [1, 30],
        gop: 60,
        quality: Quality_Default,
        rc: RC_DEFAULT,
    };
    let yuvs = prepare_yuv(ctx.clone(), "input/1920_1080.yuv").unwrap();

    let encoder_infos = Encoder::available_encoders(ctx.clone());
    for info in encoder_infos {
        let mut encode = Encoder::new(EncodeContext {
            name: info.name.clone(),
            ..ctx.clone()
        })
        .unwrap();
        let start = std::time::Instant::now();
        let mut size = 0;
        for yuv in yuvs.iter() {
            let h26xs = encode.encode(yuv).unwrap();
            for h26x in h26xs.iter() {
                size += h26x.data.len();
            }
        }
        log::info!(
            "encode:{:?}, time_avg:{}us, size_avg:{}byte",
            info.name,
            start.elapsed().as_micros() as usize / yuvs.len(),
            size / yuvs.len()
        );
        stat_encoder.get_mut().push(EncoderStat {
            pixfmt,
            name: info.name,
            us: start.elapsed().as_micros() as usize / yuvs.len(),
            size: size / yuvs.len(),
        })
    }

    let (data_h264, data_h265) = prepare_h26x(ctx, &yuvs);
    let decoder_infos = Decoder::available_decoders();
    for info in decoder_infos.iter() {
        let decode_ctx = DecodeContext {
            name: info.name.clone(),
            device_type: info.hwdevice,
        };
        let start = std::time::Instant::now();
        let mut decoded = false;
        match info.format {
            hwcodec::ffmpeg::DataFormat::H264 => {
                if data_h264.is_some() {
                    decoded = true;
                    let mut decode = Decoder::new(decode_ctx).unwrap();
                    for h264 in data_h264.as_ref().unwrap().iter() {
                        decode.decode(&h264.data).unwrap();
                    }
                }
            }
            hwcodec::ffmpeg::DataFormat::H265 => {
                if data_h265.is_some() {
                    decoded = true;
                    let mut decode = Decoder::new(decode_ctx).unwrap();
                    for h265 in data_h265.as_ref().unwrap().iter() {
                        decode.decode(&h265.data).unwrap();
                    }
                }
            }
        }
        if decoded {
            log::info!(
                "decode:{} {:?}, time_avg:{}us",
                info.name,
                info.hwdevice,
                start.elapsed().as_micros() as usize / yuvs.len()
            );
            stat_decoder.get_mut().push(DecoderStat {
                name: info.name.clone(),
                hwdevice: info.hwdevice,
                us: start.elapsed().as_micros() as usize / yuvs.len(),
            })
        }
    }
}

fn prepare_h26x(
    ctx: EncodeContext,
    yuvs: &Vec<Vec<u8>>,
) -> (Option<Vec<EncodeFrame>>, Option<Vec<EncodeFrame>>) {
    let best = CodecInfo::score(Encoder::available_encoders(ctx.clone()));

    let f = |h26x: Option<CodecInfo>| {
        if let Some(h26x) = h26x {
            let mut encode = Encoder::new(EncodeContext {
                name: h26x.name.clone(),
                ..ctx.clone()
            })
            .unwrap();
            let mut h26xs = vec![];
            for yuv in yuvs.iter() {
                if let Ok(hdata) = encode.encode(yuv) {
                    h26xs.append(hdata);
                } else {
                    log::error!("encode failed");
                }
            }
            Some(h26xs)
        } else {
            None
        }
    };
    let data_h264: Option<Vec<EncodeFrame>> = f(best.h264);
    let data_h265: Option<Vec<EncodeFrame>> = f(best.h265);

    (data_h264, data_h265)
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

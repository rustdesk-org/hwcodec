use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    ffmpeg::AVPixelFormat,
    ffmpeg_ram::{
        decode::Decoder,
        encode::{EncodeContext, Encoder},
        CodecInfo,
        Quality::*,
        RateControl::*,
    },
};
use std::time::Instant;

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));

    let ctx = EncodeContext {
        name: String::from(""),
        mc_name: None,
        width: 1280,
        height: 720,
        pixfmt: AVPixelFormat::AV_PIX_FMT_NV12,
        align: 0,
        bitrate: 1000000,
        timebase: [1, 30],
        gop: 60,
        quality: Quality_Default,
        rc: RC_DEFAULT,
        thread_count: 4,
    };
    let start = Instant::now();
    let encoders = Encoder::available_encoders(ctx.clone(), None);
    log::info!("available_encoders:{:?}", start.elapsed());
    log::info!("count:{}, {:?}", encoders.len(), encoders);
    log::info!("best encoders:{:?}", CodecInfo::prioritized(encoders));

    let start = Instant::now();
    let decoders = Decoder::available_decoders(None);
    log::info!("available_decoders:{:?}", start.elapsed());
    log::info!("count:{}, {:?}", decoders.len(), decoders);
    log::info!(
        "best decoders:{:?}",
        CodecInfo::prioritized(decoders.clone())
    );
    log::info!("soft decoders:{:?}", CodecInfo::soft());
}

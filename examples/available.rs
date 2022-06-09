use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    decode::Decoder,
    encode::{EncodeContext, Encoder},
    AVPixelFormat,
    Quality::*,
    RateContorl::*,
};
use std::time::Instant;

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "debug"));

    let ctx = EncodeContext {
        name: String::from(""),
        width: 1920,
        height: 1080,
        pixfmt: AVPixelFormat::AV_PIX_FMT_YUV420P,
        align: 0,
        bitrate: 20000000,
        timebase: [1, 30],
        gop: 60,
        quality: Quality_Default,
        rc: RC_DEFAULT,
    };
    let start = Instant::now();
    let encoders = Encoder::avaliable_encoders(ctx.clone());
    log::info!("avaliable_encoders:{:?}", start.elapsed());
    log::info!("count:{}, {:?}", encoders.len(), encoders);

    let start = Instant::now();
    let decoders = Decoder::avaliable_decoders();
    log::info!("avaliable_decoders:{:?}", start.elapsed());
    log::info!("count:{}, {:?}", decoders.len(), decoders);
}

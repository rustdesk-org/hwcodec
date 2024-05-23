use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    common::DataFormat,
    ffmpeg::AVPixelFormat::*,
    ffmpeg_ram::{
        decode::{DecodeContext, Decoder},
        encode::{EncodeContext, Encoder},
        ffmpeg_linesize_offset_length, CodecInfo,
        Quality::*,
        RateControl::*,
    },
};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));
    setup_ram();
}

fn setup_ram() {
    let encoders = Encoder::available_encoders(
        EncodeContext {
            name: String::from(""),
            mc_name: None,
            width: 1920,
            height: 1080,
            pixfmt: AV_PIX_FMT_NV12,
            align: 0,
            kbs: 0,
            timebase: [1, 30],
            gop: 60,
            quality: Quality_Default,
            rc: RC_CBR,
            thread_count: 1,
        },
        None,
    );
    let decoders = Decoder::available_decoders(None);
    let h264_encoders = encoders
        .iter()
        .filter(|info| info.name.contains("h264"))
        .cloned()
        .collect::<Vec<_>>();
    let h265_encoders = encoders
        .iter()
        .filter(|info| info.name.contains("hevc"))
        .cloned()
        .collect::<Vec<_>>();
    let h264_decoders = decoders
        .iter()
        .filter(|info| info.format == DataFormat::H264)
        .cloned()
        .collect::<Vec<_>>();
    let h265_decoders = decoders
        .iter()
        .filter(|info| info.format == DataFormat::H265)
        .cloned()
        .collect::<Vec<_>>();

    let start_width = 1920;
    let start_height = 1080;
    let max_align = 16;
    let step = 2;

    for width in (start_width..=(start_width + max_align)).step_by(step) {
        for height in (start_height..=(start_height + max_align)).step_by(step) {
            for encode_info in &h264_encoders {
                test_ram(width, height, encode_info.clone(), h264_decoders[0].clone());
            }
            for decode_info in &h264_decoders {
                test_ram(width, height, h264_encoders[0].clone(), decode_info.clone());
            }
            for encode_info in &h265_encoders {
                test_ram(width, height, encode_info.clone(), h265_decoders[0].clone());
            }
            for decode_info in &h265_decoders {
                test_ram(width, height, h265_encoders[0].clone(), decode_info.clone());
            }
        }
    }
}

fn test_ram(width: i32, height: i32, encode_info: CodecInfo, decode_info: CodecInfo) {
    println!(
        "Test {}x{}: {} -> {}",
        width, height, encode_info.name, decode_info.name
    );
    let encode_ctx = EncodeContext {
        name: encode_info.name.clone(),
        mc_name: None,
        width,
        height,
        pixfmt: AV_PIX_FMT_NV12,
        align: 0,
        kbs: 0,
        timebase: [1, 30],
        gop: 60,
        quality: Quality_Default,
        rc: RC_CBR,
        thread_count: 1,
    };
    let decode_ctx = DecodeContext {
        name: decode_info.name.clone(),
        device_type: decode_info.hwdevice,
        thread_count: 4,
    };
    let (_, _, len) = ffmpeg_linesize_offset_length(
        encode_ctx.pixfmt,
        encode_ctx.width as _,
        encode_ctx.height as _,
        encode_ctx.align as _,
    )
    .unwrap();
    let mut video_encoder = Encoder::new(encode_ctx).unwrap();
    let mut video_decoder = Decoder::new(decode_ctx).unwrap();
    let buf: Vec<u8> = vec![0; len as usize];
    let encode_frames = video_encoder.encode(&buf).unwrap();
    assert_eq!(encode_frames.len(), 1);
    let docode_frames = video_decoder.decode(&encode_frames[0].data).unwrap();
    assert_eq!(docode_frames.len(), 1);
    assert_eq!(docode_frames[0].width, width);
    assert_eq!(docode_frames[0].height, height);
    println!(
        "Pass {}x{}: {} -> {} {:?}",
        width, height, encode_info.name, decode_info.name, decode_info.hwdevice
    )
}

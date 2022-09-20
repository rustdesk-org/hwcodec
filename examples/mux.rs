use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    encode::{EncodeContext, EncodeFrame, Encoder},
    ffmpeg::{ffmpeg_linesize_offset_length, CodecInfo},
    mux::{MuxContext, Muxer},
    AVPixelFormat,
    Quality::*,
    RateContorl::*,
};
use std::{fs::File, io::Read, time::Instant};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));

    let mut muxer = Muxer::new(MuxContext {
        filename: "output/mux.flv".to_owned(),
        width: 1920,
        height: 1080,
        is265: false,
        framerate: 30,
    })
    .unwrap();

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

    let yuvs = prepare_yuv(ctx.clone(), "input/1920_1080.yuv").unwrap();
    if let (Some(h264s), _h265s) = prepare_h26x(ctx, &yuvs) {
        log::info!("yuv len:{}", yuvs.len());
        let mut cnt = 0;
        let start = Instant::now();
        for h264 in h264s {
            muxer
                .write_video(&h264.data, start.elapsed().as_millis() as _)
                .unwrap();
            std::thread::sleep(std::time::Duration::from_millis(50));
            cnt = cnt + 1;
            log::info!("cnt:{}", cnt);
        }
        muxer.write_tail().unwrap();
        log::info!("end elapsed:{:?}", start.elapsed()); // equal with video time
    }
}

fn prepare_h26x(
    ctx: EncodeContext,
    yuvs: &Vec<Vec<u8>>,
) -> (Option<Vec<EncodeFrame>>, Option<Vec<EncodeFrame>>) {
    let best = CodecInfo::score(Encoder::avaliable_encoders(ctx.clone()));

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

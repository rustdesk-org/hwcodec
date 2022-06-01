use hwcodec::{
    encode::{EncodeContext, Encoder},
    ffmpeg::ffmpeg_linesize_offset_length,
    AVPixelFormat::*,
    Quality::*,
    RateContorl::*,
};
use std::{
    fs::File,
    io::{Read, Write},
    process::Command,
};

const PSNR: bool = false;

fn main() {
    let ctx = EncodeContext {
        name: String::from("hevc_amf"),
        width: 1920,
        height: 1080,
        pixfmt: AV_PIX_FMT_YUV420P,
        align: 0,
        bitrate: 2_000_000,
        timebase: [1, 30],
        gop: 60,
        quality: Quality_High,
        rc: RC_DEFAULT,
    };
    let yuvs = prepare_yuv(ctx.clone(), "input/1920_1080.yuv").unwrap();
    mixed(ctx.clone(), &yuvs);
    bitrate(ctx.clone(), &yuvs);
    timebase(ctx.clone(), &yuvs);
    gop(ctx.clone(), &yuvs);
    quality(ctx.clone(), &yuvs);
    rate_control(ctx.clone(), &yuvs);
}

fn mixed(mut ctx: EncodeContext, yuvs: &Vec<Vec<u8>>) {
    let arr = [
        (4_000_000, 30),
        (2_000_000, 30),
        (1_000_000, 30),
        (1_500_000, 60),
        (1_000_000, 60),
        (1_000_000, 100),
        (1_000_000, 1000),
    ];
    for v in arr.iter() {
        ctx.bitrate = v.0.clone();
        ctx.timebase[1] = v.1.clone();
        let (time, size, encode_filename) =
            encode("mixed", format!("{:?}", v).as_str(), ctx.clone(), yuvs);
        psnr(encode_filename);
        println!(
            "bitrate:{}, timebase:{}, {} us, {} byte",
            v.0, v.1, time, size
        );
    }
}

fn bitrate(mut ctx: EncodeContext, yuvs: &Vec<Vec<u8>>) {
    let arr = [
        -1, 1000, 2000, 5000, 8000, 10000, 100000, 1000000, 1500000, 2000000, 4000000, 10000000,
    ];
    for v in arr.iter() {
        ctx.bitrate = v.clone();
        let (time, size, encode_filename) =
            encode("bitrate", format!("{}", v).as_str(), ctx.clone(), yuvs);
        psnr(encode_filename);
        println!("bitrate:{}, {} us, {} byte", v, time, size);
    }
}

fn timebase(mut ctx: EncodeContext, yuvs: &Vec<Vec<u8>>) {
    let arr = [1, 10, 50, 100, 200, 500, 800, 1000, 2000];
    for v in arr.iter() {
        ctx.timebase[1] = v.clone();
        let (time, size, encode_filename) =
            encode("timebase", format!("{}", v).as_str(), ctx.clone(), yuvs);
        psnr(encode_filename);
        println!("timebase:{}, {} us, {} byte", v, time, size);
    }
}

fn gop(mut ctx: EncodeContext, yuvs: &Vec<Vec<u8>>) {
    let arr = [1, 5, 10, 20, 50, 100, 200, 400, 100000];
    for v in arr.iter() {
        ctx.gop = v.clone();
        let (time, size, encode_filename) =
            encode("gop", format!("{}", v).as_str(), ctx.clone(), yuvs);
        psnr(encode_filename);
        println!("gop:{}, {} us, {} byte", v, time, size);
    }
}

fn quality(mut ctx: EncodeContext, yuvs: &Vec<Vec<u8>>) {
    let arr = [Quality_Default, Quality_High, Quality_Medium, Quality_Low];
    for v in arr.iter() {
        ctx.quality = v.clone();
        let (time, size, encode_filename) =
            encode("quality", format!("{:?}", v).as_str(), ctx.clone(), yuvs);
        psnr(encode_filename);
        println!("quality:{:?}, {} us, {} byte", v, time, size);
    }
}

fn rate_control(mut ctx: EncodeContext, yuvs: &Vec<Vec<u8>>) {
    let arr = [RC_CBR, RC_VBR];
    for v in arr.iter() {
        ctx.rc = v.clone();
        let (time, size, encode_filename) = encode(
            "rate_control",
            format!("{:?}", v).as_str(),
            ctx.clone(),
            yuvs,
        );
        psnr(encode_filename);
        println!("rate_control:{:?}, {} us, {} byte", v, time, size);
    }
}

fn psnr(encode_filename: String) {
    if PSNR {
        let psnr_filename = format!("{}.psnr.txt", encode_filename);
        let prsn_result_file = format!("{}.psnr_result.txt", encode_filename);
        Command::new("cmd")
            .arg("/c")
            .arg(format!(
            "ffmpeg.exe -i {} -i input/1920_1080.264 -lavfi psnr=stats_file={} -f null - >{} 2>&1",
            encode_filename, psnr_filename, prsn_result_file
        ))
            .spawn()
            .expect("ffmpeg psnr");
    }
}

fn encode(
    para_name: &str,
    para_value: &str,
    ctx: EncodeContext,
    yuvs: &Vec<Vec<u8>>,
) -> (usize, usize, String) {
    let dir = format!("output/{}/", para_name);
    std::fs::create_dir_all(&dir).unwrap();
    let mut filename = format!("{}/{}.", &dir, para_value);
    if ctx.name.contains("264") {
        filename += "264";
    } else {
        filename += "265";
    }
    let mut file = File::create(&filename).unwrap();
    let mut encoder = Encoder::new(ctx).unwrap();
    let start = std::time::Instant::now();
    let mut size = 0;
    for yuv in yuvs.iter() {
        let h26xs = encoder.encode(yuv).unwrap();
        for h26x in h26xs.iter() {
            size += h26x.data.len();
            file.write_all(&h26x.data).unwrap();
            file.flush().unwrap();
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
                println!("{:?}", e);
                return Err(());
            }
        }
    }
    Ok(yuvs)
}

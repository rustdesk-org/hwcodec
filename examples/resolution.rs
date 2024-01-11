use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hwcodec::{
    ff1::decode::{DecodeContext, Decoder},
    ffmpeg::AVHWDeviceType::*,
};
use std::{fs::File, io::Read};

fn main() {
    let gpu = true;
    let h264 = true;
    let hw_type = if gpu { "gpu" } else { "hw" };
    let file_type = if h264 { "h264" } else { "h265" };
    let codec = if h264 { "h264" } else { "hevc" };

    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));
    let decode_ctx = DecodeContext {
        name: String::from(codec),
        device_type: AV_HWDEVICE_TYPE_D3D11VA,
        thread_count: 4,
    };
    let mut video_decoder = Decoder::new(decode_ctx).unwrap();

    decode(
        &mut video_decoder,
        0,
        &format!("input/data_and_line/{}_1600_900.{}", hw_type, file_type),
        &format!("input/data_and_line/{}_1600_900_{}.txt", hw_type, file_type),
    );

    decode(
        &mut video_decoder,
        1,
        &format!("input/data_and_line/{}_1440_900.{}", hw_type, file_type),
        &format!("input/data_and_line/{}_1440_900_{}.txt", hw_type, file_type),
    );
}

fn decode(video_decoder: &mut Decoder, index: usize, filename: &str, len_filename: &str) {
    let mut file_lens = File::open(len_filename).unwrap();
    let mut file = File::open(filename).unwrap();
    let mut file_lens_buf = Vec::new();
    file_lens.read_to_end(&mut file_lens_buf).unwrap();
    let file_lens_str = String::from_utf8_lossy(&file_lens_buf).to_string();
    let lens: Vec<usize> = file_lens_str
        .split(",")
        .filter(|e| !e.is_empty())
        .map(|e| e.parse().unwrap())
        .collect();
    for i in 0..lens.len() {
        let mut buf = vec![0; lens[i]];
        file.read(&mut buf).unwrap();
        let frames = video_decoder.decode(&buf).unwrap();
        println!(
            "file{}, w:{}, h:{}",
            index, frames[0].width, frames[0].height
        );
    }
}

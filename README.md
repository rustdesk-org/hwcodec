# A real-time hardware codec library for [Rustdesk](https://github.com/rustdesk/rustdesk) based on FFmpeg

### Platforms

- [x] Windows
- [x] Linux
- [ ] macOS
- [ ] iOS
- [ ] android

### Codec

|      | encoder                        | decoder        |
| ---- | ------------------------------ | -------------- |
| h264 | h264_nvenc, h264_amf, h264_qsv | h264, h264_qsv |
| h265 | hevc_nvenc, hevc_amf, hevc_qsv | hevc, hevc_qsv |

### Features

* Support Nvidia, AMD graphics cards (Intel partial)
* All codecs are latency-free


### Start

#### Run in Rustdesk
`cargo run --features hwcodec`

### Notice

#### "Unable to update https://github.com/21pages/hwcodec#890204e0"

`cargo update -p hwcodec`

#### Drive installation

For linux, it may be necessary to install the driver. See compile [doc](https://github.com/21pages/hwcodec/tree/main/doc)

#### Library dependency

see [build.rs](https://github.com/21pages/hwcodec/blob/main/build.rs)

#### compile it yourself

see [doc](https://github.com/21pages/hwcodec/tree/main/doc)

### TODO
- [ ] Scoring and automatic selection of codecs
- [ ] Support for more platforms





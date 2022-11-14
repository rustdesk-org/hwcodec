# A real-time hardware codec library for [RustDesk](https://github.com/rustdesk/rustdesk) based on FFmpeg

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

#### "Unable to update https://github.com/21pages/hwcodec#xxxxxx"

`cargo update -p hwcodec`

#### Driver installation

##### Nvidia
[drivers]((https://developer.nvidia.com/nvidia-video-codec-sdk/download)) or install by `Software&Update/Additional Drivers`

##### AMD
* Install the drive tool according to [official document](https://amdgpu-install.readthedocs.io/en/latest/install-prereq.html#downloading-the-installer-package)，install `amdgpu-install` of your own system.
* **disable desktop (ref `NOTICE 2`)** , Run：`amdgpu-install -y --usecase=amf`
* If the driver is not installed properly, an error may be reported：`DLL libamfrt64.so.1 failed to open`.



#### Library dependency

##### linux
`sudo apt-get install libva-dev libvdpau-dev`


### TODO
- [ ] Support qsv in RustDesk
- [ ] Scoring and automatic selection of codecs
- [ ] Support for more platforms





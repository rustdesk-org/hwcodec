# A real-time hardware codec library for [RustDesk](https://github.com/rustdesk/rustdesk) based on FFmpeg


## Codec

### Windows

| GPU           | FFmpeg ram | FFmpeg vram | sdk vram |
| ------------- | ---------- | ----------- | -------- |
| intel encode  | qsv, mf    | qsv         | Y        |
| intel decode  | d3d11      | d3d11       | Y        |
| nvidia encode | mf         | nvenc       | Y        |
| nvidia decode | d3d11      | amf         | N        |
| amd encode    | amf        | amf         | Y        |
| amd decode    | d3d11      | d3d11       | Y        |


### Linux

| GPU           | FFmpeg ram |
| ------------- | ---------- |
| intel encode  | vaapi      |
| intel decode  | vaapi      |
| nvidia encode | nvnec      |
| nvidia decode | nvdec      |
| amd encode    | amf        |
| amd decode    | vaapi      |

## System requirements

* intel

  Windows Intel(r) graphics driver since 27.20.100.8935 version. 

  [Hardware Platforms Supported by the Intel(R) Media SDK GPU Runtime](https://www.intel.com/content/www/us/en/docs/onevpl/upgrade-from-msdk/2023-1/onevpl-hardware-support-details.html#HARDWARE-PLATFORMS-SUPPORTED-BY-THE-INTEL-R-MEDIA-SDK-GPU-RUNTIME)

* AMD

  AMD Radeon Software Adrenalin Edition 23.1.2 (22.40.01.34) or newer

  https://github.com/GPUOpen-LibrariesAndSDKs/AMF

* nvidia

  Windows: Driver version 471.41 or higher

  https://docs.nvidia.com/video-technologies/video-codec-sdk/11.1/read-me/index.html

  https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new?ncid=em-prod-816193


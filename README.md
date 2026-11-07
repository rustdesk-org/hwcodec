# A real-time hardware codec library for [RustDesk](https://github.com/rustdesk/rustdesk) based on FFmpeg


## Codec

### Windows

| GPU           | FFmpeg ram        | FFmpeg vram |
| ------------- | ----------------  | ----------- |
| intel encode  | qsv               | qsv |
| intel decode  | d3d11             | d3d11 |
| nvidia encode | nvenc(nv12->d3d11)| nvenc(d3d11) |
| nvidia decode | d3d11             | d3d11 |
| amd encode    | amf               | amf |
| amd decode    | d3d11             | d3d11 |

#### Notes

* The reason for discarding the codecs using Cucontext is discussed in the following forum thread: https://forums.developer.nvidia.com/t/cuctxdestroy-causing-system-freeze-and-black-screen/290542/1.
Based on the information above, there are several optimizations and changes made to the codec:
  - FFmpeg encoding AV_PIX_FMT_NV12 directly: The codec is modified to transfer AV_PIX_FMT_NV12 to AV_PIX_FMT_D3D11. This is done because FFmpeg doesn't use Cucontext if the device type is AV_HWDEVICE_TYPE_D3D11VA.
  - FFmpeg decoding with AV_HWDEVICE_TYPE_CUDA acceleration: This functionality is disabled and replaced with AV_HWDEVICE_TYPE_D3D11VA. The decoding process now utilizes D3D11VA acceleration instead of CUDA.

* VRAM encoding and decoding use FFmpeg only. SDK code is retained only for driver availability checks.

### Linux

| GPU           | FFmpeg ram     |
| ------------- | -------------- |
| intel encode  | vaapi          |
| intel decode  | vaapi          |
| nvidia encode | vaapi, nvnec   |
| nvidia decode | vaapi, nvdec   |
| amd encode    | vaapi, amf     |
| amd decode    | vaapi          |

#### Issue

* vaapi: only tested on intel with `va-driver-all`, and hevc_vaapi encoding not supported on my pc
* remove hevc_vaapi because of possible poor quality
* amf: not tested, https://github.com/GPUOpen-LibrariesAndSDKs/AMF/issues/378

### MacOS

| FFmpeg ram encode   | FFmpeg ram decode   |
| ------------------  | ------------------  |
| h265 only           | Y                   |

### Android

| FFmpeg ram encode   |
| ------------------  |
| Y                   |

## System requirements

* intel

  Windows Intel(r) graphics driver since 27.20.100.8935 version. 

  [Hardware Platforms Supported by the Intel(R) Media SDK GPU Runtime](https://www.intel.com/content/www/us/en/docs/onevpl/upgrade-from-msdk/2023-1/onevpl-hardware-support-details.html#HARDWARE-PLATFORMS-SUPPORTED-BY-THE-INTEL-R-MEDIA-SDK-GPU-RUNTIME)

  https://www.intel.com/content/www/us/en/docs/onevpl/developer-reference-media-intel-hardware/1-1/overview.html

* AMD

  AMD Radeon Software Adrenalin Edition 23.1.2 (22.40.01.34) or newer

  https://github.com/GPUOpen-LibrariesAndSDKs/AMF

* nvidia

  Windows: Driver version 471.41 or higher

  https://docs.nvidia.com/video-technologies/video-codec-sdk/11.1/read-me/index.html

  https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new?ncid=em-prod-816193


### Experimental Linux DMA-BUF encoder input

The optional `linux-dmabuf` feature adds `Encoder::encode_dmabuf`. A caller owns
an SDR RGB scanout fd and passes its exact DRM format, modifier, plane offsets,
pitches and exporting render node in `DmaBufFrame`. The encoder imports it with
EGL, renders limited-range BT.601 NV12 directly into exported VAAPI encoder
surfaces, synchronizes the GPU writes, then submits those surfaces to the codec.
The hot path performs no full-frame CPU pixel readback, colour conversion or
upload. The fd remains borrowed for the synchronous encode call.

This requires VAAPI surface export, GBM, GLES3, external-image sampling, and
renderable NV12 plane imports on the same GPU. EGL/GLES/GBM are loaded only on
first use. Build headers are required, but enabling the feature does not link
the application to those graphics libraries. The optional VAAPI surface pool
holds eight surfaces so frames retained by the encoder cannot be overwritten.
Use the encoder on one thread, and drop it on that thread.

The first implementation accepts packed 8-bit and 10-bit SDR RGB. The caller
must retain a CPU fallback for unsupported layouts, drivers, HDR tone mapping,
rotation and device combinations. A successful DMA-BUF import alone does not
establish correct colour or orientation. The ordinary `encode` API remains
available after GPU input, including for fallback.

A real-hardware regression test checks H.264 encode/decode colour, orientation,
updated content, and subsequent RAM input:

```sh
DRM_RENDER_NODE=/dev/dri/renderD128 cargo test --features linux-dmabuf --test linux_dmabuf -- --ignored
```


`DmaBufDetector` compares the imported scanout with an owned GPU snapshot at
8-bit RGB precision. It returns only a changed/unchanged bit to the CPU; it does
not read back the full frame, sample a subset of pixels, or treat framebuffer
identity as content identity. A change in a single quantized RGB pixel is
observable, including when the producer updates the same DMA-BUF in place.
The detector reallocates its snapshots on a size change. Keep it and its caller
on the same thread and retain a CPU fallback when creation/comparison fails.
As with conversion, an existing caller EGL binding is restored on return.

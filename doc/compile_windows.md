# Compilation process

## support 
* graphics cards: nvidia, amd, intel
* codec: h264, h265

## 1. Prepare source/library files

final directory structure

+ ffmpeg  
+ 3rd  
  + |-nv_sdk  
    + |-include  
    + |-lib  
  + |-amf
    * |-AMF
      * |-components
      * |-core

### 1.1 ffmpeg

```shell
git clone git@github.com:21pages/FFmpeg.git
```
The revised commit has not been merged into the official repository, you can rebase to update it.
### 1.2 nvidia

  * Update drivers, install`cuda`, install `Video Codec SDK`,all [here](https://developer.nvidia.com/nvidia-video-codec-sdk/download)

  * Copy `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\include` to `3rd/nv_sdk/include`

  * Copy `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\lib\x64` to `3rd/nv_sdk/lib`

  * Add `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\bin` to `Path`

  * ffnvcodec
    ```shell
      git clone git@github.com:FFmpeg/nv-codec-headers.git
      cd nv-codec-headers 
      git checkout n11.1.5.2
      make && make install
      export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
    ```

    (Change according to installation directory and version)

### 1.3 AMD

* `git clone git@github.com:GPUOpen-LibrariesAndSDKs/AMF.git`
* `git checkout v1.4.29`
* Copy `amf\public\include` to `3rd/amf`, rename `include` to `AMF`



## 2. Prepare the compilation tools

* Install `msvc`, `msys2`
* Enable setting `set MSYS2_PATH_TYPE=inherit` in file `msys2_shell.cmd` to inherit environment variables
* pen vs64-bit command line tools, such as vs2019:`x64 Native Tools Command Prompt for vs2019`, `cd` to the `msys2` installation directory
* Run `msys2_shell.cmd -mingw64`  for open the msys2 command line tool
* Install toolchain
  `pacman -S diffutils make cmake pkg-config yasm nasm git`



## 3. Compile

### Configure option

in the msys2 command line, cd to the ffmpeg directory

#### common
```shell
CC=cl.exe ./configure  \
--prefix=$PWD/../install_release \
--toolchain=msvc \
--disable-everything \
--disable-shared --enable-small \
--disable-runtime-cpudetect --disable-swscale-alpha \
--disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
--disable-network --disable-error-resilience  \
--enable-decoder=h264 --enable-decoder=hevc \
--enable-parser=h264 --enable-parser=hevc \
--enable-bsf=h264_mp4toannexb --enable-bsf=hevc_mp4toannexb  \
--disable-appkit --disable-bzlib --disable-coreimage  --disable-metal --disable-sdl2 \
--disable-securetransport --disable-vulkan --disable-audiotoolbox --disable-v4l2-m2m \
--disable-debug --disable-valgrind-backtrace --disable-large-tests \
--enable-avformat --disable-swresample --disable-swscale --disable-postproc \
```
#### bin
```shell
--disable-programs --disable-ffmpeg --disable-ffplay --disable-ffprobe \
```
#### nvidia
```shell
--enable-cuda-nvcc --enable-nonfree --enable-libnpp \
--enable-encoder=h264_nvenc --enable-encoder=hevc_nvenc \
--enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid \
--enable-hwaccel=h264_nvdec --enable-hwaccel=hevc_nvdec \
--extra-cflags="-I../3rd/nv_sdk/include" \
--extra-ldflags="-libpath:../3rd/nv_sdk/lib" \
```

#### amd
```shell
--enable-amf --enable-encoder=h264_amf --enable-encoder=hevc_amf \
--extra-cflags="-I../3rd/amf" \
```

#### intel
```shell
--enable-mediafoundation --enable-encoder=h264_mf --enable-encoder=hevc_mf \
```

#### d3d9(dxva2)
```shell
--enable-dxva2 \
--enable-hwaccel=h264_dxva2 --enable-hwaccel=hevc_dxva2 \
```

#### d3d11
```shell
--enable-d3d11va \
--enable-hwaccel=h264_d3d11va --enable-hwaccel=hevc_d3d11va \
--enable-hwaccel=h264_d3d11va2 --enable-hwaccel=hevc_d3d11va2 \
```

#### mux
```shell
--enable-muxer=mp4 \
--enable-protocol=file \
```

#### debug
```shell
--extra-cflags="-g" \
```

### Compile and install
`make -j32 && make install`



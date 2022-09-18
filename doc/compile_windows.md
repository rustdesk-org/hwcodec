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
  + |- mfx
    * |-include
      * |-mfx
    * |-lib
      * |-pkgconfig
        * |-libmfx.pc
      * |-mfx.lib

### 1.1 ffmpeg

```shell
git clone git@github.com:21pages/FFmpeg.git
```
The revised commit has not been merged into the official repository, you can rebase to update it.
### 1.2 nvidia

  * Update drivers, install`cuda`, install `Video Codec SDK`,all [here](https://developer.nvidia.com/nvidia-video-codec-sdk/download)

  * Copy `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\include` to `3rd/nv_sdk/include`

  * Copy `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\lib\x64` to `3rd/nv_sdk/lib`

  * Environment variables

    | Key             | Value                                    |
    | --------------- | ---------------------------------------- |
    | CUDA_PATH_V11_6 | C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6 |
    | CUDA_PATH       | %CUDA_PATH_V11_6%                        |
    | CUDA_LIB_PATH   | %CUDA_PATH%\lib\x64                      |
    | CUDA_BIN_PATH   | %CUDA_PATH%\bin                          |
    | CUDA_SDK_PATH   | C:\ProgramData\NVIDIA Corporation\CUDA Samples\v11.6 |
    | CUDA_SDK_BIN    | %CUDA_SDK_PATH%\bin\Win64                |
    | CUDA_SDK_LIB    | %CUDA_SDK_PATH%\common\lib\x64           |
    | PATH            | add %CUDA_BIN_PATH%   %CUDA_SDK_BIN_PATH% |

    (Change according to installation directory and version)

### 1.3 AMD

* `git clone git@github.com:GPUOpen-LibrariesAndSDKs/AMF.git`
* Copy `amf\public\include` to `3rd/amf`, rename `include` to `AMF`

### 1.4 intel

* `git clone git@github.com:lu-zero/mfx_dispatch.git`

* Copy `mfx_dispatch/mfx` to `3rd/mfx/include`

* Edit `CMakeLists.txt`, add `src/mfx_driver_store_loader.cpp`  to `if (CMAKE_SYSTEM_NAME MATCHES "Windows")`

* Use `cmake gui` to generate vs project, compile project `mfx`, project type `release x64`, `Properties->C/C++->Code Generation->Runtime Library` select MT, generate `mfx.lib`, put it in `3rd/mfx/lib`

* put `libmfx.pc` into `3rd/mfx/lib/pkgconfig`

  ```
  prefix=${pcfiledir}/../..

  # libmfx pkg-config.
  exec_prefix=${prefix}
  includedir=${prefix}/include
  libdir=${exec_prefix}/lib

  Name: libmfx
  Description: Intel Media SDK Dispatched static library
  Version: 1.35.1
  Libs: -L"${libdir}" -lmfx -lole32 -lAdvapi32
  Requires: 
  Cflags: -I"${includedir}"
  ```


## 2. Prepare the compilation tools

* Install `msvc`, `msys2`
* Enable setting `set MSYS2_PATH_TYPE=inherit` in file `msys2_shell.cmd` to inherit environment variables
* pen vs64-bit command line tools, such as vs2019:`x64 Native Tools Commmand Prompt for vs2019`, `cd` to the `msys2` installation directory
* Run `msys2_shell.cmd -mingw64`  for open the msys2 command line tool
* Install toolchain
  `pacman -S diffutils make cmake pkg-config yasm nasm git`



## 3. Compile

### Configure option

in the msys2 command line, cd to the ffmpeg directory

#### common
```shell
CC=cl.exe ./configure  \
--prefix=$PWD/../install \
--toolchain=msvc \
--disable-everything \
--disable-shared --enable-small \
--disable-runtime-cpudetect --disable-swscale-alpha \
--disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
--disable-network --disable-dct --disable-dwt --disable-error-resilience --disable-lsp \
--disable-mdct --disable-rdft --disable-fft --disable-faan \
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
Modify `PKG_CONFIG_PATH`,
`export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/e/ffmpeg/3rd/mfx/lib/pkgconfig` (change it according to your directory)

```shell
--enable-libmfx \
--enable-encoder=h264_qsv --enable-encoder=hevc_qsv \
--enable-decoder=h264_qsv --enable-decoder=hevc_qsv \
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
--enable-muxer=flv \
--enable-protocol=file \
```

#### debug
```shell
--extra-cflags="-g" \
```

### Compile and install
`make -j32 && make install`



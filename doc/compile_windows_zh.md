# 编译流程

当前支持显卡: nvidia, amd, intel

当前支持编解码: h264, h265

## 一. 准备源文件/库文件

最终目录结构  

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

### 1.2 nvidia

  * 更新驱动, 安装`cuda`, 安装 `Video Codec SDK`,都在[这里](https://developer.nvidia.com/nvidia-video-codec-sdk/download)

  * 将`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\include`复制到`3rd/nv_sdk/include`

  * 将`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\lib\x64`复制到`3rd/nv_sdk/lib`

  * 环境变量: 将`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\bin` 添加到`PATH`

  * ffnvcodec
    ```shell
      git clone git@github.com:FFmpeg/nv-codec-headers.git
      cd nv-codec-headers 
      git checkout n11.1.5.2
      make && make install
      export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
    ```

### 1.3 AMD

* `git clone git@github.com:GPUOpen-LibrariesAndSDKs/AMF.git`
* `git checkout v1.4.29`
* 将`amf\public\include`拷贝到`3rd/amf`, 将`include`重命名为`AMF`


## 二. 准备编译工具

* 安装msvc, msys2
* 打开`msys2_shell.cmd`里的`set MSYS2_PATH_TYPE=inherit`, 用来继承环境变量
* 打开vs64位命令行工具, 例如vs2019:`x64 Native Tools Command Prompt for vs2019`, `cd`到`msys2`的安装目录
* `msys2_shell.cmd -mingw64` 打开msys2命令行工具
* 安装工具链
  `pacman -S diffutils make cmake pkg-config yasm nasm git`

## 三. 编译

在msys2命令行里, cd到ffmpeg目录, configure

### 编译选项

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

### 编译安装
`make -j32 && make install`

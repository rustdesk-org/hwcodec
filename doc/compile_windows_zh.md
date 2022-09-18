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

### 1.2 nvidia

  * 更新驱动, 安装`cuda`, 安装 `Video Codec SDK`,都在[这里](https://developer.nvidia.com/nvidia-video-codec-sdk/download)

  * 将`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\include`复制到`3rd/nv_sdk/include`

  * 将`C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6\lib\x64`复制到`3rd/nv_sdk/lib`

  * 环境变量

    | 键               | 值                                        |
    | --------------- | ---------------------------------------- |
    | CUDA_PATH_V11_6 | C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6 |
    | CUDA_PATH       | %CUDA_PATH_V11_6%                        |
    | CUDA_LIB_PATH   | %CUDA_PATH%\lib\x64                      |
    | CUDA_BIN_PATH   | %CUDA_PATH%\bin                          |
    | CUDA_SDK_PATH   | C:\ProgramData\NVIDIA Corporation\CUDA Samples\v11.6 |
    | CUDA_SDK_BIN    | %CUDA_SDK_PATH%\bin\Win64                |
    | CUDA_SDK_LIB    | %CUDA_SDK_PATH%\common\lib\x64           |
    | PATH            | 添加:%CUDA_BIN_PATH%   %CUDA_SDK_BIN_PATH% |

    (根据安装目录和版本更改)

### 1.3 AMD

* `git clone git@github.com:GPUOpen-LibrariesAndSDKs/AMF.git`
* 将`amf\public\include`拷贝到`3rd/amf`, 将`include`重命名为`AMF`

### 1.4 intel

* `git clone git@github.com:lu-zero/mfx_dispatch.git`

* 将`mfx_dispatch/mfx`复制到`3rd/mfx/include`

* 编辑`CMakeLists.txt`, 添加`src/mfx_driver_store_loader.cpp` 到`if (CMAKE_SYSTEM_NAME MATCHES "Windows")`

* 使用`cmake gui`生成vs项目, 编译项目`mfx`, 项目类型`release x64`, `属性->C/C++->代码生成->运行库`选择`MT`, 生成`mfx.lib`, 放到`3rd/mfx/lib`

* 写一个`libmfx.pc`, 放到`3rd/mfx/lib/pkgconfig`

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


## 二. 准备编译工具

* 安装msvc, msys2
* 打开`msys2_shell.cmd`里的`set MSYS2_PATH_TYPE=inherit`, 用来继承环境变量
* 打开vs64位命令行工具, 例如vs2019:`x64 Native Tools Commmand Prompt for vs2019`, `cd`到`msys2`的安装目录
* `msys2_shell.cmd -mingw64` 打开msys2命令行工具
* 安装工具链
  `pacman -S diffutils make cmake pkg-config yasm nasm git`



## 三. 编译

在msys2命令行里, cd到ffmpeg目录, configure

### 编译选项

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
修改`PKG_CONFIG_PATH`,
`export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/e/ffmpeg/3rd/mfx/lib/pkgconfig`(根据实际目录)

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

### 编译安装
`make -j32 && make install`

# 编译流程

当前支持显卡: nvidia, amd, 暂不支持intel

当前支持编解码: h264, h265


**注意1：安装nvidia / amd 官方提供的显卡驱动后，每当操作系统升级内核，可能会造成无法进入桌面模式的情况，需要再次手动安装驱动程序。注意保留显卡驱动的安装程序以便随时可通过命令行再次手动安装。**

**注意2：安装显卡驱动前，需要关闭桌面模式，在操作系统命令行模式下进行手动安装，切换至命令行模式方法如下：**
```
# 禁用桌面启动模式
sudo systemctl set-default multi-user.target 

# 下次启动的时候将不会自动进入桌面
# 启动后通过Ctrl + Alt + (F1 ~ F3) 进入一个命令行会话
# 安装相应驱动
...

# 当安装好驱动后，开启桌面启动模式后重启
sudo systemctl set-default graphical.target
sudo reboot
```

## 一. 准备源文件/库文件

### 1.1 ffmpeg

```shell
git clone git@github.com:21pages/FFmpeg.git
```
### 1.2 nvidia

* 更新驱动, 安装`cuda`, 安装 `Video Codec SDK`,都在[这里](https://developer.nvidia.com/nvidia-video-codec-sdk/download)
* 安装 `nv-codec-headers`:
```shell
git clone https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
cd nv-codec-headers && sudo make install
```

### 1.3 AMD

* `git clone git@github.com:GPUOpen-LibrariesAndSDKs/AMF.git`
* `sudo cp AMF/amf/public/include /usr/local/include/AMF -r`
* 安装驱动工具，根据[官方文档](https://amdgpu-install.readthedocs.io/en/latest/install-prereq.html#downloading-the-installer-package)，安装自己系统的`amdgpu-install`
* **关闭桌面模式（参考`注意2`）**，运行命令：`amdgpu-install -y --usecase=amf`
* 如果没有安装好驱动， 可能会报错这种错误：`DLL libamfrt64.so.1 failed to open`.


## 二. 准备其它依赖库

```
sudo apt-get update -qq && sudo apt-get -y install \
  autoconf \
  automake \
  build-essential \
  cmake \
  git-core \
  libass-dev \
  libfreetype6-dev \
  libgnutls28-dev \
  libmp3lame-dev \
  libtool \
  libva-dev \
  libvdpau-dev \
  libvorbis-dev \
  libxcb1-dev \
  libxcb-shm0-dev \
  libxcb-xfixes0-dev \
  meson \
  ninja-build \
  pkg-config \
  texinfo \
  wget \
  yasm \
  zlib1g-dev \
  libc6 \
  libc6-dev \
  unzip \
  libnuma1 \
  libnuma-dev \
  libunistring-dev
```
也许有多余， 但必须有`libva-dev`, `libvdpau-dev`

## 三. 编译

### 编译选项

#### common
```shell
CC=gcc ./configure  \
--prefix=$PWD/../install \
--disable-everything \
--pkg-config-flags="--static" \
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
--disable-avformat --disable-swresample --disable-swscale --disable-postproc \
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
--extra-cflags="-I/usr/local/cuda/include" \
--extra-ldflags="-L/usr/local/cuda/lib64" \
```

#### amd
```shell
--enable-amf --enable-encoder=h264_amf --enable-encoder=hevc_amf \
--extra-cflags="-I/usr/local/include" \
```

#### vaapi
```shell
--enable-hwaccel=h264_vaapi --enable-hwaccel=hevc_vaapi \
```

#### vdpau
```shell
--enable-hwaccel=h264_vdpau --enable-hwaccel=hevc_vdpau \
```

#### debug
```shell
--extra-cflags="-g" \
```

### 编译安装
`make -j32 && make install`

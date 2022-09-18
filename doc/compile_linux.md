# Compilation process

## support 
* graphics cards: nvidia, amd; intel not yet
* codec: h264, h265

**NOTICE 1: Once you have installed the official drivers from nvidia / amd. Whenever the operating system upgrades the kernel, it may not be able to enter the desktop session. You need to manually install the drivers again. Keep the driver installer so you can manually install it again from the command line at any time.**


**NOTICE 2: Before you install graphics card driver, you must disable desktop mode, and install it in operating system command line session. Toggle to CMD session as follows:**
```
# disable desktop session boot
sudo systemctl set-default multi-user.target 

# will not enter desktop session on next boot
# choose a CMD session by `Ctrl + Alt + (F1 ~ F3)`
# install driver...
...

# after installing the driver, enable the desktop boot mode and reboot
sudo systemctl set-default graphical.target
sudo reboot
```

## 1. Prepare source/library files

### 1.1 ffmpeg

```shell
git clone git@github.com:21pages/FFmpeg.git
```

### 1.2 nvidia

* Update drivers, install`cuda`, install `Video Codec SDK`,all [here](https://developer.nvidia.com/nvidia-video-codec-sdk/download)
* Install `nv-codec-headers`:
```shell
git clone https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
cd nv-codec-headers && sudo make install
```

### 1.3 AMD

* `git clone git@github.com:GPUOpen-LibrariesAndSDKs/AMF.git`
* `sudo cp AMF/amf/public/include /usr/local/include/AMF -r`
* Install the drive tool according to [official document](https://amdgpu-install.readthedocs.io/en/latest/install-prereq.html#downloading-the-installer-package)，install `amdgpu-install` of your own system.
* **disable desktop (ref `NOTICE 2`)** , Run：`amdgpu-install -y --usecase=amf`
* If the driver is not installed properly, an error may be reported：`DLL libamfrt64.so.1 failed to open`.


## 2. Prepare other dependent Libraries

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
It may be redundant, but it must include `libva-dev`, `libvdpau-dev`.

## 3. Compile

### Configure option

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

// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/encode_video.c

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "FFMPEG_RAM_ENC"
#include <log.h>

// #define CFG_PKG_TRACE

static int calculate_offset_length(int pix_fmt, int height, const int *linesize,
                                   int *offset, int *length) {
  switch (pix_fmt) {
  case AV_PIX_FMT_YUV420P:
    offset[0] = linesize[0] * height;
    offset[1] = offset[0] + linesize[1] * height / 2;
    *length = offset[1] + linesize[2] * height / 2;
    break;
  case AV_PIX_FMT_NV12:
    offset[0] = linesize[0] * height;
    *length = offset[0] + linesize[1] * height / 2;
    break;
  default:
    LOG_ERROR("unsupported pixfmt" + std::to_string(pix_fmt));
    return -1;
  }

  return 0;
}

extern "C" int ffmpeg_ram_get_linesize_offset_length(int pix_fmt, int width,
                                                     int height, int align,
                                                     int *linesize, int *offset,
                                                     int *length) {
  AVFrame *frame = NULL;
  int ioffset[AV_NUM_DATA_POINTERS] = {0};
  int ilength = 0;
  int ret = -1;

  if (!(frame = av_frame_alloc())) {
    LOG_ERROR("Alloc frame failed");
    goto _exit;
  }

  frame->format = pix_fmt;
  frame->width = width;
  frame->height = height;

  if ((ret = av_frame_get_buffer(frame, align)) < 0) {
    LOG_ERROR("av_frame_get_buffer, ret = " + std::to_string(ret));
    goto _exit;
  }
  if (linesize) {
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
      linesize[i] = frame->linesize[i];
  }
  if (offset || length) {
    ret = calculate_offset_length(pix_fmt, height, frame->linesize, ioffset,
                                  &ilength);
    if (ret < 0)
      goto _exit;
  }
  if (offset) {
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (ioffset[i] == 0)
        break;
      offset[i] = ioffset[i];
    }
  }
  if (length)
    *length = ilength;

  ret = 0;
_exit:
  if (frame)
    av_frame_free(&frame);
  return ret;
}

namespace {
enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateContorl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

typedef void (*RamEncodeCallback)(const uint8_t *data, int len, int64_t pts,
                                  int key, const void *obj);

int set_lantency_free(void *priv_data, const char *name) {
  int ret;

  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0) {
    if ((ret = av_opt_set(priv_data, "delay", "0", 0)) < 0) {
      LOG_ERROR("nvenc set_lantency_free failed, ret = " + std::to_string(ret));
      return -1;
    }
  }
  if (strcmp(name, "h264_amf") == 0 || strcmp(name, "hevc_amf") == 0) {
    if ((ret = av_opt_set(priv_data, "query_timeout", "1000", 0)) < 0) {
      LOG_ERROR("amf set_lantency_free failed, ret = " + std::to_string(ret));
      return -1;
    }
  }
  if (strcmp(name, "h264_qsv") == 0 || strcmp(name, "hevc_qsv") == 0) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR("qsv set_lantency_free failed, ret = " + std::to_string(ret));
      return -1;
    }
  }
  return 0;
}

int set_quality(void *priv_data, const char *name, int quality) {
  int ret;

  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0) {
    switch (quality) {
    // p7 isn't zero lantency
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "preset", "p4", 0)) < 0) {
        LOG_ERROR("nvenc set opt preset p4 failed, ret = " +
                  std::to_string(ret));
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "preset", "p1", 0)) < 0) {
        LOG_ERROR("nvenc set opt preset p1 failed, ret = " +
                  std::to_string(ret));
      }
      break;
    default:
      break;
    }
  }
  if (strcmp(name, "h264_amf") == 0 || strcmp(name, "hevc_amf") == 0) {
    switch (quality) {
    case Quality_High:
      if ((ret = av_opt_set(priv_data, "quality", "quality", 0)) < 0) {
        LOG_ERROR("amf set opt quality quality failed, ret = " +
                  std::to_string(ret));
      }
      break;
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "quality", "balanced", 0)) < 0) {
        LOG_ERROR("amf set opt quality balanced failed, ret = " +
                  std::to_string(ret));
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "quality", "speed", 0)) < 0) {
        LOG_ERROR("amf set opt quality speed failed, ret = " +
                  std::to_string(ret));
      }
      break;
    default:
      break;
    }
  }
  if (strcmp(name, "h264_qsv") == 0 || strcmp(name, "hevc_qsv") == 0) {
    switch (quality) {
    case Quality_High:
      if ((ret = av_opt_set(priv_data, "preset", "veryslow", 0)) < 0) {
        LOG_ERROR("qsv set opt preset veryslow failed, ret = " +
                  std::to_string(ret));
      }
      break;
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "preset", "medium", 0)) < 0) {
        LOG_ERROR("qsv set opt preset medium failed, ret = " +
                  std::to_string(ret));
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "preset", "veryfast", 0)) < 0) {
        LOG_ERROR("qsv set opt preset veryfast failed, ret = " +
                  std::to_string(ret));
      }
      break;
    default:
      break;
    }
  }
  return ret;
}

int set_rate_control(void *priv_data, const char *name, int rc) {
  int ret;

  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0) {
    switch (rc) {
    case RC_CBR:
      if ((ret = av_opt_set(priv_data, "rc", "cbr", 0)) < 0) {
        LOG_ERROR("nvenc set opt rc cbr failed, ret = " + std::to_string(ret));
      }
      break;
    case RC_VBR:
      if ((ret = av_opt_set(priv_data, "rc", "vbr", 0)) < 0) {
        LOG_ERROR("nvenc set opt rc vbr failed, ret = " + std::to_string(ret));
      }
      break;
    default:
      break;
    }
  }
  if (strcmp(name, "h264_amf") == 0 || strcmp(name, "hevc_amf") == 0) {
    switch (rc) {
    case RC_CBR:
      if ((ret = av_opt_set(priv_data, "rc", "cbr", 0)) < 0) {
        LOG_ERROR("amf set opt rc cbr failed, ret = " + std::to_string(ret));
      }
      break;
    case RC_VBR:
      if ((ret = av_opt_set(priv_data, "rc", "vbr_latency", 0)) < 0) {
        LOG_ERROR("amf set opt rc vbr_latency failed, ret = " +
                  std::to_string(ret));
      }
      break;
    default:
      break;
    }
  }
  return ret;
}

int set_gpu(void *priv_data, const char *name, int gpu) {
  int ret = -1;
  if (gpu < 0)
    return -1;
  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR("nvenc set gpu failed, ret = " + std::to_string(ret));
    }
  }
  return ret;
}

class FFmpegRamEncoder {
public:
  AVCodecContext *c_ = NULL;
  AVFrame *frame_ = NULL;
  AVPacket *pkt_ = NULL;
  char name_[32] = {0};
  int64_t first_ms_ = 0;

  int width_ = 0;
  int height_ = 0;
  AVPixelFormat pixfmt_ = AV_PIX_FMT_NV12;
  int align_ = 0;
  int bit_rate_ = 0;
  int time_base_num_ = 1;
  int time_base_den_ = 30;
  int gop_ = 0xFFFF;
  int quality_ = 0;
  int rc_ = 0;
  int thread_count_ = 1;
  int gpu_ = 0;
  RamEncodeCallback callback_ = NULL;
  int offset_[AV_NUM_DATA_POINTERS] = {0};

#ifdef CFG_PKG_TRACE
  int in;
  int out;
#endif

  FFmpegRamEncoder(const char *name, int width, int height, int pixfmt,
                   int align, int bit_rate, int time_base_num,
                   int time_base_den, int gop, int quality, int rc,
                   int thread_count, int gpu, RamEncodeCallback callback) {
    memset(name_, 0, sizeof(name_));
    snprintf(name_, sizeof(name_), "%s", name);
    width_ = width;
    height_ = height;
    pixfmt_ = (AVPixelFormat)pixfmt;
    align_ = align;
    bit_rate_ = bit_rate;
    time_base_num_ = time_base_num;
    time_base_den_ = time_base_den;
    gop_ = gop;
    quality_ = quality;
    rc_ = rc;
    thread_count_ = thread_count;
    gpu_ = gpu;
    callback_ = callback;
  }

  ~FFmpegRamEncoder() {}

  bool init(int *linesize, int *offset, int *length) {
    const AVCodec *codec = NULL;

    int ret;

    if (!(codec = avcodec_find_encoder_by_name(name_))) {
      LOG_ERROR("Codec " + name_ + " not found");
      return false;
    }

    if (!(c_ = avcodec_alloc_context3(codec))) {
      LOG_ERROR("Could not allocate video codec context");
      return false;
    }

    if (!(frame_ = av_frame_alloc())) {
      LOG_ERROR("Could not allocate video frame");
      return false;
    }
    frame_->format = pixfmt_;
    frame_->width = width_;
    frame_->height = height_;

    if ((ret = av_frame_get_buffer(frame_, align_)) < 0) {
      LOG_ERROR("av_frame_get_buffer failed, ret = " + std::to_string((ret)));
      return false;
    }

    if (!(pkt_ = av_packet_alloc())) {
      LOG_ERROR("Could not allocate video packet");
      return false;
    }
    if ((ret = av_new_packet(
             pkt_, av_image_get_buffer_size((AVPixelFormat)frame_->format,
                                            frame_->width, frame_->height,
                                            align_))) < 0) {
      LOG_ERROR("av_new_packet failed, ret = " + std::to_string((ret)));
      return false;
    }

    /* resolution must be a multiple of two */
    c_->width = width_;
    c_->height = height_;
    c_->pix_fmt = (AVPixelFormat)pixfmt_;
    c_->has_b_frames = 0;
    c_->max_b_frames = 0;
    c_->gop_size = gop_;
    /* put sample parameters */
    // https://github.com/FFmpeg/FFmpeg/blob/415f012359364a77e8394436f222b74a8641a3ee/libavcodec/encode.c#L581
    if (bit_rate_ >= 1000) {
      c_->bit_rate = bit_rate_;
      if (strcmp(name_, "h264_qsv") == 0 || strcmp(name_, "hevc_qsv") == 0) {
        c_->rc_max_rate = bit_rate_;
      }
    }
    /* frames per second */
    c_->time_base = av_make_q(time_base_num_, time_base_den_);
    c_->framerate = av_inv_q(c_->time_base);
    c_->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
    c_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    c_->thread_count = thread_count_;
    c_->thread_type = FF_THREAD_SLICE;

    // https://github.com/obsproject/obs-studio/blob/3cc7dc0e7cf8b01081dc23e432115f7efd0c8877/plugins/obs-ffmpeg/obs-ffmpeg-mux.c#L160
    c_->color_range = AVCOL_RANGE_MPEG;
    c_->colorspace = AVCOL_SPC_SMPTE170M;
    c_->color_primaries = AVCOL_PRI_SMPTE170M;
    c_->color_trc = AVCOL_TRC_SMPTE170M;

    if (set_lantency_free(c_->priv_data, name_) < 0) {
      return false;
    }
    set_quality(c_->priv_data, name_, quality_);
    set_rate_control(c_->priv_data, name_, rc_);
    set_gpu(c_->priv_data, name_, gpu_);

    if ((ret = avcodec_open2(c_, codec, NULL)) < 0) {
      LOG_ERROR("avcodec_open2 failed, ret = " + std::to_string((ret)));
      return false;
    }
    first_ms_ = 0;
#ifdef CFG_PKG_TRACE
    in_ = 0;
    out_ = 0;
#endif

    if (ffmpeg_ram_get_linesize_offset_length(pixfmt_, width_, height_, align_,
                                              NULL, offset_, length) != 0)
      return false;

    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      linesize[i] = frame_->linesize[i];
      offset[i] = offset_[i];
    }
    return true;
  }

  int encode(const uint8_t *data, int length, const void *obj, uint64_t ms) {
    int ret;

#ifdef CFG_PKG_TRACE
    in_++;
    LOG_DEBUG("delay EI: in:" + in_ + " out:" + out_);
#endif
    if ((ret = av_frame_make_writable(frame_)) != 0) {
      LOG_ERROR("av_frame_make_writable failed, ret = " +
                std::to_string((ret)));
      return ret;
    }
    if ((ret = fill_frame(frame_, (uint8_t *)data, length, offset_)) != 0)
      return ret;

    return do_encode(obj, ms);
  }

  void free_encoder() {
    if (pkt_)
      av_packet_free(&pkt_);
    if (frame_)
      av_frame_free(&frame_);
    if (c_)
      avcodec_free_context(&c_);
  }

  int set_bitrate(int bitrate) {
    if (strcmp(name_, "h264_nvenc") == 0 || strcmp(name_, "hevc_nvenc") == 0 ||
        strcmp(name_, "h264_amf") == 0 || strcmp(name_, "hevc_amf") == 0) {
      c_->bit_rate = bitrate;
      return 0;
    }
    LOG_ERROR("ffmpeg_ram_set_bitrate " + name_ +
              " does not implement bitrate change");
    return -1;
  }

private:
  int do_encode(const void *obj, int64_t ms) {
    int ret;
    bool encoded = false;
    if ((ret = avcodec_send_frame(c_, frame_)) < 0) {
      LOG_ERROR("avcodec_send_frame failed, ret = " + std::to_string((ret)));
      return ret;
    }

    while (ret >= 0) {
      if ((ret = avcodec_receive_packet(c_, pkt_)) < 0) {
        if (ret != AVERROR(EAGAIN)) {
          LOG_ERROR("avcodec_receive_packet failed, ret = " +
                    std::to_string((ret)));
        }
        goto _exit;
      }
      encoded = true;
#ifdef CFG_PKG_TRACE
      encoder->out++;
      LOG_DEBUG("delay EO: in:" + encoder->in + " out:" + encoder->out);
#endif
      if (first_ms_ == 0)
        first_ms_ = ms;
      callback_(pkt_->data, pkt_->size, ms - first_ms_,
                pkt_->flags & AV_PKT_FLAG_KEY, obj);
    }
  _exit:
    av_packet_unref(pkt_);
    return encoded ? 0 : -1;
  }

  int fill_frame(AVFrame *frame, uint8_t *data, int data_length,
                 const int *const offset) {
    switch (frame->format) {
    case AV_PIX_FMT_NV12:
      if (data_length <
          frame->height * (frame->linesize[0] + frame->linesize[1] / 2)) {
        LOG_ERROR("fill_frame: NV12 data length error. data_length:" +
                  std::to_string(data_length) +
                  ", linesize[0]:" + std::to_string(frame->linesize[0]) +
                  ", linesize[1]:" + std::to_string(frame->linesize[1]));
        return -1;
      }
      frame->data[0] = data;
      frame->data[1] = data + offset[0];
      break;
    case AV_PIX_FMT_YUV420P:
      if (data_length <
          frame->height * (frame->linesize[0] + frame->linesize[1] / 2 +
                           frame->linesize[2] / 2)) {
        LOG_ERROR("fill_frame: 420P data length error. data_length:" +
                  std::to_string(data_length) +
                  ", linesize[0]:" + std::to_string(frame->linesize[0]) +
                  ", linesize[1]:" + std::to_string(frame->linesize[1]) +
                  ", linesize[2]:" + std::to_string(frame->linesize[2]));
        return -1;
      }
      frame->data[0] = data;
      frame->data[1] = data + offset[0];
      frame->data[2] = data + offset[1];
      break;
    default:
      LOG_ERROR("fill_frame: unsupported format, " +
                std::to_string(frame->format));
      return -1;
    }
    return 0;
  }
};

} // namespace

extern "C" FFmpegRamEncoder *
ffmpeg_ram_new_encoder(const char *name, int width, int height, int pixfmt,
                       int align, int bit_rate, int time_base_num,
                       int time_base_den, int gop, int quality, int rc,
                       int thread_count, int gpu, int *linesize, int *offset,
                       int *length, RamEncodeCallback callback) {
  FFmpegRamEncoder *encoder = NULL;
  try {
    encoder = new FFmpegRamEncoder(name, width, height, pixfmt, align, bit_rate,
                                   time_base_num, time_base_den, gop, quality,
                                   rc, thread_count, gpu, callback);
    if (encoder) {
      if (encoder->init(linesize, offset, length)) {
        return encoder;
      }
    }
  } catch (const std::exception &e) {
    LOG_ERROR("new FFmpegRamEncoder failed, " + std::string(e.what()));
  }
  if (encoder) {
    encoder->free_encoder();
    delete encoder;
    encoder = NULL;
  }
  return NULL;
}

extern "C" int ffmpeg_ram_encode(FFmpegRamEncoder *encoder, const uint8_t *data,
                                 int length, const void *obj, uint64_t ms) {
  try {
    return encoder->encode(data, length, obj, ms);
  } catch (const std::exception &e) {
    LOG_ERROR("ffmpeg_ram_encode failed, " + std::string(e.what()));
  }
  return -1;
}

extern "C" void ffmpeg_ram_free_encoder(FFmpegRamEncoder *encoder) {
  try {
    if (!encoder)
      return;
    encoder->free_encoder();
    delete encoder;
    encoder = NULL;
  } catch (const std::exception &e) {
    LOG_ERROR("free encoder failed, " + std::string(e.what()));
  }
}

extern "C" int ffmpeg_ram_set_bitrate(FFmpegRamEncoder *encoder, int bitrate) {
  try {
    return encoder->set_bitrate(bitrate);
  } catch (const std::exception &e) {
    LOG_ERROR("ffmpeg_ram_set_bitrate failed, " + std::string(e.what()));
  }
  return -1;
}
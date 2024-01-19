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

#define LOG_MODULE "FF1ENC"
#include <log.h>

// #define CFG_PKG_TRACE

enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateContorl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

typedef void (*RamEncodeCallback)(const uint8_t *data, int len, int64_t pts,
                                  int key, const void *obj);

typedef struct Encoder {
  AVCodecContext *c;
  AVFrame *frame;
  AVPacket *pkt;
  int offset[AV_NUM_DATA_POINTERS];
  char name[32];
  RamEncodeCallback callback;
  int64_t first_ms;

#ifdef CFG_PKG_TRACE
  int in;
  int out;
#endif
} Encoder;

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

extern "C" int ffram_get_linesize_offset_length(int pix_fmt, int width,
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

static int set_lantency_free(void *priv_data, const char *name) {
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

static int set_quality(void *priv_data, const char *name, int quality) {
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

static int set_rate_control(void *priv_data, const char *name, int rc) {
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

static int set_gpu(void *priv_data, const char *name, int gpu) {
  int ret;
  if (gpu < 0)
    return -1;
  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR("nvenc set gpu failed, ret = " + std::to_string(ret));
    }
  }
  return ret;
}

extern "C" Encoder *ffram_new_encoder(const char *name, int width, int height,
                                      int pixfmt, int align, int bit_rate,
                                      int time_base_num, int time_base_den,
                                      int gop, int quality, int rc,
                                      int thread_count, int gpu, int *linesize,
                                      int *offset, int *length,
                                      RamEncodeCallback callback) {
  const AVCodec *codec = NULL;
  AVCodecContext *c = NULL;
  AVFrame *frame = NULL;
  AVPacket *pkt = NULL;
  Encoder *encoder = NULL;
  int ret;

  if (!(codec = avcodec_find_encoder_by_name(name))) {
    LOG_ERROR("Codec" + name + " not found");
    goto _exit;
  }

  if (!(c = avcodec_alloc_context3(codec))) {
    LOG_ERROR("Could not allocate video codec context");
    goto _exit;
  }

  if (!(frame = av_frame_alloc())) {
    LOG_ERROR("Could not allocate video frame");
    goto _exit;
  }
  frame->format = pixfmt;
  frame->width = width;
  frame->height = height;

  if ((ret = av_frame_get_buffer(frame, align)) < 0) {
    LOG_ERROR("av_frame_get_buffer failed, ret = " + std::to_string((ret)));
    goto _exit;
  }

  if (!(pkt = av_packet_alloc())) {
    LOG_ERROR("Could not allocate video packet");
    goto _exit;
  }
  if ((ret = av_new_packet(pkt, av_image_get_buffer_size(
                                    (AVPixelFormat)frame->format, frame->width,
                                    frame->height, align))) < 0) {
    LOG_ERROR("av_new_packet failed, ret = " + std::to_string((ret)));
    goto _exit;
  }

  /* resolution must be a multiple of two */
  c->width = width;
  c->height = height;
  c->pix_fmt = (AVPixelFormat)pixfmt;
  c->has_b_frames = 0;
  c->max_b_frames = 0;
  c->gop_size = gop;
  /* put sample parameters */
  // https://github.com/FFmpeg/FFmpeg/blob/415f012359364a77e8394436f222b74a8641a3ee/libavcodec/encode.c#L581
  if (bit_rate >= 1000) {
    c->bit_rate = bit_rate;
    if (strcmp(name, "h264_qsv") == 0 || strcmp(name, "hevc_qsv") == 0) {
      c->rc_max_rate = bit_rate;
    }
  }
  /* frames per second */
  c->time_base = av_make_q(time_base_num, time_base_den);
  c->framerate = av_inv_q(c->time_base);
  c->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  c->thread_count = thread_count;
  c->thread_type = FF_THREAD_SLICE;

  // https://github.com/obsproject/obs-studio/blob/3cc7dc0e7cf8b01081dc23e432115f7efd0c8877/plugins/obs-ffmpeg/obs-ffmpeg-mux.c#L160
  c->color_range = AVCOL_RANGE_MPEG;
  c->colorspace = AVCOL_SPC_SMPTE170M;
  c->color_primaries = AVCOL_PRI_SMPTE170M;
  c->color_trc = AVCOL_TRC_SMPTE170M;

  if (set_lantency_free(c->priv_data, name) < 0) {
    goto _exit;
  }
  set_quality(c->priv_data, name, quality);
  set_rate_control(c->priv_data, name, rc);
  set_gpu(c->priv_data, name, gpu);

  if ((ret = avcodec_open2(c, codec, NULL)) < 0) {
    LOG_ERROR("avcodec_open2 failed, ret = " + std::to_string((ret)));
    goto _exit;
  }

  if (!(encoder = (Encoder *)calloc(1, sizeof(Encoder)))) {
    LOG_ERROR("calloc failed");
    goto _exit;
  }
  encoder->c = c;
  encoder->frame = frame;
  encoder->pkt = pkt;
  encoder->callback = callback;
  encoder->first_ms = 0;
  snprintf(encoder->name, sizeof(encoder->name), "%s", name);
#ifdef CFG_PKG_TRACE
  encoder->in = 0;
  encoder->out = 0;
#endif

  if (ffram_get_linesize_offset_length(pixfmt, width, height, align, NULL,
                                       encoder->offset, length) != 0)
    goto _exit;

  for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
    linesize[i] = frame->linesize[i];
    offset[i] = encoder->offset[i];
  }

  return encoder;

_exit:
  if (encoder)
    free(encoder);
  if (pkt)
    av_packet_free(&pkt);
  if (frame)
    av_frame_free(&frame);
  if (c)
    avcodec_free_context(&c);
  return NULL;
}

static int fill_frame(AVFrame *frame, uint8_t *data, int data_length,
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

static int do_encode(Encoder *encoder, AVFrame *frame, const void *obj,
                     int64_t ms) {
  int ret;
  bool encoded = false;
  AVPacket *pkt = encoder->pkt;

  if ((ret = avcodec_send_frame(encoder->c, frame)) < 0) {
    LOG_ERROR("avcodec_send_frame failed, ret = " + std::to_string((ret)));
    return ret;
  }

  while (ret >= 0) {
    if ((ret = avcodec_receive_packet(encoder->c, pkt)) < 0) {
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
    if (encoder->first_ms == 0)
      encoder->first_ms = ms;
    encoder->callback(pkt->data, pkt->size, ms - encoder->first_ms,
                      pkt->flags & AV_PKT_FLAG_KEY, obj);
  }
_exit:
  av_packet_unref(pkt);
  return encoded ? 0 : -1;
}

extern "C" int ffram_encode(Encoder *encoder, const uint8_t *data, int length,
                            const void *obj, uint64_t ms) {
  int ret;

#ifdef CFG_PKG_TRACE
  encoder->in++;
  LOG_DEBUG("delay EI: in:" + encoder->in + " out:" + encoder->out);
#endif
  if ((ret = av_frame_make_writable(encoder->frame)) != 0) {
    LOG_ERROR("av_frame_make_writable failed, ret = " + std::to_string((ret)));
    return ret;
  }
  if ((ret = fill_frame(encoder->frame, (uint8_t *)data, length,
                        encoder->offset)) != 0)
    return ret;

  return do_encode(encoder, encoder->frame, obj, ms);
}

extern "C" void ffram_free_encoder(Encoder *encoder) {
  if (!encoder)
    return;
  if (encoder->pkt)
    av_packet_free(&encoder->pkt);
  if (encoder->frame)
    av_frame_free(&encoder->frame);
  if (encoder->c)
    avcodec_free_context(&encoder->c);
}

extern "C" int ffram_set_bitrate(Encoder *encoder, int bitrate) {
  const char *name = encoder->name;
  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0 ||
      strcmp(name, "h264_amf") == 0 || strcmp(name, "hevc_amf") == 0) {
    encoder->c->bit_rate = bitrate;
    return 0;
  }
  LOG_ERROR("ffram_set_bitrate " + name + " does not implement bitrate change");
  return -1;
}
// VAAPI decode → DRM PRIME (dma-buf). Linux only. No RGB download.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
}

#include <new>
#include <string.h>
#include <unistd.h>

#include "ffmpeg_ram_ffi.h"

namespace {

struct VaapiPrimeDec {
  AVCodecContext *c = nullptr;
  AVBufferRef *hw_device_ctx = nullptr;
  AVFrame *frame = nullptr;
  AVFrame *mapped = nullptr;
  AVPacket *pkt = nullptr;
};

enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                 const enum AVPixelFormat *pix_fmts) {
  (void)ctx;
  for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
    if (*p == AV_PIX_FMT_VAAPI) {
      return *p;
    }
  }
  return AV_PIX_FMT_NONE;
}

void close_mapped(VaapiPrimeDec *d) {
  if (d->mapped) {
    av_frame_unref(d->mapped);
  }
}

int fill_prime(AVFrame *mapped, FFmpegPrimeFrame *out) {
  auto *desc = (AVDRMFrameDescriptor *)mapped->data[0];
  if (!desc || desc->nb_layers < 1 || desc->nb_objects < 1) {
    return 0;
  }
  memset(out, 0, sizeof(*out));
  out->kind = FFMPEG_GPU_FRAME_PRIME;
  if (desc->nb_objects > 4) {
    return 0;
  }
  out->n_fds = desc->nb_objects;
  for (int i = 0; i < out->n_fds; i++) {
    int fd = dup(desc->objects[i].fd);
    if (fd < 0) {
      for (int j = 0; j < i; j++) {
        close(out->fds[j]);
      }
      return 0;
    }
    out->fds[i] = fd;
    if (i == 0) {
      out->modifier = desc->objects[i].format_modifier;
    }
  }
  out->width = mapped->width;
  out->height = mapped->height;
  int n = 0;
  for (int L = 0; L < desc->nb_layers && n < 4; L++) {
    AVDRMLayerDescriptor *layer = &desc->layers[L];
    for (int p = 0; p < layer->nb_planes && n < 4; p++) {
      out->pitches[n] = (int)layer->planes[p].pitch;
      out->offsets[n] = (int)layer->planes[p].offset;
      out->obj_indices[n] = layer->planes[p].object_index;
      n++;
    }
  }
  out->n_planes = n;
  if (desc->nb_layers == 1) {
    out->fourcc = desc->layers[0].format;
  } else {
    out->fourcc = 0x3231564e; // DRM_FORMAT_NV12
  }
  return n >= 1;
}

} // namespace

extern "C" void *ffmpeg_vaapi_prime_new(int hevc) {
  auto *d = new (std::nothrow) VaapiPrimeDec();
  if (!d) {
    return nullptr;
  }
  const AVCodec *codec = avcodec_find_decoder_by_name(hevc ? "hevc" : "h264");
  if (!codec) {
    delete d;
    return nullptr;
  }
  d->c = avcodec_alloc_context3(codec);
  if (!d->c) {
    delete d;
    return nullptr;
  }
  d->c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  d->c->thread_count = 1;
  d->c->get_format = get_hw_format;
  d->c->extra_hw_frames = 8;
  int ret = av_hwdevice_ctx_create(&d->hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                   nullptr, nullptr, 0);
  if (ret < 0) {
    ffmpeg_vaapi_prime_free(d);
    return nullptr;
  }
  d->c->hw_device_ctx = av_buffer_ref(d->hw_device_ctx);
  d->pkt = av_packet_alloc();
  d->frame = av_frame_alloc();
  d->mapped = av_frame_alloc();
  if (!d->pkt || !d->frame || !d->mapped) {
    ffmpeg_vaapi_prime_free(d);
    return nullptr;
  }
  if (avcodec_open2(d->c, codec, nullptr) != 0) {
    ffmpeg_vaapi_prime_free(d);
    return nullptr;
  }
  return d;
}

extern "C" void ffmpeg_vaapi_prime_free(void *decoder) {
  auto *d = (VaapiPrimeDec *)decoder;
  if (!d) {
    return;
  }
  close_mapped(d);
  if (d->mapped)
    av_frame_free(&d->mapped);
  if (d->frame)
    av_frame_free(&d->frame);
  if (d->pkt)
    av_packet_free(&d->pkt);
  if (d->c)
    avcodec_free_context(&d->c);
  if (d->hw_device_ctx)
    av_buffer_unref(&d->hw_device_ctx);
  delete d;
}

extern "C" int ffmpeg_vaapi_prime_decode(void *decoder, const uint8_t *data,
                                         int length, FFmpegPrimeFrame *out) {
  auto *d = (VaapiPrimeDec *)decoder;
  if (!d || !data || length <= 0 || !out) {
    return 0;
  }
  d->pkt->data = (uint8_t *)data;
  d->pkt->size = length;
  int ret = avcodec_send_packet(d->c, d->pkt);
  av_packet_unref(d->pkt);
  if (ret < 0) {
    return 0;
  }
  int got = 0;
  while (ret >= 0) {
    ret = avcodec_receive_frame(d->c, d->frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }
    if (ret < 0) {
      break;
    }
    close_mapped(d);
    d->mapped->format = AV_PIX_FMT_DRM_PRIME;
    if (av_hwframe_map(d->mapped, d->frame, AV_HWFRAME_MAP_READ) < 0) {
      av_frame_unref(d->frame);
      continue;
    }
    if (fill_prime(d->mapped, out)) {
      got = 1;
    } else {
      close_mapped(d);
    }
    av_frame_unref(d->frame);
  }
  return got;
}

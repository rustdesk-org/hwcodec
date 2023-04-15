// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/hw_decode.c
// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/decode_video.c

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <stdbool.h>

// #define CFG_PKG_TRACE

extern void my_fprintf(FILE *const _Stream, const char *const _Format, ...);
#define fprintf my_fprintf

typedef void (*DecodeCallback)(const void *obj, int width, int height,
                               enum AVPixelFormat pixfmt,
                               int linesize[AV_NUM_DATA_POINTERS],
                               uint8_t *data[AV_NUM_DATA_POINTERS], int key);

typedef struct Decoder {
  AVCodecContext *c;
  AVBufferRef *hw_device_ctx;
  AVCodecParserContext *sw_parser_ctx;
  AVFrame *sw_frame;
  AVFrame *frame;
  AVPacket *pkt;
  bool hwaccel;
  int hw_pix_fmt;
  DecodeCallback callback;

#ifdef CFG_PKG_TRACE
  int in;
  int out;
#endif
} Decoder;

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    int hw_pix_fmt = *((int*)ctx->extradata);
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }
    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

Decoder *new_decoder(const char *name, int device_type,
                     DecodeCallback callback) {
  AVCodecContext *c = NULL;
  AVBufferRef *hw_device_ctx = NULL;
  AVCodecParserContext *sw_parse_ctx = NULL;
  AVFrame *sw_frame = NULL;
  AVFrame *frame = NULL;
  AVPacket *pkt = NULL;
  const AVCodec *codec = NULL;
  Decoder *decoder = NULL;
  bool hwaccel = device_type != AV_HWDEVICE_TYPE_NONE;
  enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
  int ret;

  if (!(codec = avcodec_find_decoder_by_name(name))) {
    fprintf(stderr, "Codec %s not found\n", name);
    goto _exit;
  }

  if (hwaccel) {
    bool support_device = false;
    enum AVHWDeviceType iter_type = AV_HWDEVICE_TYPE_NONE;
    while((iter_type = av_hwdevice_iterate_types(iter_type)) != AV_HWDEVICE_TYPE_NONE) {
      if (iter_type == device_type) {
        support_device = true;
        break;
      }
    }
    if (!support_device) {
      fprintf(stderr, "not support device type %s.\n", av_hwdevice_get_type_name(device_type));
      goto _exit;
    }
  }

  if (!(c = avcodec_alloc_context3(codec))) {
    fprintf(stderr, "Could not allocate video codec context\n");
    goto _exit;
  }

  c->get_format  = get_hw_format;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  c->thread_count = 4;
  c->thread_type = FF_THREAD_SLICE;

  if (strcmp(name, "h264_qsv") == 0 || strcmp(name, "hevc_qsv") == 0) {
    if ((ret = av_opt_set(c->priv_data, "async_depth", "1", 0)) < 0) {
      fprintf(stderr, "qsv set opt failed %s\n", av_err2str(ret));
      goto _exit;
    }
    // https://github.com/FFmpeg/FFmpeg/blob/c6364b711bad1fe2fbd90e5b2798f87080ddf5ea/libavcodec/qsvdec.c#L932
    // for disable warning
    c->pkt_timebase = av_make_q(1, 30);
  }

  if (hwaccel) {
    ret = av_hwdevice_ctx_create(&hw_device_ctx, device_type, NULL, NULL, 0);
    if (ret < 0) {
      fprintf(stderr, "Failed to create specified HW device:%s\n",
              av_err2str(ret));
      goto _exit;
    }
    c->hw_device_ctx = hw_device_ctx;
    if (!(sw_frame = av_frame_alloc())) {
      fprintf(stderr, "Can not alloc frame\n");
      goto _exit;
    }
  } else {
    if (!(sw_parse_ctx = av_parser_init(codec->id))) {
      fprintf(stderr, "parser not found\n");
      goto _exit;
    }
    sw_parse_ctx->flags |= PARSER_FLAG_COMPLETE_FRAMES;
  }

  if (!(pkt = av_packet_alloc())) {
    fprintf(stderr, "Failed to allocate AVPacket\n");
    goto _exit;
  }

  if (!(frame = av_frame_alloc())) {
    fprintf(stderr, "Can not alloc frame\n");
    goto _exit;
  }

  if ((ret = avcodec_open2(c, codec, NULL)) != 0) {
    fprintf(stderr, "avcodec_open2: %s\n", av_err2str(ret));
    goto _exit;
  }

  if (hwaccel) {
    // todo: find or specific
    for (int i = 0;; i++) {
      const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
      if (!config) {
          fprintf(stderr, "Decoder %s does not support device type %s.\n",
                  codec->name, av_hwdevice_get_type_name(device_type));
          goto _exit;
      }
      if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
          config->device_type == device_type) {
          hw_pix_fmt = config->pix_fmt;
          break;
      }
    }
    if (hw_pix_fmt == AV_PIX_FMT_NONE) {
      fprintf(stderr, "failed to set hw_pix_fmt\n");
      goto _exit;
    }
  }

  if (!(decoder = calloc(1, sizeof(Decoder)))) {
    fprintf(stderr, "calloc failed\n");
    goto _exit;
  }
  decoder->c = c;
  decoder->hw_device_ctx = hw_device_ctx;
  decoder->sw_parser_ctx = sw_parse_ctx;
  decoder->frame = frame;
  decoder->sw_frame = sw_frame;
  decoder->pkt = pkt;
  decoder->hwaccel = hwaccel;
  decoder->callback = callback;
  decoder->hw_pix_fmt = hw_pix_fmt;
  c->extradata = (uint8_t*)&decoder->hw_pix_fmt;
#ifdef CFG_PKG_TRACE
  decoder->in = 0;
  decoder->out = 0;
#endif

  return decoder;

_exit:
  if (frame) av_frame_free(&frame);
  if (pkt) av_packet_free(&pkt);
  if (sw_frame) av_frame_free(&sw_frame);
  if (sw_parse_ctx) av_parser_close(sw_parse_ctx);
  if (c)
    avcodec_free_context(&c);
  else if (hw_device_ctx)
    av_buffer_unref(&hw_device_ctx);

  return NULL;
}

static int do_decode(Decoder *decoder, AVPacket *pkt, const void *obj) {
  int ret;
  AVFrame *tmp_frame = NULL;
  bool decoded = false;

  ret = avcodec_send_packet(decoder->c, pkt);
  if (ret < 0) {
    fprintf(stderr, "avcodec_send_packet: %s\n", av_err2str(ret));
    return ret;
  }

  while (ret >= 0) {
    if ((ret = avcodec_receive_frame(decoder->c, decoder->frame)) != 0) {
      if (ret != AVERROR(EAGAIN))
        fprintf(stderr, "avcodec_receive_frame: %s\n", av_err2str(ret));
      goto _exit;
    }

    if (decoder->hwaccel) {
      if (!decoder->frame->hw_frames_ctx) {
        fprintf(stderr, "hw_frames_ctx is NULL\n");
        goto _exit;
      }
      if ((ret = av_hwframe_transfer_data(decoder->sw_frame, decoder->frame,
                                          0)) < 0) {
        fprintf(stderr, "av_hwframe_transfer_data: %s\n", av_err2str(ret));
        goto _exit;
      }

      tmp_frame = decoder->sw_frame;
    } else {
      tmp_frame = decoder->frame;
    }
    decoded = true;
#ifdef CFG_PKG_TRACE
    decoder->out++;
    fprintf(stdout, "delay DO: in:%d, out:%d\n", decoder->in, decoder->out);
#endif
    decoder->callback(obj, tmp_frame->width, tmp_frame->height,
                      tmp_frame->format, tmp_frame->linesize, tmp_frame->data,
                      tmp_frame->key_frame);
  }
_exit:
  av_packet_unref(decoder->pkt);
  return decoded ? 0 : -1;
}

int decode(Decoder *decoder, const uint8_t *data, int length, const void *obj) {
  int ret = -1;
#ifdef CFG_PKG_TRACE
  decoder->in++;
  fprintf(stdout, "delay DI: in:%d, out:%d\n", decoder->in, decoder->out);
#endif

  if (!data || !length) {
    fprintf(stderr, "illegal decode parameter\n");
    return -1;
  }

  if (decoder->hwaccel) {
    decoder->pkt->data = (uint8_t *)data;
    decoder->pkt->size = length;
    ret = do_decode(decoder, decoder->pkt, obj);
  } else {
    while (length >= 0) {
      ret = av_parser_parse2(decoder->sw_parser_ctx, decoder->c,
                             &decoder->pkt->data, &decoder->pkt->size, data,
                             length, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (ret < 0) {
        fprintf(stderr, "av_parser_parse2: %s", av_err2str(ret));
        return ret;
      }
      data += ret;
      length -= ret;

      if (decoder->pkt->size > 0) {
        ret = do_decode(decoder, decoder->pkt, obj);
        break;
      }
    }
  }

  return ret;
}

void free_decoder(Decoder *decoder) {
  if (!decoder) return;
  if (decoder->frame) av_frame_free(&decoder->frame);
  if (decoder->pkt) av_packet_free(&decoder->pkt);
  if (decoder->sw_frame) av_frame_free(&decoder->sw_frame);
  if (decoder->sw_parser_ctx) av_parser_close(decoder->sw_parser_ctx);
  if (decoder->c)
    avcodec_free_context(&decoder->c);
  else if (decoder->hw_device_ctx)
    av_buffer_unref(&decoder->hw_device_ctx);
}

#include "incbin.h"

INCBIN_EXTERN(BinFile264);
INCBIN_EXTERN(BinFile265);

void get_bin_file(int is265, uint8_t **p, /*int maxlen,*/ int *len) {
    if (is265 == 0) {
      *p = (uint8_t*)gBinFile264Data;
      *len = gBinFile264Size;
    } else {
      *p = (uint8_t*)gBinFile265Data;
      *len = gBinFile265Size;
    }
}

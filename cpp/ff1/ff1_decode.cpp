// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/hw_decode.c
// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/decode_video.c

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <stdbool.h>

static char av_error[AV_ERROR_MAX_STRING_SIZE] = {0};
#define av_err2str(errnum)                                                     \
  av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

// #define CFG_PKG_TRACE

typedef void (*PixelbufferDecodeCallback)(const void *obj, int width,
                                          int height, enum AVPixelFormat pixfmt,
                                          int linesize[AV_NUM_DATA_POINTERS],
                                          uint8_t *data[AV_NUM_DATA_POINTERS],
                                          int key);

typedef struct Decoder {
  AVCodecContext *c;
  AVBufferRef *hw_device_ctx;
  AVCodecParserContext *sw_parser_ctx;
  AVFrame *sw_frame;
  AVFrame *frame;
  AVPacket *pkt;
  bool hwaccel;

  char name[128];
  int device_type;
  int thread_count;
  PixelbufferDecodeCallback callback;

  bool ready_decode;
  int last_width;
  int last_height;

#ifdef CFG_PKG_TRACE
  int in;
  int out;
#endif
} Decoder;

extern "C" void hwcodec_free_decoder(Decoder *decoder) {
  if (!decoder)
    return;
  if (decoder->frame)
    av_frame_free(&decoder->frame);
  if (decoder->pkt)
    av_packet_free(&decoder->pkt);
  if (decoder->sw_frame)
    av_frame_free(&decoder->sw_frame);
  if (decoder->sw_parser_ctx)
    av_parser_close(decoder->sw_parser_ctx);
  if (decoder->c)
    avcodec_free_context(&decoder->c);
  else if (decoder->hw_device_ctx)
    av_buffer_unref(&decoder->hw_device_ctx);

  decoder->frame = NULL;
  decoder->pkt = NULL;
  decoder->sw_frame = NULL;
  decoder->sw_parser_ctx = NULL;
  decoder->c = NULL;
  decoder->hw_device_ctx = NULL;
  decoder->ready_decode = false;
}

static int reset(Decoder *d) {
  if (d)
    hwcodec_free_decoder(d);
  else
    return -1;

  AVCodecContext *c = NULL;
  AVBufferRef *hw_device_ctx = NULL;
  AVCodecParserContext *sw_parse_ctx = NULL;
  AVFrame *sw_frame = NULL;
  AVFrame *frame = NULL;
  AVPacket *pkt = NULL;
  const AVCodec *codec = NULL;
  bool hwaccel = d->device_type != AV_HWDEVICE_TYPE_NONE;
  int ret;

  if (!(codec = avcodec_find_decoder_by_name(d->name))) {
    fprintf(stdout, "Codec %s not found\n", d->name);
    return -1;
  }

  if (!(c = avcodec_alloc_context3(codec))) {
    fprintf(stdout, "Could not allocate video codec context\n");
    return -1;
  }

  c->flags |= AV_CODEC_CAP_TRUNCATED;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  c->thread_count = d->thread_count;
  c->thread_type = FF_THREAD_SLICE;

  if (strcmp(d->name, "h264_qsv") == 0 || strcmp(d->name, "hevc_qsv") == 0) {
    if ((ret = av_opt_set(c->priv_data, "async_depth", "1", 0)) < 0) {
      fprintf(stdout, "qsv set opt failed %s\n", av_err2str(ret));
      return -1;
    }
    // https://github.com/FFmpeg/FFmpeg/blob/c6364b711bad1fe2fbd90e5b2798f87080ddf5ea/libavcodec/qsvdec.c#L932
    // for disable warning
    c->pkt_timebase = av_make_q(1, 30);
  }

  ret = av_hwdevice_ctx_create(&hw_device_ctx, (AVHWDeviceType)d->device_type,
                               NULL, NULL, 0);
  if (ret < 0) {
    fprintf(stdout, "Failed to create specified HW device:%s\n",
            av_err2str(ret));
    return -1;
  }
  c->hw_device_ctx = hw_device_ctx;
  if (!(sw_frame = av_frame_alloc())) {
    fprintf(stdout, "Can not alloc frame\n");
    return -1;
  }
  if (!(sw_parse_ctx = av_parser_init(codec->id))) {
    fprintf(stdout, "parser not found\n");
    return -1;
  }
  sw_parse_ctx->flags |= PARSER_FLAG_COMPLETE_FRAMES;

  if (!(pkt = av_packet_alloc())) {
    fprintf(stdout, "Failed to allocate AVPacket\n");
    return -1;
  }

  if (!(frame = av_frame_alloc())) {
    fprintf(stdout, "Can not alloc frame\n");
    return -1;
  }

  if ((ret = avcodec_open2(c, codec, NULL)) != 0) {
    fprintf(stdout, "avcodec_open2: %s\n", av_err2str(ret));
    return -1;
  }

  d->c = c;
  d->hw_device_ctx = hw_device_ctx;
  d->sw_parser_ctx = sw_parse_ctx;
  d->frame = frame;
  d->sw_frame = sw_frame;
  d->pkt = pkt;
  d->hwaccel = hwaccel;
  d->last_width = 0;
  d->last_height = 0;
#ifdef CFG_PKG_TRACE
  decoder->in = 0;
  decoder->out = 0;
#endif
  d->ready_decode = true;

  return 0;
}

extern "C" Decoder *hwcodec_new_decoder(const char *name, int device_type,
                                        int thread_count,
                                        PixelbufferDecodeCallback callback) {
  Decoder *decoder = NULL;

  if (!(decoder = (Decoder *)calloc(1, sizeof(Decoder)))) {
    fprintf(stdout, "calloc failed\n");
    return NULL;
  }
  snprintf(decoder->name, sizeof(decoder->name), "%s", name);
  decoder->device_type = device_type;
  decoder->thread_count = thread_count;
  decoder->callback = callback;

  if (reset(decoder) != 0) {
    fprintf(stdout, "reset failed\n");
    hwcodec_free_decoder(decoder);
    return NULL;
  }
  return decoder;
}

static int do_decode(Decoder *decoder, AVPacket *pkt, const void *obj) {
  int ret;
  AVFrame *tmp_frame = NULL;
  bool decoded = false;

  ret = avcodec_send_packet(decoder->c, pkt);
  if (ret < 0) {
    fprintf(stdout, "avcodec_send_packet: %s\n", av_err2str(ret));
    return ret;
  }

  while (ret >= 0) {
    if ((ret = avcodec_receive_frame(decoder->c, decoder->frame)) != 0) {
      if (ret != AVERROR(EAGAIN))
        fprintf(stdout, "avcodec_receive_frame: %s\n", av_err2str(ret));
      goto _exit;
    }

    if (decoder->hwaccel) {
      if (!decoder->frame->hw_frames_ctx) {
        fprintf(stdout, "hw_frames_ctx is NULL\n");
        goto _exit;
      }
      if ((ret = av_hwframe_transfer_data(decoder->sw_frame, decoder->frame,
                                          0)) < 0) {
        fprintf(stdout, "av_hwframe_transfer_data: %s\n", av_err2str(ret));
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
    decoder->callback(obj, decoder->sw_parser_ctx->width,
                      decoder->sw_parser_ctx->height,
                      (AVPixelFormat)tmp_frame->format, tmp_frame->linesize,
                      tmp_frame->data, tmp_frame->key_frame);
  }
_exit:
  av_packet_unref(decoder->pkt);
  return decoded ? 0 : -1;
}

extern "C" int hwcodec_decode(Decoder *decoder, const uint8_t *data, int length,
                              const void *obj) {
  int ret = -1;
  bool retried = false;
#ifdef CFG_PKG_TRACE
  decoder->in++;
  fprintf(stdout, "delay DI: in:%d, out:%d\n", decoder->in, decoder->out);
#endif

  if (!data || !length) {
    fprintf(stdout, "illegal decode parameter\n");
    return -1;
  }
  if (!decoder->ready_decode) {
    fprintf(stdout, "not ready decode\n");
    return -1;
  }

_lable:
  ret = av_parser_parse2(decoder->sw_parser_ctx, decoder->c,
                         &decoder->pkt->data, &decoder->pkt->size, data, length,
                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
  if (ret < 0) {
    fprintf(stdout, "av_parser_parse2: %s", av_err2str(ret));
    return ret;
  }
  if (decoder->last_width != 0 && decoder->last_height != 0) {
    if (decoder->last_width != decoder->sw_parser_ctx->width ||
        decoder->last_height != decoder->sw_parser_ctx->height) {
      if (reset(decoder) != 0) {
        fprintf(stdout, "reset failed\n");
        return -1;
      }
      if (!retried) {
        retried = true;
        goto _lable;
      }
    }
  }
  decoder->last_width = decoder->sw_parser_ctx->width;
  decoder->last_height = decoder->sw_parser_ctx->height;
  if (decoder->pkt->size > 0) {
    ret = do_decode(decoder, decoder->pkt, obj);
  }

  return ret;
}

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

#define LOG_MODULE "FFMPEG_RAM_DEC"
#include <log.h>

// #define CFG_PKG_TRACE

namespace {
typedef void (*RamDecodeCallback)(const void *obj, int width, int height,
                                  enum AVPixelFormat pixfmt,
                                  int linesize[AV_NUM_DATA_POINTERS],
                                  uint8_t *data[AV_NUM_DATA_POINTERS], int key);

class FFmpegRamDecoder {
public:
  AVCodecContext *c_ = NULL;
  AVBufferRef *hw_device_ctx_ = NULL;
  AVCodecParserContext *sw_parser_ctx_ = NULL;
  AVFrame *sw_frame_ = NULL;
  AVFrame *frame_ = NULL;
  AVPacket *pkt_ = NULL;
  bool hwaccel_ = true;

  char name_[128] = {0};
  AVHWDeviceType device_type_ = AV_HWDEVICE_TYPE_NONE;
  int thread_count_ = 1;
  RamDecodeCallback callback_ = NULL;

  bool ready_decode_ = false;
  int last_width_ = 0;
  int last_height_ = 0;

#ifdef CFG_PKG_TRACE
  int in_ = 0;
  int out_ = 0;
#endif

  FFmpegRamDecoder(const char *name, int device_type, int thread_count,
                   RamDecodeCallback callback) {
    snprintf(this->name_, sizeof(this->name_), "%s", name);
    this->device_type_ = (AVHWDeviceType)device_type;
    this->thread_count_ = thread_count;
    this->callback_ = callback;
  }

  ~FFmpegRamDecoder() {}

  void free_decoder() {
    if (frame_)
      av_frame_free(&frame_);
    if (pkt_)
      av_packet_free(&pkt_);
    if (sw_frame_)
      av_frame_free(&sw_frame_);
    if (sw_parser_ctx_)
      av_parser_close(sw_parser_ctx_);
    if (c_)
      avcodec_free_context(&c_);
    if (hw_device_ctx_)
      av_buffer_unref(&hw_device_ctx_);

    frame_ = NULL;
    pkt_ = NULL;
    sw_frame_ = NULL;
    sw_parser_ctx_ = NULL;
    c_ = NULL;
    hw_device_ctx_ = NULL;
    ready_decode_ = false;
  }
  int reset() {
    free_decoder();
    const AVCodec *codec = NULL;
    hwaccel_ = device_type_ != AV_HWDEVICE_TYPE_NONE;
    int ret;
    if (!(codec = avcodec_find_decoder_by_name(name_))) {
      LOG_ERROR("avcodec_find_decoder_by_name " + name_ + " failed");
      return -1;
    }
    if (!(c_ = avcodec_alloc_context3(codec))) {
      LOG_ERROR("Could not allocate video codec context");
      return -1;
    }

    c_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    c_->thread_count = thread_count_;
    c_->thread_type = FF_THREAD_SLICE;

    if (strcmp(name_, "h264_qsv") == 0 || strcmp(name_, "hevc_qsv") == 0) {
      if ((ret = av_opt_set(c_->priv_data, "async_depth", "1", 0)) < 0) {
        LOG_ERROR("qsv set opt async_depth 1 failed");
        return -1;
      }
      // https://github.com/FFmpeg/FFmpeg/blob/c6364b711bad1fe2fbd90e5b2798f87080ddf5ea/libavcodec/qsvdec.c#L932
      // for disable warning
      c_->pkt_timebase = av_make_q(1, 30);
    }

    ret = av_hwdevice_ctx_create(&hw_device_ctx_, device_type_, NULL, NULL, 0);
    if (ret < 0) {
      LOG_ERROR("av_hwdevice_ctx_create failed, ret = " + std::to_string(ret));
      return -1;
    }
    c_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
    if (!(sw_frame_ = av_frame_alloc())) {
      LOG_ERROR("av_frame_alloc failed");
      return -1;
    }
    if (!(sw_parser_ctx_ = av_parser_init(codec->id))) {
      LOG_ERROR("av_parser_init failed");
      return -1;
    }
    sw_parser_ctx_->flags |= PARSER_FLAG_COMPLETE_FRAMES;

    if (!(pkt_ = av_packet_alloc())) {
      LOG_ERROR("av_packet_alloc failed");
      return -1;
    }

    if (!(frame_ = av_frame_alloc())) {
      LOG_ERROR("av_frame_alloc failed");
      return -1;
    }

    if ((ret = avcodec_open2(c_, codec, NULL)) != 0) {
      LOG_ERROR("avcodec_open2 failed, ret = " + std::to_string(ret));
      return -1;
    }

    last_width_ = 0;
    last_height_ = 0;
#ifdef CFG_PKG_TRACE
    in_ = 0;
    out_ = 0;
#endif
    ready_decode_ = true;

    return 0;
  }

  int decode(const uint8_t *data, int length, const void *obj) {
    int ret = -1;
    bool retried = false;
#ifdef CFG_PKG_TRACE
    in_++;
    LOG_DEBUG("delay DI: in:" + in_ + " out:" + out_);
#endif

    if (!data || !length) {
      LOG_ERROR("illegal decode parameter");
      return -1;
    }
    if (!ready_decode_) {
      LOG_ERROR("not ready decode");
      return -1;
    }

  _lable:
    ret = av_parser_parse2(sw_parser_ctx_, c_, &pkt_->data, &pkt_->size, data,
                           length, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (ret < 0) {
      LOG_ERROR("av_parser_parse2 failed, ret = " + std::to_string(ret));
      return ret;
    }
    if (last_width_ != 0 && last_height_ != 0) {
      if (last_width_ != sw_parser_ctx_->width ||
          last_height_ != sw_parser_ctx_->height) {
        if (reset() != 0) {
          LOG_ERROR("reset failed");
          return -1;
        }
        if (!retried) {
          retried = true;
          goto _lable;
        }
      }
    }
    last_width_ = sw_parser_ctx_->width;
    last_height_ = sw_parser_ctx_->height;
    if (pkt_->size > 0) {
      ret = do_decode(obj);
    }

    return ret;
  }

private:
  int do_decode(const void *obj) {
    int ret;
    AVFrame *tmp_frame = NULL;
    bool decoded = false;

    ret = avcodec_send_packet(c_, pkt_);
    if (ret < 0) {
      LOG_ERROR("avcodec_send_packet failed, ret = " + std::to_string(ret));
      return ret;
    }

    while (ret >= 0) {
      if ((ret = avcodec_receive_frame(c_, frame_)) != 0) {
        if (ret != AVERROR(EAGAIN)) {
          LOG_ERROR("avcodec_receive_frame failed, ret = " +
                    std::to_string(ret));
        }
        goto _exit;
      }

      if (hwaccel_) {
        if (!frame_->hw_frames_ctx) {
          LOG_ERROR("hw_frames_ctx is NULL");
          goto _exit;
        }
        if ((ret = av_hwframe_transfer_data(sw_frame_, frame_, 0)) < 0) {
          LOG_ERROR("av_hwframe_transfer_data failed, ret = " +
                    std::to_string(ret));
          goto _exit;
        }

        tmp_frame = sw_frame_;
      } else {
        tmp_frame = frame_;
      }
      decoded = true;
#ifdef CFG_PKG_TRACE
      out_++;
      LOG_DEBUG("delay DO: in:" + in_ + " out:" + out_);
#endif
      callback_(obj, sw_parser_ctx_->width, sw_parser_ctx_->height,
                (AVPixelFormat)tmp_frame->format, tmp_frame->linesize,
                tmp_frame->data, tmp_frame->key_frame);
    }
  _exit:
    av_packet_unref(pkt_);
    return decoded ? 0 : -1;
  }
};

} // namespace

extern "C" void ffmpeg_ram_free_decoder(FFmpegRamDecoder *decoder) {
  try {
    if (!decoder)
      return;
    decoder->free_decoder();
    delete decoder;
    decoder = NULL;
  } catch (const std::exception &e) {
    LOG_ERROR("ffmpeg_ram_free_decoder exception:" + e.what());
  }
}

extern "C" FFmpegRamDecoder *
ffmpeg_ram_new_decoder(const char *name, int device_type, int thread_count,
                       RamDecodeCallback callback) {
  FFmpegRamDecoder *decoder = NULL;
  try {
    decoder = new FFmpegRamDecoder(name, device_type, thread_count, callback);
    if (decoder) {
      if (decoder->reset() == 0) {
        return decoder;
      }
    }
  } catch (std::exception &e) {
    LOG_ERROR("new decoder exception:" + e.what());
  }
  if (decoder) {
    decoder->free_decoder();
    delete decoder;
    decoder = NULL;
  }
  return NULL;
}

extern "C" int ffmpeg_ram_decode(FFmpegRamDecoder *decoder, const uint8_t *data,
                                 int length, const void *obj) {
  try {
    return decoder->decode(data, length, obj);
  } catch (const std::exception &e) {
    LOG_ERROR("ffmpeg_ram_decode exception:" + e.what());
  }
  return -1;
}

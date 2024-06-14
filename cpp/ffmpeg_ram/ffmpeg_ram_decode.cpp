// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/hw_decode.c
// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/decode_video.c

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
// #include <libavcodec/jni.h>
#include <libavcodec/packet.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <memory>
#include <stdbool.h>

#define LOG_MODULE "FFMPEG_RAM_DEC"
#include <log.h>

#ifdef _WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif

#include "common.h"
#include "system.h"

// #define CFG_PKG_TRACE

// extern "C" void *rustdesk_get_java_vm();

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

  std::string name_;
  AVHWDeviceType device_type_ = AV_HWDEVICE_TYPE_NONE;
  int thread_count_ = 1;
  RamDecodeCallback callback_ = NULL;
  DataFormat data_format_;

  bool ready_decode_ = false;
  int last_width_ = 0;
  int last_height_ = 0;
  // int mc_width = 600;
  // int mc_height = 800;
  bool mc_codec_try_opened = false;
  uint8_t *extradata_ = NULL;

#ifdef CFG_PKG_TRACE
  int in_ = 0;
  int out_ = 0;
#endif

  FFmpegRamDecoder(const char *name, int device_type, int thread_count,
                   RamDecodeCallback callback) {
    this->name_ = name;
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
    if (extradata_)
      av_free((void *)extradata_);

    frame_ = NULL;
    pkt_ = NULL;
    sw_frame_ = NULL;
    sw_parser_ctx_ = NULL;
    c_ = NULL;
    hw_device_ctx_ = NULL;
    ready_decode_ = false;
    extradata_ = NULL;
  }
  int reset() {
    if (name_.find("h264") != std::string::npos) {
      data_format_ = DataFormat::H264;
    } else if (name_.find("hevc") != std::string::npos) {
      data_format_ = DataFormat::H265;
    } else {
      LOG_ERROR("unsupported data format:" + name_);
      return -1;
    }
    free_decoder();
    const AVCodec *codec = NULL;
    hwaccel_ = device_type_ != AV_HWDEVICE_TYPE_NONE;
    int ret;
    if (!(codec = avcodec_find_decoder_by_name(name_.c_str()))) {
      LOG_ERROR("avcodec_find_decoder_by_name " + name_ + " failed");
      return -1;
    }
    if (!(c_ = avcodec_alloc_context3(codec))) {
      LOG_ERROR("Could not allocate video codec context");
      return -1;
    }

    c_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    c_->thread_count =
        device_type_ != AV_HWDEVICE_TYPE_NONE ? 1 : thread_count_;
    c_->thread_type = FF_THREAD_SLICE;

    if (name_.find("qsv") != std::string::npos) {
      if ((ret = av_opt_set(c_->priv_data, "async_depth", "1", 0)) < 0) {
        LOG_ERROR("qsv set opt async_depth 1 failed");
        return -1;
      }
      // https://github.com/FFmpeg/FFmpeg/blob/c6364b711bad1fe2fbd90e5b2798f87080ddf5ea/libavcodec/qsvdec.c#L932
      // for disable warning
      c_->pkt_timebase = av_make_q(1, 30);
    }

    if (name_.find("mediacodec") != std::string::npos) {
      // if ((ret = av_opt_set_int(c_->priv_data, "ndk_codec", 0, 0)) < 0) {
      //   LOG_ERROR("qsv set opt ndk_codec 0 failed");
      //   return -1;
      // }
      // av_jni_set_java_vm(rustdesk_get_java_vm(), nullptr);
    }

    // if (name_.find("mediacodec") != std::string::npos) {
    //   c_->width = mc_width;
    //   c_->height = mc_height;
    // }

    if (hwaccel_) {
      ret =
          av_hwdevice_ctx_create(&hw_device_ctx_, device_type_, NULL, NULL, 0);
      if (ret < 0) {
        LOG_ERROR("av_hwdevice_ctx_create failed, ret = " + av_err2str(ret));
        return -1;
      }
      c_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
      if (!check_support()) {
        LOG_ERROR("check_support failed");
        return -1;
      }
      if (!(sw_frame_ = av_frame_alloc())) {
        LOG_ERROR("av_frame_alloc failed");
        return -1;
      }
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

    if (name_.find("mediacodec") == std::string::npos) {
      if ((ret = avcodec_open2(c_, codec, NULL)) != 0) {
        LOG_ERROR("avcodec_open2 failed, ret = " + av_err2str(ret));
        return -1;
      }
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
    if (name_.find("mediacodec") != std::string::npos) {
      if (!mc_codec_try_opened) {
        mc_codec_try_opened = true;
        ready_decode_ = false;
        ret = av_parser_parse2(sw_parser_ctx_, c_, &pkt_->data, &pkt_->size,
                               data, length, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
          LOG_ERROR("av_parser_parse2 failed, ret = " + av_err2str(ret));
          return ret;
        }
        c_->width = sw_parser_ctx_->width;
        c_->height = sw_parser_ctx_->height;
        // https://stackoverflow.com/questions/29790334/how-to-fill-extradata-field-of-avcodeccontext-with-sps-and-pps-data
        // extradata_ = (uint8_t *)av_malloc(length);
        // c_->extradata = extradata_;
        // c_->extradata_size = length;
        int extradata_size = 0;
        if (!extract_extradata(c_, pkt_, &extradata_, &extradata_size)) {
          LOG_ERROR("extract_extradata failed");
          return -1;
        }
        c_->extradata = extradata_;
        c_->extradata_size = extradata_size;
        if ((ret = avcodec_open2(c_, NULL, NULL)) != 0) {
          LOG_ERROR("avcodec_open2 failed, ret = " + av_err2str(ret));
          return -1;
        }
        ready_decode_ = true;
      }
    }

  _lable:
    ret = av_parser_parse2(sw_parser_ctx_, c_, &pkt_->data, &pkt_->size, data,
                           length, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (ret < 0) {
      LOG_ERROR("av_parser_parse2 failed, ret = " + av_err2str(ret));
      return ret;
    }
    // if (name_.find("mediacodec") != std::string::npos) {
    //   if (sw_parser_ctx_->width != mc_width ||
    //       sw_parser_ctx_->height != mc_height) {
    //     if (reset() != 0) {
    //       LOG_ERROR("reset failed");
    //       return -1;
    //     }
    //     if (!retried) {
    //       retried = true;
    //       goto _lable;
    //     }
    //   }
    // }
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
      LOG_INFO("pkt size:" + std::to_string(pkt_->size) +
               ", length:" + std::to_string(length) +
               "packet:" + std::to_string((long)pkt_->data) +
               "data:" + std::to_string((long)data) +
               "width:" + std::to_string(sw_parser_ctx_->width) +
               "height:" + std::to_string(sw_parser_ctx_->height) +
               "extradata:" + std::to_string((long)c_->extradata) +
               "extradata_size:" + std::to_string(c_->extradata_size));
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
      LOG_ERROR("avcodec_send_packet failed, ret = " + av_err2str(ret));
      return ret;
    }

    while (ret >= 0) {
      if ((ret = avcodec_receive_frame(c_, frame_)) != 0) {
        // if (ret != AVERROR(EAGAIN)) {
        LOG_ERROR("avcodec_receive_frame failed, ret = " + av_err2str(ret));
        // }
        goto _exit;
      }

      if (hwaccel_) {
        if (!frame_->hw_frames_ctx) {
          LOG_ERROR("hw_frames_ctx is NULL");
          goto _exit;
        }
        if ((ret = av_hwframe_transfer_data(sw_frame_, frame_, 0)) < 0) {
          LOG_ERROR("av_hwframe_transfer_data failed, ret = " +
                    av_err2str(ret));
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
#if FF_API_FRAME_KEY
      int key_frame = frame_->flags & AV_FRAME_FLAG_KEY;
#else
      int key_frame = frame_->key_frame;
#endif

      callback_(obj, sw_parser_ctx_->width, sw_parser_ctx_->height,
                (AVPixelFormat)tmp_frame->format, tmp_frame->linesize,
                tmp_frame->data, key_frame);
    }
  _exit:
    av_packet_unref(pkt_);
    return decoded ? 0 : -1;
  }

  bool check_support() {
#ifdef _WIN32
    if (device_type_ == AV_HWDEVICE_TYPE_D3D11VA) {
      if (!c_->hw_device_ctx) {
        LOG_ERROR("hw_device_ctx is NULL");
        return false;
      }
      AVHWDeviceContext *deviceContext =
          (AVHWDeviceContext *)hw_device_ctx_->data;
      if (!deviceContext) {
        LOG_ERROR("deviceContext is NULL");
        return false;
      }
      AVD3D11VADeviceContext *d3d11vaDeviceContext =
          (AVD3D11VADeviceContext *)deviceContext->hwctx;
      if (!d3d11vaDeviceContext) {
        LOG_ERROR("d3d11vaDeviceContext is NULL");
        return false;
      }
      ID3D11Device *device = d3d11vaDeviceContext->device;
      if (!device) {
        LOG_ERROR("device is NULL");
        return false;
      }
      std::unique_ptr<NativeDevice> native_ = std::make_unique<NativeDevice>();
      if (!native_) {
        LOG_ERROR("Failed to create native device");
        return false;
      }
      if (!native_->Init(0, (ID3D11Device *)device, 0)) {
        LOG_ERROR("Failed to init native device");
        return false;
      }
      if (!native_->support_decode(data_format_)) {
        LOG_ERROR("Failed to check support " + name_);
        return false;
      }
      return true;
    } else {
      return true;
    }
#else
    return true;
#endif
  }

  bool extract_extradata(AVCodecContext *pCodecCtx, AVPacket *packet,
                         uint8_t **extradata_dest, int *extradata_size_dest) {
    const AVBitStreamFilter *bsf;
    int ret;
    if ((bsf = av_bsf_get_by_name("extract_extradata")) == NULL) {
      LOG_ERROR("failed to get extract_extradata bsf");
      return false;
    }
    LOG_DEBUG("found bsf");

    AVBSFContext *bsf_context;
    if ((ret = av_bsf_alloc(bsf, &bsf_context)) < 0) {
      LOG_ERROR("failed to alloc bsf context");
      return false;
    }

    LOG_DEBUG("alloced bsf context");

    if ((ret = avcodec_parameters_from_context(bsf_context->par_in,
                                               pCodecCtx)) < 0) {
      LOG_ERROR("failed to copy parameters from context\n");
      av_bsf_free(&bsf_context);
      return false;
    }

    LOG_DEBUG("copied bsf params");

    if ((ret = av_bsf_init(bsf_context)) < 0) {
      LOG_ERROR("failed to init bsf context");
      av_bsf_free(&bsf_context);
      return false;
    }

    LOG_DEBUG("initialized bsf context");

    AVPacket *packet_ref = av_packet_alloc();
    if (av_packet_ref(packet_ref, packet) < 0) {
      LOG_ERROR("failed to ref packet\n");
      av_bsf_free(&bsf_context);
      return false;
    }

    // make sure refs are used corectly
    // this probably resests packet
    if ((ret = av_bsf_send_packet(bsf_context, packet_ref)) < 0) {
      LOG_ERROR("failed to send packet to bsf\n");
      av_packet_unref(packet_ref);
      av_bsf_free(&bsf_context);
      return false;
    }

    LOG_DEBUG("sent packet to bsf");

    bool done = 0;

    while (ret >= 0 && !done) //! h->decoder_ctx->extradata)
    {
      size_t extradata_size;
      uint8_t *extradata;

      ret = av_bsf_receive_packet(bsf_context, packet_ref);
      if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
          LOG_ERROR("bsf error, not eagain or eof\n");
          return 0;
        }
        continue;
      }

      extradata = av_packet_get_side_data(packet_ref, AV_PKT_DATA_NEW_EXTRADATA,
                                          &extradata_size);

      if (extradata) {
        LOG_DEBUG("got extradata, size: " + std::to_string(extradata_size));
        done = true;
        *extradata_dest = (uint8_t *)av_mallocz(extradata_size +
                                                AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(*extradata_dest, extradata, extradata_size);
        *extradata_size_dest = extradata_size;
        av_packet_unref(packet_ref);
      }
    }

    av_packet_free(&packet_ref);
    av_bsf_free(&bsf_context);

    return done;
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

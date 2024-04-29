extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

#include <libavutil/hwcontext_d3d11va.h>

#include <memory>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "FFMPEG_VRAM_ENC"
#include <log.h>

namespace {
enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateContorl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

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
  int ret = -1;

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

void lockContext(void *lock_ctx);
void unlockContext(void *lock_ctx);

class FFmpegVRamEncoder {
public:
  AVCodecContext *c_ = NULL;
  AVBufferRef *hw_device_ctx_ = NULL;
  AVFrame *frame_ = NULL;
  AVPacket *pkt_ = NULL;
  char name_[128] = {0};
  std::unique_ptr<NativeDevice> native_ = nullptr;
  ID3D11Device *d3d11Device_ = NULL;
  ID3D11DeviceContext *d3d11DeviceContext_ = NULL;

  void *handle_ = nullptr;
  int64_t luid_;
  API api_;
  DataFormat dataFormat_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t kbs_;
  int32_t framerate_;
  int32_t gop_;

  const int align_ = 0;
  const AVHWDeviceType device_type_ = AV_HWDEVICE_TYPE_D3D11VA;
  const AVPixelFormat hw_pixfmt_ = AV_PIX_FMT_D3D11;
  const AVPixelFormat sw_pixfmt_ = AV_PIX_FMT_NV12;
  const bool full_range_ = false;
  const bool bt709_ = false;

  FFmpegVRamEncoder(void *handle, int64_t luid, API api, DataFormat dataFormat,
                    int32_t width, int32_t height, int32_t kbs,
                    int32_t framerate, int32_t gop) {
    handle_ = handle;
    luid_ = luid;
    api_ = api;
    dataFormat_ = dataFormat;
    width_ = width;
    height_ = height;
    kbs_ = kbs;
    framerate_ = framerate;
    gop_ = gop;
  }

  ~FFmpegVRamEncoder() {}

  bool init() {
    const AVCodec *codec = NULL;
    int ret;

    native_ = std::make_unique<NativeDevice>();
    if (!native_->Init(luid_, (ID3D11Device *)handle_)) {
      LOG_ERROR("NativeDevice init failed");
      return false;
    }
    d3d11Device_ = native_->device_.Get();
    d3d11Device_->AddRef();
    d3d11DeviceContext_ = native_->context_.Get();
    d3d11DeviceContext_->AddRef();

    AdapterVendor vendor = native_->GetVendor();
    if (ADAPTER_VENDOR_NVIDIA == vendor) {
      if (dataFormat_ == H264) {
        snprintf(name_, sizeof(name_), "h264_nvenc");
      } else if (dataFormat_ == H265) {
        snprintf(name_, sizeof(name_), "hevc_nvenc");
      } else {
        LOG_ERROR("Unsupported data format: " + std::to_string(dataFormat_));
        return false;
      }
    } else if (ADAPTER_VENDOR_AMD == vendor) {
      if (dataFormat_ == H264) {
        snprintf(name_, sizeof(name_), "h264_amf");
      } else if (dataFormat_ == H265) {
        snprintf(name_, sizeof(name_), "hevc_amf");
      } else {
        LOG_ERROR("Unsupported data format: " + std::to_string(dataFormat_));
        return false;
      }
    } else if (ADAPTER_VENDOR_INTEL == vendor) {
      if (dataFormat_ == H264) {
        snprintf(name_, sizeof(name_), "h264_qsv");
      } else if (dataFormat_ == H265) {
        snprintf(name_, sizeof(name_), "hevc_qsv");
      } else {
        LOG_ERROR("Unsupported data format: " + std::to_string(dataFormat_));
        return false;
      }
    } else {
      LOG_ERROR("Unsupported vendor: " + std::to_string(vendor));
      return false;
    }
    LOG_INFO("FFmpeg vram encoder name: " + std::string(name_));
    if (!(codec = avcodec_find_encoder_by_name(name_))) {
      LOG_ERROR("Codec " + name_ + " not found");
      return false;
    }

    if (!(c_ = avcodec_alloc_context3(codec))) {
      LOG_ERROR("Could not allocate video codec context");
      return false;
    }

    /* resolution must be a multiple of two */
    c_->width = width_;
    c_->height = height_;
    c_->pix_fmt = hw_pixfmt_;
    c_->sw_pix_fmt = sw_pixfmt_;
    c_->has_b_frames = 0;
    c_->max_b_frames = 0;
    c_->gop_size = gop_;
    // https://github.com/FFmpeg/FFmpeg/blob/415f012359364a77e8394436f222b74a8641a3ee/libavcodec/encode.c#L581
    c_->bit_rate = kbs_ * 1000;
    if (strcmp(name_, "h264_qsv") == 0 || strcmp(name_, "hevc_qsv") == 0) {
      c_->rc_max_rate = c_->bit_rate;
    }
    /* frames per second */
    c_->time_base = av_make_q(1, framerate_);
    c_->framerate = av_inv_q(c_->time_base);
    c_->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
    c_->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // https://github.com/obsproject/obs-studio/blob/3cc7dc0e7cf8b01081dc23e432115f7efd0c8877/plugins/obs-ffmpeg/obs-ffmpeg-mux.c#L160
    c_->color_range = AVCOL_RANGE_MPEG;
    c_->colorspace = AVCOL_SPC_SMPTE170M;
    c_->color_primaries = AVCOL_PRI_SMPTE170M;
    c_->color_trc = AVCOL_TRC_SMPTE170M;

    c_->slices = 1;
    c_->thread_type = FF_THREAD_SLICE;
    c_->thread_count = c_->slices;

    if (set_lantency_free(c_->priv_data, name_) < 0) {
      return false;
    }
    set_quality(c_->priv_data, name_, Quality_Medium);
    set_rate_control(c_->priv_data, name_, RC_CBR);
    if (dataFormat_ == H264) {
      c_->profile = FF_PROFILE_H264_HIGH;
    } else if (dataFormat_ == H265) {
      c_->profile = FF_PROFILE_HEVC_MAIN;
    }

    // av_hwdevice_ctx_create_derived
    hw_device_ctx_ = av_hwdevice_ctx_alloc(device_type_);
    if (!hw_device_ctx_) {
      LOG_ERROR("av_hwdevice_ctx_create failed");
      return false;
    }

    AVHWDeviceContext *deviceContext =
        (AVHWDeviceContext *)hw_device_ctx_->data;
    AVD3D11VADeviceContext *d3d11vaDeviceContext =
        (AVD3D11VADeviceContext *)deviceContext->hwctx;
    d3d11vaDeviceContext->device = d3d11Device_;
    d3d11vaDeviceContext->device_context = d3d11DeviceContext_;
    d3d11vaDeviceContext->lock = lockContext;
    d3d11vaDeviceContext->unlock = unlockContext;
    d3d11vaDeviceContext->lock_ctx = this;
    ret = av_hwdevice_ctx_init(hw_device_ctx_);
    if (ret < 0) {
      LOG_ERROR("av_hwdevice_ctx_init failed, ret = " + std::to_string(ret));
      return false;
    }
    c_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
    if (!set_hwframe_ctx()) {
      return false;
    }

    if (!(pkt_ = av_packet_alloc())) {
      LOG_ERROR("Could not allocate video packet");
      return false;
    }

    if ((ret = avcodec_open2(c_, codec, NULL)) < 0) {
      LOG_ERROR("avcodec_open2 failed, ret = " + std::to_string((ret)) +
                ", name: " + name_);
      return false;
    }

    if (!(frame_ = av_frame_alloc())) {
      LOG_ERROR("Could not allocate video frame");
      return false;
    }
    frame_->format = c_->pix_fmt;
    frame_->width = c_->width;
    frame_->height = c_->height;
    frame_->color_range = c_->color_range;
    frame_->color_primaries = c_->color_primaries;
    frame_->color_trc = c_->color_trc;
    frame_->colorspace = c_->colorspace;
    frame_->chroma_location = c_->chroma_sample_location;

    if ((ret = av_hwframe_get_buffer(c_->hw_frames_ctx, frame_, 0)) < 0) {
      LOG_ERROR("av_frame_get_buffer failed, ret = " + std::to_string((ret)));
      return false;
    }

    return true;
  }

  int encode(void *texture, EncodeCallback callback, void *obj) {
    int ret;

    if ((ret = av_frame_make_writable(frame_)) != 0) {
      LOG_ERROR("av_frame_make_writable failed, ret = " +
                std::to_string((ret)));
      return ret;
    }
    if (!convert(texture))
      return -1;

    return do_encode(callback, obj);
  }

  void destroy() {
    if (pkt_)
      av_packet_free(&pkt_);
    if (frame_)
      av_frame_free(&frame_);
    if (c_)
      avcodec_free_context(&c_);
    if (hw_device_ctx_) {
      av_buffer_unref(&hw_device_ctx_);
      // AVHWDeviceContext takes ownership of d3d11 object
      d3d11Device_ = nullptr;
      d3d11DeviceContext_ = nullptr;
    } else {
      SAFE_RELEASE(d3d11Device_);
      SAFE_RELEASE(d3d11DeviceContext_);
    }
  }

  int set_bitrate(int kbs) {
    if (strcmp(name_, "h264_nvenc") == 0 || strcmp(name_, "hevc_nvenc") == 0 ||
        strcmp(name_, "h264_amf") == 0 || strcmp(name_, "hevc_amf") == 0) {
      c_->bit_rate = kbs * 1000;
      return 0;
    }
    LOG_ERROR("ffmpeg_ram_set_bitrate " + name_ +
              " does not implement bitrate change");
    return -1;
  }

private:
  int do_encode(EncodeCallback callback, const void *obj) {
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
      if (callback)
        callback(pkt_->data, pkt_->size, pkt_->flags & AV_PKT_FLAG_KEY, obj);
    }
  _exit:
    av_packet_unref(pkt_);
    return encoded ? 0 : -1;
  }

  bool convert(void *texture) {
    if (frame_->format == AV_PIX_FMT_D3D11) {
      ID3D11Texture2D *texture2D = (ID3D11Texture2D *)frame_->data[0];
      D3D11_TEXTURE2D_DESC desc;
      texture2D->GetDesc(&desc);
      if (desc.Width != width_ || desc.Height != height_ ||
          desc.Format != DXGI_FORMAT_NV12) {
        LOG_ERROR(
            "convert: texture size mismatch, " + std::to_string(desc.Width) +
            "x" + std::to_string(desc.Height) +
            " != " + std::to_string(width_) + "x" + std::to_string(height_) +
            " or " + std::to_string(desc.Format) +
            " != " + std::to_string(DXGI_FORMAT_NV12));
        return false;
      }
      DXGI_COLOR_SPACE_TYPE colorSpace_in =
          DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      DXGI_COLOR_SPACE_TYPE colorSpace_out;
      if (bt709_) {
        if (full_range_) {
          colorSpace_out = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709;
        } else {
          colorSpace_out = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
        }
      } else {
        if (full_range_) {
          colorSpace_out = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;
        } else {
          colorSpace_out = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
        }
      }
      if (!native_->BgraToNv12((ID3D11Texture2D *)texture, texture2D, width_,
                               height_, colorSpace_in, colorSpace_out)) {
        LOG_ERROR("convert: BgraToNv12 failed");
        return false;
      }
      return true;
    } else {
      LOG_ERROR("convert: unsupported format, " +
                std::to_string(frame_->format));
      return false;
    }
  }

  bool set_hwframe_ctx() {
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;
    bool ret = true;

    if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_))) {
      LOG_ERROR("av_hwframe_ctx_alloc failed.");
      return false;
    }
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format = hw_pixfmt_;
    frames_ctx->sw_format = sw_pixfmt_;
    frames_ctx->width = width_;
    frames_ctx->height = height_;
    frames_ctx->initial_pool_size = 1;
    AVD3D11VAFramesContext *frames_hwctx =
        (AVD3D11VAFramesContext *)frames_ctx->hwctx;
    frames_hwctx->BindFlags = D3D11_BIND_RENDER_TARGET;
    frames_hwctx->MiscFlags = 0;
    if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
      LOG_ERROR("av_hwframe_ctx_init failed.");
      av_buffer_unref(&hw_frames_ref);
      return false;
    }
    c_->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (!c_->hw_frames_ctx) {
      LOG_ERROR("av_buffer_ref failed");
      ret = false;
    }
    av_buffer_unref(&hw_frames_ref);

    return ret;
  }
};

void lockContext(void *lock_ctx) { (void)lock_ctx; }

void unlockContext(void *lock_ctx) { (void)lock_ctx; }

} // namespace

extern "C" {
FFmpegVRamEncoder *ffmpeg_vram_new_encoder(void *handle, int64_t luid, API api,
                                           DataFormat dataFormat, int32_t width,
                                           int32_t height, int32_t kbs,
                                           int32_t framerate, int32_t gop) {
  FFmpegVRamEncoder *encoder = NULL;
  try {
    encoder = new FFmpegVRamEncoder(handle, luid, api, dataFormat, width,
                                    height, kbs, framerate, gop);
    if (encoder) {
      if (encoder->init()) {
        return encoder;
      }
    }
  } catch (const std::exception &e) {
    LOG_ERROR("new FFmpegVRamEncoder failed, " + std::string(e.what()));
  }
  if (encoder) {
    encoder->destroy();
    delete encoder;
    encoder = NULL;
  }
  return NULL;
}

int ffmpeg_vram_encode(FFmpegVRamEncoder *encoder, void *texture,
                       EncodeCallback callback, void *obj) {
  try {
    return encoder->encode(texture, callback, obj);
  } catch (const std::exception &e) {
    LOG_ERROR("ffmpeg_vram_encode failed, " + std::string(e.what()));
  }
  return -1;
}

void ffmpeg_vram_destroy_encoder(FFmpegVRamEncoder *encoder) {
  try {
    if (!encoder)
      return;
    encoder->destroy();
    delete encoder;
    encoder = NULL;
  } catch (const std::exception &e) {
    LOG_ERROR("free encoder failed, " + std::string(e.what()));
  }
}

int ffmpeg_vram_set_bitrate(FFmpegVRamEncoder *encoder, int kbs) {
  try {
    return encoder->set_bitrate(kbs);
  } catch (const std::exception &e) {
    LOG_ERROR("ffmpeg_ram_set_bitrate failed, " + std::string(e.what()));
  }
  return -1;
}

int ffmpeg_vram_set_framerate(void *encoder, int32_t framerate) { return -1; }

int ffmpeg_vram_test_encode(void *outDescs, int32_t maxDescNum,
                            int32_t *outDescNum, API api, DataFormat dataFormat,
                            int32_t width, int32_t height, int32_t kbs,
                            int32_t framerate, int32_t gop) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    int count = 0;
    AdapterVendor vendors[] = {ADAPTER_VENDOR_INTEL, ADAPTER_VENDOR_NVIDIA,
                               ADAPTER_VENDOR_AMD};
    for (auto vendor : vendors) {
      Adapters adapters;
      if (!adapters.Init(vendor))
        continue;
      for (auto &adapter : adapters.adapters_) {
        FFmpegVRamEncoder *e = (FFmpegVRamEncoder *)ffmpeg_vram_new_encoder(
            (void *)adapter.get()->device_.Get(), LUID(adapter.get()->desc1_),
            api, dataFormat, width, height, kbs, framerate, gop);
        if (!e)
          continue;
        if (e->native_->EnsureTexture(e->width_, e->height_)) {
          e->native_->next();
          if (ffmpeg_vram_encode(e, e->native_->GetCurrentTexture(), nullptr,
                                 nullptr) == 0) {
            AdapterDesc *desc = descs + count;
            desc->luid = LUID(adapter.get()->desc1_);
            count += 1;
          }
        }
        e->destroy();
        delete e;
        e = nullptr;
        if (count >= maxDescNum)
          break;
      }
      if (count >= maxDescNum)
        break;
    }
    *outDescNum = count;
    return 0;
  } catch (const std::exception &e) {
    LOG_ERROR("test failed: " + e.what());
  }
  return -1;
}

} // extern "C"

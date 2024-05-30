extern "C" {
#include <libavutil/opt.h>
}

#include "uitl.h"
#include <limits>
#include <log.h>
#include <map>
#include <string.h>
#include <vector>

#include "common.h"

#include "common.h"

namespace util {

void set_av_codec_ctx(AVCodecContext *c, const std::string &name, int gop,
                      int fps) {
  c->has_b_frames = 0;
  c->max_b_frames = 0;
  c->gop_size = gop < 0xFFFF ? gop
                : name.find("vaapi") != std::string::npos
                    ? std::numeric_limits<int16_t>::max()
                    : std::numeric_limits<int>::max();
  c->keyint_min = std::numeric_limits<int>::max();
  /* frames per second */
  c->time_base = av_make_q(1, fps);
  c->framerate = av_inv_q(c->time_base);
  c->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
  c->flags |= AV_CODEC_FLAG_CLOSED_GOP;
  c->flags2 |= AV_CODEC_FLAG2_FAST;
  c->slices = 1;
  c->thread_type = FF_THREAD_SLICE;
  c->thread_count = c->slices;

  // https://github.com/obsproject/obs-studio/blob/3cc7dc0e7cf8b01081dc23e432115f7efd0c8877/plugins/obs-ffmpeg/obs-ffmpeg-mux.c#L160
  c->color_range = AVCOL_RANGE_MPEG;
  c->colorspace = AVCOL_SPC_SMPTE170M;
  c->color_primaries = AVCOL_PRI_SMPTE170M;
  c->color_trc = AVCOL_TRC_SMPTE170M;

  if (name.find("h264") != std::string::npos) {
    c->profile = FF_PROFILE_H264_HIGH;
  } else if (name.find("hevc") != std::string::npos) {
    c->profile = FF_PROFILE_HEVC_MAIN;
  }

  if (name.find("qsv") != std::string::npos) {
    c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
  }
}

bool set_lantency_free(void *priv_data, const std::string &name) {
  int ret;

  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "delay", "0", 0)) < 0) {
      LOG_ERROR("nvenc set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("amf") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "query_timeout", "1000", 0)) < 0) {
      LOG_ERROR("amf set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("qsv") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR("qsv set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR("vaapi set_lantency_free failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool set_quality(void *priv_data, const std::string &name, int quality) {
  int ret = -1;

  if (name.find("nvenc") != std::string::npos) {
    switch (quality) {
    // p7 isn't zero lantency
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "preset", "p4", 0)) < 0) {
        LOG_ERROR("nvenc set opt preset p4 failed, ret = " + av_err2str(ret));
        return false;
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "preset", "p1", 0)) < 0) {
        LOG_ERROR("nvenc set opt preset p1 failed, ret = " + av_err2str(ret));
        return false;
      }
      break;
    default:
      break;
    }
  }
  if (name.find("amf") != std::string::npos) {
    switch (quality) {
    case Quality_High:
      if ((ret = av_opt_set(priv_data, "quality", "quality", 0)) < 0) {
        LOG_ERROR("amf set opt quality quality failed, ret = " +
                  av_err2str(ret));
        return false;
      }
      break;
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "quality", "balanced", 0)) < 0) {
        LOG_ERROR("amf set opt quality balanced failed, ret = " +
                  av_err2str(ret));
        return false;
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "quality", "speed", 0)) < 0) {
        LOG_ERROR("amf set opt quality speed failed, ret = " + av_err2str(ret));
        return false;
      }
      break;
    default:
      break;
    }
  }
  if (name.find("qsv") != std::string::npos) {
    switch (quality) {
    case Quality_High:
      if ((ret = av_opt_set(priv_data, "preset", "veryslow", 0)) < 0) {
        LOG_ERROR("qsv set opt preset veryslow failed, ret = " +
                  av_err2str(ret));
        return false;
      }
      break;
    case Quality_Medium:
      if ((ret = av_opt_set(priv_data, "preset", "medium", 0)) < 0) {
        LOG_ERROR("qsv set opt preset medium failed, ret = " + av_err2str(ret));
        return false;
      }
      break;
    case Quality_Low:
      if ((ret = av_opt_set(priv_data, "preset", "veryfast", 0)) < 0) {
        LOG_ERROR("qsv set opt preset veryfast failed, ret = " +
                  av_err2str(ret));
        return false;
      }
      break;
    default:
      break;
    }
  }
  if (name.find("mediacodec") != std::string::npos) {
    if (name.find("h264") != std::string::npos) {
      if ((ret = av_opt_set(priv_data, "level", "5.1", 0)) < 0) {
        LOG_ERROR("mediacodec set opt level 5.1 failed, ret = " +
                  av_err2str(ret));
        return false;
      }
    }
    if (name.find("hevc") != std::string::npos) {
      // https:en.wikipedia.org/wiki/High_Efficiency_Video_Coding_tiers_and_levels
      if ((ret = av_opt_set(priv_data, "level", "h5.1", 0)) < 0) {
        LOG_ERROR("mediacodec set opt level h5.1 failed, ret = " +
                  av_err2str(ret));
        return false;
      }
    }
  }
  return true;
}

struct CodecOptions {
  std::string codec_name;
  std::string option_name;
  std::map<int, std::string> rc_values;
};

bool set_rate_control(AVCodecContext *c, const std::string &name, int rc,
                      int kbs, int q, int fps) {
  std::vector<CodecOptions> codecs = {
      {"nvenc", "rc", {{RC_CBR, "cbr"}, {RC_VBR, "vbr"}}},
      {"amf", "rc", {{RC_CBR, "cbr"}, {RC_VBR, "vbr_latency"}}},
      {"mediacodec",
       "bitrate_mode",
       {{RC_CBR, "cbr"}, {RC_VBR, "vbr"}, {RC_CQ, "cq"}}}};

  if (kbs > 0) {
    c->bit_rate = kbs * 1000;
    if (RC_CBR == rc) {
      // c->rc_max_rate = c->bit_rate;
      // c->rc_min_rate = c->bit_rate;
      if (name.find("qsv") != std::string::npos) {
        c->bit_rate--; // cbr with vbr
      }
      if (name.find("nvenc") != std::string::npos ||
          name.find("amf") != std::string::npos) {
        // c->rc_buffer_size = c->bit_rate / fps;
      }
    }
  }

  for (const auto &codec : codecs) {
    if (name.find(codec.codec_name) != std::string::npos) {
      auto it = codec.rc_values.find(rc);
      if (it != codec.rc_values.end()) {
        int ret = av_opt_set(c->priv_data, codec.option_name.c_str(),
                             it->second.c_str(), 0);
        if (ret < 0) {
          LOG_ERROR(codec.codec_name + " set opt " + codec.option_name + " " +
                    it->second + " failed, ret = " + av_err2str(ret));
          return false;
        }
        if (name.find("mediacodec") != std::string::npos) {
          if (rc == RC_CQ) {
            if (q >= 0 && q <= 51) {
              c->global_quality = q;
            }
          }
        }
      }
      break;
    }
  }

  return true;
}
bool set_gpu(void *priv_data, const std::string &name, int gpu) {
  int ret;
  if (gpu < 0)
    return -1;
  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR("nvenc set gpu failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool force_hw(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("_mf") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "hw_encoding", 1, 0)) < 0) {
      LOG_ERROR("mediafoundation set hw_encoding failed, ret = " +
                av_err2str(ret));
      return false;
    }
  }
  return true;
}

struct IntegerOption {
  std::string option_name;
  int value;
};

bool set_options(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("qsv") != std::string::npos) {
    auto options = std::vector<IntegerOption>{
        // {"preset", 4},
        {"forced-idr", 1},
        // {"low_delay_brc", 1},
        // {"low_power", 1}, {"recovery_point_sei", 0}, {"pic_timing_sei", 0},
    };
    for (const auto &option : options) {
      if ((ret = av_opt_set_int(priv_data, option.option_name.c_str(),
                                option.value, 0)) < 0) {
        LOG_ERROR(name + " set " + option.option_name +
                  " failed, ret = " + av_err2str(ret));
      }
    }
    if (name.find("h264") != std::string::npos) {
      auto options264 = std::vector<IntegerOption>{
          // {"cavlc", 0},
          // {"vcm", 1},
          // {"max_dec_frame_buffering", 1},
          {"profile", 100}, // MFX_PROFILE_AVC_HIGH
      };
      for (const auto &option : options264) {
        if ((ret = av_opt_set_int(priv_data, option.option_name.c_str(),
                                  option.value, 0)) < 0) {
          LOG_ERROR(name + " set " + option.option_name +
                    " failed, ret = " + av_err2str(ret));
        }
      }
    }
    if (name.find("hevc") != std::string::npos) {
      auto options265 = std::vector<IntegerOption>{
          {"profile", 1}, //     MFX_PROFILE_HEVC_MAIN
      };
      for (const auto &option : options265) {
        if ((ret = av_opt_set_int(priv_data, option.option_name.c_str(),
                                  option.value, 0)) < 0) {
          LOG_ERROR(name + " set " + option.option_name +
                    " failed, ret = " + av_err2str(ret));
        }
      }
    }
  }

  if (name.find("nvenc") != std::string::npos) {
    auto options = std::vector<IntegerOption>{
        // {"forced-idr", 1},
        // {"zerolatency", 1},
        // {"preset", 12},   // P1, lowest quality
        // {"tune", 3},      // NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY
        // {"multipass", 1}, // NV_ENC_TWO_PASS_QUARTER_RESOLUTION
    };
    for (const auto &option : options) {
      if ((ret = av_opt_set_int(priv_data, option.option_name.c_str(),
                                option.value, 0)) < 0) {
        LOG_ERROR(name + " set " + option.option_name +
                  " failed, ret = " + av_err2str(ret));
      }
    }
    if (name.find("h264") != std::string::npos) {
      auto options = std::vector<IntegerOption>{
          // {"coder", 1},   // NV_ENC_H264_ENTROPY_CODING_MODE_CABAC
          // {"profile", 2}, // NV_ENC_H264_PROFILE_HIGH
      };
      for (const auto &option : options) {
        if ((ret = av_opt_set_int(priv_data, option.option_name.c_str(),
                                  option.value, 0)) < 0) {
          LOG_ERROR(name + " set " + option.option_name +
                    " failed, ret = " + av_err2str(ret));
        }
      }
    }
    if (name.find("hevc") != std::string::npos) {
      auto options = std::vector<IntegerOption>{
          // {"profile", 0}, // NV_ENC_HEVC_PROFILE_MAIN
      };
      for (const auto &option : options) {
        if ((ret = av_opt_set_int(priv_data, option.option_name.c_str(),
                                  option.value, 0)) < 0) {
          LOG_ERROR(name + " set " + option.option_name +
                    " failed, ret = " + av_err2str(ret));
        }
      }
    }
  }
  if (name.find("_mf") != std::string::npos) {
    // ff_eAVScenarioInfo_DisplayRemoting = 1
    if ((ret = av_opt_set_int(priv_data, "scenario", 1, 0)) < 0) {
      LOG_ERROR("mediafoundation set scenario failed, ret = " +
                av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "idr_interval",
                              std::numeric_limits<int>::max(), 0)) < 0) {
      LOG_ERROR("vaapi set idr_interval failed, ret = " + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs) {
  if (kbs > 0) {
    c->bit_rate = kbs * 1000;
    if (name.find("qsv") != std::string::npos) {
      c->rc_max_rate = c->bit_rate;
    }
  }
  return true;
}
} // namespace util
extern "C" {
#include <libavutil/opt.h>
}

#include "util.h"
#include <limits>
#include <map>
#include <string.h>
#include <vector>

#include "common.h"

#include "common.h"

#define LOG_MODULE "UTIL"
#include "log.h"

namespace util_encode {

int64_t calc_vbr_max_rate(int64_t bit_rate) {
  // Keep both relative and absolute headroom for VBR burst allocation.
  const int64_t by_ratio = (bit_rate * 3) / 2;
  const int64_t by_offset = bit_rate + 500000;
  return by_ratio > by_offset ? by_ratio : by_offset;
}

void set_av_codec_ctx(AVCodecContext *c, const std::string &name,
                      int gop, int fps) {
  c->has_b_frames = 0;
  c->max_b_frames = 0;
  if (gop > 0 && gop < std::numeric_limits<int16_t>::max()) {
    c->gop_size = gop;
  } else if (name.find("vaapi") != std::string::npos) {
    c->gop_size = std::numeric_limits<int16_t>::max();
  } else if (name.find("qsv") != std::string::npos) {
    c->gop_size = std::numeric_limits<uint16_t>::max();
  } else {
    c->gop_size = std::numeric_limits<int>::max();
  }
  c->keyint_min = std::numeric_limits<int>::max();
  /* frames per second */
  c->time_base = av_make_q(1, 1000);
  c->framerate = av_make_q(fps, 1);
  c->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
  c->flags |= AV_CODEC_FLAG_LOW_DELAY;
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
}

bool set_lantency_free(void *priv_data, const std::string &name) {
  int ret;

  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "delay", "0", 0)) < 0) {
      LOG_ERROR(std::string("nvenc set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("amf") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "query_timeout", "1000", 0)) < 0) {
      LOG_ERROR(std::string("amf set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("qsv") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR(std::string("qsv set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("vaapi") != std::string::npos) {
    if ((ret = av_opt_set(priv_data, "async_depth", "1", 0)) < 0) {
      LOG_ERROR(std::string("vaapi set_lantency_free failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  if (name.find("videotoolbox") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "realtime", 1, 0)) < 0) {
      LOG_ERROR(std::string("videotoolbox set realtime failed, ret = ") + av_err2str(ret));
      return false;
    }
    if ((ret = av_opt_set_int(priv_data, "prio_speed", 1, 0)) < 0) {
      LOG_ERROR(std::string("videotoolbox set prio_speed failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  return true;
}

static int clamp_qp(int qp) {
  if (qp < 0) return 0;
  if (qp > 51) return 51;
  return qp;
}

static int ensure_kbs(int kbs) {
  return kbs > 0 ? kbs : DEFAULT_KBS;
}

void sanitize_qp(int &qp, int &qp_min, int &qp_max) {
  if (qp <= 0) qp = DEFAULT_QP;
  if (qp_min <= 0) qp_min = DEFAULT_QP_MIN;
  if (qp_max <= 0) qp_max = DEFAULT_QP_MAX;
  qp = clamp_qp(qp);
  qp_min = clamp_qp(qp_min);
  qp_max = clamp_qp(qp_max);
  if (qp_min > qp) qp_min = qp;
  if (qp_max < qp) qp_max = qp;
}

static void apply_nvenc_qp(AVCodecContext *c, int qp) {
  av_opt_set(c->priv_data, "rc", "constqp", 0);
  av_opt_set_int(c->priv_data, "qp", qp, 0);
}

static void apply_amf_qp(AVCodecContext *c, const std::string &name,
                          int qp, int qp_min, int qp_max) {
  av_opt_set(c->priv_data, "rc", "cqp", 0);
  av_opt_set_int(c->priv_data, "vbaq", 0, 0);
  av_opt_set_int(c->priv_data, "qp_i", qp, 0);
  av_opt_set_int(c->priv_data, "qp_p", qp, 0);
  if (name.find("h264") != std::string::npos) {
    av_opt_set_int(c->priv_data, "qp_b", qp, 0);
  }
  if (qp_min > 0) c->qmin = qp_min;
  if (qp_max > 0) c->qmax = qp_max;
}

static void apply_qsv_qp(AVCodecContext *c, int qp, int qp_min, int qp_max) {
  c->global_quality = qp * FF_QP2LAMBDA;
  if (qp_min > 0) c->qmin = qp_min;
  if (qp_max > 0) c->qmax = qp_max;
}

static void set_cqp_common(AVCodecContext *c) {
  c->flags |= AV_CODEC_FLAG_QSCALE;
  c->bit_rate = 0;
  c->rc_max_rate = 0;
}

bool set_rate_control(AVCodecContext *c, const std::string &name, int rc,
                      int kbs, int qp, int qp_min, int qp_max) {
  int ret;

  if (name.find("qsv") != std::string::npos) {
    c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    if (rc == RC_CQP) {
      sanitize_qp(qp, qp_min, qp_max);
      set_cqp_common(c);
      apply_qsv_qp(c, qp, qp_min, qp_max);
    } else {
      kbs = ensure_kbs(kbs);
      c->bit_rate = kbs * 1000;
      if (rc == RC_VBR) {
        c->rc_max_rate = calc_vbr_max_rate(c->bit_rate);
      } else if (rc == RC_CBR) {
        c->rc_max_rate = c->bit_rate;
      }
    }
    struct QsvOpt {
      const char *name;
      int64_t value;
    };
    const QsvOpt qsv_opts[] = {
        {"preset", 5},
        {"scenario", 1},
        {"adaptive_i", 1},
        {"adaptive_b", 0},
        {"b_strategy", 0},
    };
    for (const auto &opt : qsv_opts) {
      ret = av_opt_set_int(c->priv_data, opt.name, opt.value, 0);
      if (ret < 0) {
        LOG_WARN(std::string("qsv set opt ") + opt.name + "=" +
                 std::to_string(opt.value) + " failed, ret = " + av_err2str(ret));
      }
    }
    if (rc != RC_CQP) {
      av_opt_set_int(c->priv_data, "mbbrc", 1, 0);
      // av_opt_set_int(c->priv_data, "extbrc", 1, 0); // extbrc causes bitrate spikes for h264, keep it disabled
    }
  } else if (name.find("nvenc") != std::string::npos) {
    if (rc == RC_CQP) {
      sanitize_qp(qp, qp_min, qp_max);
      set_cqp_common(c);
      apply_nvenc_qp(c, qp);
    } else {
      kbs = ensure_kbs(kbs);
      c->bit_rate = kbs * 1000;
      if (rc == RC_CBR) {
        av_opt_set(c->priv_data, "rc", "cbr", 0);
        c->rc_max_rate = c->bit_rate;
      } else if (rc == RC_VBR) {
        av_opt_set(c->priv_data, "rc", "vbr", 0);
        c->rc_max_rate = calc_vbr_max_rate(c->bit_rate);
      }
    }
  } else if (name.find("amf") != std::string::npos) {
    if (rc == RC_CQP) {
      sanitize_qp(qp, qp_min, qp_max);
      set_cqp_common(c);
      apply_amf_qp(c, name, qp, qp_min, qp_max);
    } else {
      kbs = ensure_kbs(kbs);
      c->bit_rate = kbs * 1000;
      if (rc == RC_CBR) {
        av_opt_set(c->priv_data, "rc", "hqcbr", 0);
        c->rc_max_rate = c->bit_rate;
        c->rc_buffer_size = c->bit_rate * 2;
      } else if (rc == RC_VBR) {
        av_opt_set(c->priv_data, "rc", "hqvbr", 0);
        c->rc_max_rate = calc_vbr_max_rate(c->bit_rate);
        c->rc_buffer_size = c->rc_max_rate * 2;
      }
      c->rc_initial_buffer_occupancy = c->rc_buffer_size;
      av_opt_set(c->priv_data, "quality", "quality", 0);
    }
  } else if (name.find("mediacodec") != std::string::npos) {
    kbs = ensure_kbs(kbs);
    c->bit_rate = kbs * 1000;
    if (rc == RC_CBR) {
      av_opt_set(c->priv_data, "bitrate_mode", "cbr", 0);
    } else if (rc == RC_VBR) {
      av_opt_set(c->priv_data, "bitrate_mode", "vbr", 0);
    }
  } else if (name.find("vaapi") != std::string::npos) {
    kbs = ensure_kbs(kbs);
    c->bit_rate = kbs * 1000;
    if ((ret = av_opt_set_int(c->priv_data, "idr_interval",
                              std::numeric_limits<int>::max(), 0)) < 0) {
      LOG_ERROR(std::string("vaapi set idr_interval failed, ret = ") + av_err2str(ret));
    }
  } else if (name.find("_mf") != std::string::npos) {
    kbs = ensure_kbs(kbs);
    c->bit_rate = kbs * 1000;
    if ((ret = av_opt_set_int(c->priv_data, "scenario", 1, 0)) < 0) {
      LOG_ERROR(std::string("mediafoundation set scenario failed, ret = ") +
                av_err2str(ret));
    }
  } else {
    // fallback for other codecs (videotoolbox, etc.)
    kbs = ensure_kbs(kbs);
    c->bit_rate = kbs * 1000;
  }

  return true;
}
bool set_gpu(void *priv_data, const std::string &name, int gpu) {
  int ret;
  if (gpu < 0)
    return -1;
  if (name.find("nvenc") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR(std::string("nvenc set gpu failed, ret = ") + av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool force_hw(void *priv_data, const std::string &name) {
  int ret;
  if (name.find("_mf") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "hw_encoding", 1, 0)) < 0) {
      LOG_ERROR(std::string("mediafoundation set hw_encoding failed, ret = ") +
                av_err2str(ret));
      return false;
    }
  }
  if (name.find("videotoolbox") != std::string::npos) {
    if ((ret = av_opt_set_int(priv_data, "allow_sw", 0, 0)) < 0) {
      LOG_ERROR(std::string("mediafoundation set allow_sw failed, ret = ") +
                av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool is_using_cqp(const std::string &name, AVCodecContext *c) {
  (void)name;
  return c && (c->flags & AV_CODEC_FLAG_QSCALE);
}

bool change_qp(AVCodecContext *c, const std::string &name, int qp, int qp_min,
               int qp_max) {
  if (!is_using_cqp(name, c)) return true;
  sanitize_qp(qp, qp_min, qp_max);
  if (name.find("nvenc") != std::string::npos) {
    av_opt_set_int(c->priv_data, "qp", qp, 0);
  } else if (name.find("amf") != std::string::npos) {
    av_opt_set_int(c->priv_data, "qp_i", qp, 0);
    av_opt_set_int(c->priv_data, "qp_p", qp, 0);
    if (name.find("h264") != std::string::npos) {
      av_opt_set_int(c->priv_data, "qp_b", qp, 0);
    }
    if (qp_min > 0) c->qmin = qp_min;
    if (qp_max > 0) c->qmax = qp_max;
  } else if (name.find("qsv") != std::string::npos) {
    apply_qsv_qp(c, qp, qp_min, qp_max);
  }
  return true;
}

bool change_bit_rate(AVCodecContext *c, const std::string &name, int rc, int kbs) {
  if (is_using_cqp(name, c)) return true;
  kbs = ensure_kbs(kbs);
  c->bit_rate = kbs * 1000;
  if (rc == RC_VBR) {
    c->rc_max_rate = calc_vbr_max_rate(c->bit_rate);
  } else {
    c->rc_max_rate = c->bit_rate;
  }
  if (name.find("amf") != std::string::npos) {
    c->rc_buffer_size = c->rc_max_rate * 2;
    c->rc_initial_buffer_occupancy = c->rc_buffer_size;
  }
  return true;
}

void vram_encode_test_callback(const uint8_t *data, int32_t len, int32_t key, const void *obj, int64_t pts) {
  (void)data;
  (void)len;
  (void)pts;
  if (obj) {
    int32_t *pkey = (int32_t *)obj;
    *pkey = key;
  }
}

} // namespace util_encode

namespace util_decode {

static bool g_flag_could_not_find_ref_with_poc = false;

bool has_flag_could_not_find_ref_with_poc() {
  bool v = g_flag_could_not_find_ref_with_poc;
  g_flag_could_not_find_ref_with_poc = false;
  return v;
}

} // namespace util_decode

extern "C" void hwcodec_set_flag_could_not_find_ref_with_poc() {
  util_decode::g_flag_could_not_find_ref_with_poc = true;
}
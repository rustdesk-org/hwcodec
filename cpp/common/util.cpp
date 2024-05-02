extern "C" {
#include <libavutil/opt.h>
}

#include "uitl.h"
#include <log.h>
#include <string.h>

namespace util {

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

int set_gpu(void *priv_data, const char *name, int gpu) {
  int ret;
  if (gpu < 0)
    return -1;
  if (strcmp(name, "h264_nvenc") == 0 || strcmp(name, "hevc_nvenc") == 0) {
    if ((ret = av_opt_set_int(priv_data, "gpu", gpu, 0)) < 0) {
      LOG_ERROR("nvenc set gpu failed, ret = " + std::to_string(ret));
      return -1;
    }
  }
  return 0;
}

int force_hw(void *priv_data, const char *name) {
  int ret;
  if (strcmp(name, "h264_mf") == 0 || strcmp(name, "hevc_mf") == 0) {
    if ((ret = av_opt_set_int(priv_data, "hw_encoding", 1, 0)) < 0) {
      LOG_ERROR("mediafoundation set hw_encoding failed, ret = " +
                std::to_string(ret));
      return -1;
    }
  }
  return 0;
}

int set_others(void *priv_data, const char *name) {
  int ret;
  if (strcmp(name, "h264_mf") == 0 || strcmp(name, "hevc_mf") == 0) {
    // ff_eAVScenarioInfo_DisplayRemoting = 1
    if ((ret = av_opt_set_int(priv_data, "scenario", 1, 0)) < 0) {
      LOG_ERROR("mediafoundation set scenario failed, ret = " +
                std::to_string(ret));
      return -1;
    }
  }
  return 0;
}
} // namespace util
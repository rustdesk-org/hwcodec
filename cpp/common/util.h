#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <chrono>
extern "C" {
#include <libavcodec/avcodec.h>
}

namespace util_encode {

void set_av_codec_ctx(AVCodecContext *c, const std::string &name,
                      int gop, int fps);
bool set_lantency_free(void *priv_data, const std::string &name);
bool set_rate_control(AVCodecContext *c, const std::string &name, int rc,
                      int kbs, int qp, int qp_min, int qp_max);
bool set_gpu(void *priv_data, const std::string &name, int gpu);
bool force_hw(void *priv_data, const std::string &name);
void sanitize_qp(int &qp, int &qp_min, int &qp_max);
int64_t calc_vbr_max_rate(int64_t bit_rate);

bool is_using_cqp(const std::string &name, AVCodecContext *c);
bool change_qp(AVCodecContext *c, const std::string &name, int qp, int qp_min, int qp_max);
bool change_bit_rate(AVCodecContext *c, const std::string &name, int rc, int kbs);
void vram_encode_test_callback(const uint8_t *data, int32_t len, int32_t key, const void *obj, int64_t pts);

} // namespace util

namespace util_decode {
    bool has_flag_could_not_find_ref_with_poc();
}

namespace util {

    inline std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    inline int64_t elapsed_ms(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now() - start).count();
    }

    inline bool skip_test(const int64_t *excludedLuids, const int32_t *excludeFormats, int32_t excludeCount, int64_t currentLuid, int32_t dataFormat) {
      for (int32_t i = 0; i < excludeCount; i++) {
        if (excludedLuids[i] == currentLuid && excludeFormats[i] == dataFormat) {
          return true;
        }
      }
      return false;
    }
}


#endif

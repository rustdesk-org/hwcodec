#ifndef UTIL_H
#define UTIL_H

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace util {

void set_av_codec_ctx(AVCodecContext *c, const std::string &name, int kbs,
                      int gop, int fps);
bool set_lantency_free(void *priv_data, const std::string &name);
bool set_quality(void *priv_data, const std::string &name, int quality);
bool set_rate_control(AVCodecContext *c, const std::string &name, int rc,
                      int q);
bool set_gpu(void *priv_data, const std::string &name, int gpu);
bool force_hw(void *priv_data, const std::string &name);
bool set_others(void *priv_data, const std::string &name);

bool change_bit_rate(AVCodecContext *c, const std::string &name, int kbs);

} // namespace util

#endif

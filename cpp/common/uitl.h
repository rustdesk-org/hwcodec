#ifndef UTIL_H
#define UTIL_H

#include <string>

namespace util {
enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateContorl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

bool set_lantency_free(void *priv_data, const std::string &name);
bool set_quality(void *priv_data, const std::string &name, int quality);
bool set_rate_control(void *priv_data, const std::string &name, int rc);
bool set_gpu(void *priv_data, const std::string &name, int gpu);
bool force_hw(void *priv_data, const std::string &name);
bool set_others(void *priv_data, const std::string &name);

} // namespace util

#endif

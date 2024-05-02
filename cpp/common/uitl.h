#ifndef UTIL_H
#define UTIL_H

namespace util {
enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateContorl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

int set_lantency_free(void *priv_data, const char *name);
int set_quality(void *priv_data, const char *name, int quality);
int set_rate_control(void *priv_data, const char *name, int rc);
int set_gpu(void *priv_data, const char *name, int gpu);
int force_hw(void *priv_data, const char *name);
int set_others(void *priv_data, const char *name);

} // namespace util

#endif

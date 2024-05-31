#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_GOP 0x7FFFFFFF // i32 max

enum AdapterVendor {
  ADAPTER_VENDOR_AMD = 0x1002,
  ADAPTER_VENDOR_INTEL = 0x8086,
  ADAPTER_VENDOR_NVIDIA = 0x10DE,
  ADAPTER_VENDOR_UNKNOWN = 0,
};

enum SurfaceFormat {
  SURFACE_FORMAT_BGRA,
  SURFACE_FORMAT_RGBA,
  SURFACE_FORMAT_NV12,
};

enum DataFormat {
  H264,
  H265,
  VP8,
  VP9,
  AV1,
};

enum API {
  API_DX11,
};

struct AdapterDesc {
  int64_t luid;
};

enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateControl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
  RC_CQ,
};

#endif // COMMON_H
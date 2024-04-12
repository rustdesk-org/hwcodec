#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_GOP 0xFFFF

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

enum FormatMASK {
  MASK_H264 = 1 << 0,
  MASK_H265 = 1 << 1,
  MASK_VP8 = 1 << 2,
  MASK_VP9 = 1 << 3,
  MASK_AV1 = 1 << 4,
};

enum API {
  API_DX11,
};

struct AdapterDesc {
  int64_t luid;
};

void hwcodec_get_bin_file(int32_t is265, uint8_t **p, int32_t *len);

#endif // COMMON_H
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_GOP 0x7FFFFFFF // i32 max

#define TEST_TIMEOUT_MS 1000
#define ENCODE_TIMEOUT_MS 1000
#define DECODE_TIMEOUT_MS 1000

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

// same as Driver
enum Vendor {
  VENDOR_NV = 0,
  VENDOR_AMD = 1,
  VENDOR_INTEL = 2,
  VENDOR_FFMPEG = 3
};

enum RateControl {
  RC_CBR,
  RC_VBR,
  RC_CQP,
};

#define DEFAULT_QP     28
#define DEFAULT_QP_MIN 22
#define DEFAULT_QP_MAX 34
#define DEFAULT_KBS    2000

enum HwcodecErrno {
  HWCODEC_SUCCESS = 0,
  HWCODEC_ERR_COMMON = -1,
  HWCODEC_ERR_HEVC_COULD_NOT_FIND_POC = -2,
};

#endif // COMMON_H
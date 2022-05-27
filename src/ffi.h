#ifndef FFI_H
#define FFI_H

#include <stdint.h>

#define AV_NUM_DATA_POINTERS 8

enum AVPixelFormat {
  AV_PIX_FMT_YUV420P = 0,
  AV_PIX_FMT_NV12 = 23,
};

enum AVHWDeviceType {
  AV_HWDEVICE_TYPE_NONE,
  AV_HWDEVICE_TYPE_VDPAU,
  AV_HWDEVICE_TYPE_CUDA,
  AV_HWDEVICE_TYPE_VAAPI,
  AV_HWDEVICE_TYPE_DXVA2,
  AV_HWDEVICE_TYPE_QSV,
  AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
  AV_HWDEVICE_TYPE_D3D11VA,
  AV_HWDEVICE_TYPE_DRM,
  AV_HWDEVICE_TYPE_OPENCL,
  AV_HWDEVICE_TYPE_MEDIACODEC,
  AV_HWDEVICE_TYPE_VULKAN,
};

typedef void (*DecodeCallback)(const void *obj, int width, int height,
                               int pixfmt, int linesize[AV_NUM_DATA_POINTERS],
                               uint8_t *data[AV_NUM_DATA_POINTERS], int key);
typedef void (*EncodeCallback)(const uint8_t *data, int len, int64_t pts,
                               const void *obj);

void *new_encoder(const char *name, int fps, int width, int height, int pixfmt,
                  int align, int *linesize, int *offset, int *length,
                  EncodeCallback callback);
void *new_decoder(const char *name, int device_type, DecodeCallback callback);
int encode(void *encoder, const uint8_t *data, int length, const void *obj);
int decode(void *decoder, const uint8_t *data, int length, const void *obj);
void free_encoder(void *encoder);
void free_decoder(void *decoder);
int hwdevice_supported(int device_type);
int get_linesize_offset_length(int pix_fmt, int width, int height, int align,
                               int *linesize, int *offset, int *length);

#endif  // FFI_H
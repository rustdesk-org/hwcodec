#ifndef FF1_FFI_H
#define FF1_FFI_H

#include <stdint.h>

#define AV_NUM_DATA_POINTERS 8

enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateControl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

typedef void (*PixelbufferDecodeCallback)(const void *obj, int width,
                                          int height, int pixfmt,
                                          int linesize[AV_NUM_DATA_POINTERS],
                                          uint8_t *data[AV_NUM_DATA_POINTERS],
                                          int key);
typedef void (*PixelbufferEncodeCallback)(const uint8_t *data, int len,
                                          int64_t pts, int key,
                                          const void *obj);

void *hwcodec_new_encoder(const char *name, int width, int height, int pixfmt,
                          int align, int bit_rate, int time_base_num,
                          int time_base_den, int gop, int quality, int rc,
                          int thread_count, int gpu, int *linesize, int *offset,
                          int *length, PixelbufferEncodeCallback callback);
void *hwcodec_new_decoder(const char *name, int device_type, int thread_count,
                          PixelbufferDecodeCallback callback);
int hwcodec_encode(void *encoder, const uint8_t *data, int length,
                   const void *obj, int64_t ms);
int hwcodec_decode(void *decoder, const uint8_t *data, int length,
                   const void *obj);
void hwcodec_free_encoder(void *encoder);
void hwcodec_free_decoder(void *decoder);
int hwcodec_get_linesize_offset_length(int pix_fmt, int width, int height,
                                       int align, int *linesize, int *offset,
                                       int *length);
int hwcodec_set_bitrate(void *encoder, int bitrate);
void hwcodec_get_bin_file(int is265, uint8_t **p, int *len);

#endif // FFI_H
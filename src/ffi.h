#ifndef FFI_H
#define FFI_H

#include <stdint.h>

#define AV_NUM_DATA_POINTERS 8

#define AV_LOG_QUIET -8
#define AV_LOG_PANIC 0
#define AV_LOG_FATAL 8
#define AV_LOG_ERROR 16
#define AV_LOG_WARNING 24
#define AV_LOG_INFO 32
#define AV_LOG_VERBOSE 40
#define AV_LOG_DEBUG 48
#define AV_LOG_TRACE 56

enum AVPixelFormat {
  AV_PIX_FMT_YUV420P = 0,
  AV_PIX_FMT_NV12 = 23,
};

enum Quality { Quality_Default, Quality_High, Quality_Medium, Quality_Low };

enum RateContorl {
  RC_DEFAULT,
  RC_CBR,
  RC_VBR,
};

typedef void (*DecodeCallback)(const void *obj, int width, int height,
                               int pixfmt, int linesize[AV_NUM_DATA_POINTERS],
                               uint8_t *data[AV_NUM_DATA_POINTERS], int key);
typedef void (*EncodeCallback)(const uint8_t *data, int len, int64_t pts,
                               const void *obj);

void *new_encoder(const char *name, int width, int height, int pixfmt,
                  int align, int bit_rate, int time_base_num, int time_base_den,
                  int gop, int quality, int rc, int *linesize, int *offset,
                  int *length, EncodeCallback callback);
void *new_decoder(const char *name, int device_type, DecodeCallback callback);
void *new_muxer(const char *filename, int width, int height, int is265,
                int framerate);
int encode(void *encoder, const uint8_t *data, int length, const void *obj,
           int64_t ms);
int decode(void *decoder, const uint8_t *data, int length, const void *obj);
int write_video_frame(void *muxer, const uint8_t *data, int len,
                      int64_t elapsed_ms);
int write_tail(void *muxer);
void free_encoder(void *encoder);
void free_decoder(void *decoder);
void free_muxer(void *muxer);
int get_linesize_offset_length(int pix_fmt, int width, int height, int align,
                               int *linesize, int *offset, int *length);
int set_bitrate(void *encoder, int bitrate);
int av_log_get_level(void);
void av_log_set_level(int level);

#endif  // FFI_H
#ifndef FFMPEG_RAM_FFI_H
#define FFMPEG_RAM_FFI_H

#include <stdint.h>

#define AV_NUM_DATA_POINTERS 8

typedef void (*RamDecodeCallback)(const void *obj, int width, int height,
                                  int pixfmt,
                                  int linesize[AV_NUM_DATA_POINTERS],
                                  uint8_t *data[AV_NUM_DATA_POINTERS], int key);
typedef void (*RamEncodeCallback)(const uint8_t *data, int len, int64_t pts,
                                  int key, const void *obj);

void *ffmpeg_ram_new_encoder(const char *name, const char *mc_name, int width,
                             int height, int pixfmt, int align, int fps,
                             int gop, int rc, int quality, int kbs, int q,
                             int thread_count, int gpu, int *linesize,
                             int *offset, int *length,
                             RamEncodeCallback callback);
void *ffmpeg_ram_new_decoder(const char *name, int device_type,
                             int thread_count, RamDecodeCallback callback);
int ffmpeg_ram_encode(void *encoder, const uint8_t *data, int length,
                      const void *obj, int64_t ms);
int ffmpeg_ram_decode(void *decoder, const uint8_t *data, int length,
                      const void *obj);
void ffmpeg_ram_free_encoder(void *encoder);
void ffmpeg_ram_free_decoder(void *decoder);
int ffmpeg_ram_get_linesize_offset_length(int pix_fmt, int width, int height,
                                          int align, int *linesize, int *offset,
                                          int *length);
int ffmpeg_ram_set_bitrate(void *encoder, int kbs);

#if defined(__linux__)
#define FFMPEG_GPU_FRAME_PRIME 2

typedef struct FFmpegPrimeFrame {
  int kind;
  int n_fds;
  int fds[4];
  uint32_t fourcc;
  uint64_t modifier;
  int width;
  int height;
  int n_planes;
  int pitches[4];
  int offsets[4];
  int obj_indices[4];
} FFmpegPrimeFrame;

void *ffmpeg_vaapi_prime_new(int hevc);
void ffmpeg_vaapi_prime_free(void *decoder);
int ffmpeg_vaapi_prime_decode(void *decoder, const uint8_t *data, int length,
                             FFmpegPrimeFrame *out);
#endif

#endif // FFMPEG_RAM_FFI_H
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

#ifdef __linux__
#include "linux_dmabuf_ffi.h"
/** Thread-confined GPU comparison context. Returns 1 changed, 0 equal, or a negative error. */
void *ffmpeg_ram_new_dmabuf_detector(const char *render_node);
int ffmpeg_ram_dmabuf_changed(void *detector, const HwDmaBufDescriptor *frame, const char *render_node);
void ffmpeg_ram_free_dmabuf_detector(void *detector);
int ffmpeg_ram_encode_dmabuf(void *encoder, const HwDmaBufDescriptor *frame,
                             const char *render_node, const void *obj, int64_t ms);
#endif

#endif // FFMPEG_RAM_FFI_H
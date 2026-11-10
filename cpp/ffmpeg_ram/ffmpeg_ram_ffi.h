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

/* GPU-frame delivery (used by macOS / VideoToolbox today).
 *
 * The callback is synchronous and the descriptor is BORROWED: `pixel_buffer`
 * (a CVPixelBufferRef) is valid only during the invocation and the decoder
 * performs its own frame cleanup right after it returns. The callee must
 * retain the buffer (CVPixelBufferRetain) before keeping it.
 *
 * Return PIXELBUFFER_ACCEPTED when the frame was taken over (the decoder then
 * skips the software transfer for that frame), or PIXELBUFFER_REJECTED when it
 * was not; a rejected frame is delivered through the normal software path
 * (same decoded frame, no re-decode). */
#define PIXELBUFFER_REJECTED 0
#define PIXELBUFFER_ACCEPTED 1

typedef struct FfiPixelBufferFrame {
  void *pixel_buffer; /* borrowed CVPixelBufferRef, valid only during the call */
  int32_t width;
  int32_t height;
  uint32_t pixfmt; /* e.g. '420f' / '420v' */
} FfiPixelBufferFrame;

typedef int32_t (*RamPixelBufferCallback)(void *obj,
                                          const FfiPixelBufferFrame *frame);

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

/* Install (or clear, with NULL) the GPU-frame callback. Must be called by the
 * owner of the decoder, only while it is idle: before the first
 * ffmpeg_ram_decode() call or in between two calls. Not thread safe against a
 * concurrent decode. */
void ffmpeg_ram_set_pixelbuffer_callback(void *decoder,
                                         RamPixelBufferCallback callback);

#endif // FFMPEG_RAM_FFI_H
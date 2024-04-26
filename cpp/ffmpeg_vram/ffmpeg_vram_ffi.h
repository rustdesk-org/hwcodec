#ifndef FFMPEG_VRAM_FFI_H
#define FFMPEG_VRAM_FFI_H

#include "../common/callback.h"
#include <stdbool.h>

#define AV_NUM_DATA_POINTERS 8

void *ffmpeg_vram_new_decoder(void *device, int64_t luid, int32_t api,
                              int32_t codecID);
int ffmpeg_vram_decode(void *decoder, uint8_t *data, int len,
                       DecodeCallback callback, void *obj);
int ffmpeg_vram_destroy_decoder(void *decoder);
int ffmpeg_vram_test_decode(void *outDescs, int32_t maxDescNum,
                            int32_t *outDescNum, int32_t api,
                            int32_t dataFormat, uint8_t *data, int32_t length);

#endif // FFMPEG_VRAM_FFI_H
#ifndef FFMPEG_VRAM_FFI_H
#define FFMPEG_VRAM_FFI_H

#include "../common/callback.h"
#include <stdbool.h>

#define AV_NUM_DATA_POINTERS 8

void *ffmpeg_vram_new_decoder(void *device, int64_t luid, int32_t api,
                              int32_t codecID, bool outputSharedHandle);
int ffmpeg_vram_decode(void *decoder, uint8_t *data, int len,
                       DecodeCallback callback, void *obj);
void ffmpeg_vram_free_decoder(void *decoder);

#endif // FFMPEG_VRAM_FFI_H
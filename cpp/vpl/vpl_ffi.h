#ifndef VPL_FFI_H
#define VPL_FFI_H

#include "../common/callback.h"
#include <stdbool.h>

int vpl_driver_support();

void *vpl_new_encoder(void *handle, int64_t luid, int32_t api,
                      int32_t dataFormat, int32_t width, int32_t height,
                      int32_t kbs, int32_t framerate, int32_t gop);

int vpl_encode(void *encoder, void *tex, EncodeCallback callback, void *obj);

int vpl_destroy_encoder(void *encoder);

void *vpl_new_decoder(void *device, int64_t luid, int32_t api,
                      int32_t dataFormat, bool outputSharedHandle);

int vpl_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
               void *obj);

int vpl_destroy_decoder(void *decoder);

int vpl_test_encode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                    int32_t api, int32_t dataFormat, int32_t width,
                    int32_t height, int32_t kbs, int32_t framerate,
                    int32_t gop);

int vpl_test_decode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                    int32_t api, int32_t dataFormat, bool outputSharedHandle,
                    uint8_t *data, int32_t length);

int vpl_set_bitrate(void *encoder, int32_t kbs);

int vpl_set_framerate(void *encoder, int32_t framerate);

#endif // VPL_FFI_H
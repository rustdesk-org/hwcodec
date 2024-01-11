#ifndef CALLBACK_H
#define CALLBACK_H

#include <stdint.h>

#define MAX_DATA_NUM 8

typedef void (*EncodeCallback)(const uint8_t *data, int32_t len, int32_t key,
                               const void *obj);

typedef void (*DecodeCallback)(void *opaque, const void *obj);

#endif // CALLBACK_H
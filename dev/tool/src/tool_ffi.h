#ifndef TOOL_FFI_H
#define TOOL_FFI_H

#include <stdint.h>

void *tool_new(int64_t luid);
void *tool_device(void *tool);
void *tool_get_texture(void *tool, int width, int height);
void tool_get_texture_size(void *tool, void *texture, int *width, int *height);
void tool_destroy(void *tool);
int tool_texture_write_bgra(void *texture, const uint8_t *pixels, int width,
                            int height);
int tool_texture_read_bgra(void *texture, uint8_t *pixels, int width, int height);

#endif // TOOL_FFI_H
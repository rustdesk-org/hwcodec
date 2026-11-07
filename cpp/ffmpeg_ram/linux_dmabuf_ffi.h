#ifndef HWCODEC_LINUX_DMABUF_FFI_H
#define HWCODEC_LINUX_DMABUF_FFI_H
#include <stdint.h>
/** Borrowed SDR DMA-BUF. The fd and node string remain valid throughout encode. */
typedef struct {
  int fd;
  uint32_t width, height, format, num_planes;
  uint32_t offsets[4], pitches[4];
  uint64_t modifier;
} HwDmaBufDescriptor;
#endif

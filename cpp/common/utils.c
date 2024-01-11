#include <stdint.h>

#include "incbin.h"

INCBIN_EXTERN(BinFile264);
INCBIN_EXTERN(BinFile265);

void hwcodec_get_bin_file(int32_t is265, uint8_t **p, int32_t *len) {
  if (is265 == 0) {
    *p = (uint8_t *)gBinFile264Data;
    *len = gBinFile264Size;
  } else {
    *p = (uint8_t *)gBinFile265Data;
    *len = gBinFile265Size;
  }
}
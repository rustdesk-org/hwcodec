#include <libavutil/log.h>
#include <stdio.h>

void hwcodec_fprintf(FILE *const _Stream, const char *const _Format, ...) {
  va_list ap;
  int level = av_log_get_level();

  if ((_Stream == stderr && level >= AV_LOG_ERROR) ||
      (_Stream == stdout && level > AV_LOG_QUIET)) {
    va_start(ap, _Format);
    vfprintf(_Stream, _Format, ap);
    va_end(ap);
  }
}
#include <libavutil/log.h>
#include <stdio.h>

void my_fprintf(FILE *const _Stream, const char *const _Format, ...) {
  va_list ap;
  int level = av_log_get_level();

  if ((_Stream == stderr && level >= AV_LOG_ERROR) ||
      (_Stream == stdout && level > AV_LOG_QUIET)) {
    va_start(ap, _Format);
    vfprintf(_Stream, _Format, ap);
    va_end(ap);
  }
}


#if defined(__clang__) || defined(__GNUC__)
void log_callback(__attribute__((unused)) void* _ptr, int level, const char* fmt_orig, va_list args)
#else
void log_callback(void* _ptr, int level, const char* fmt_orig, va_list args)
#endif
{
	char fmt[256] = {0};
	strncpy(fmt, fmt_orig, sizeof(fmt) - 1);
	int done = 0;
	// strip whitespaces from end
	for (int i = sizeof(fmt) - 1; i >= 0 && !done; --i)
		switch (fmt[i])
		{
		case ' ':
		case '\n':
		case '\t':
		case '\r':
			fmt[i] = '\0';
			break;
		case '\0':
			break;
		default:
			done = 1;
		}
	char buf[2048];
	vsnprintf(buf, sizeof(buf), fmt, args);
	switch (level)
	{
	case AV_LOG_FATAL:
	case AV_LOG_ERROR:
	case AV_LOG_PANIC:
		log_error("%s", buf);
		break;
	case AV_LOG_INFO:
		log_info("%s", buf);
		break;
	case AV_LOG_WARNING:
		log_warn("%s", buf);
		break;
	case AV_LOG_QUIET:
		break;
	case AV_LOG_VERBOSE:
		log_debug("%s", buf);
		break;
	case AV_LOG_DEBUG:
		log_trace("%s", buf);
		break;
	}
}

// called in src/c_log.rs
void init_ffmpeg_logger_() { av_log_set_callback(log_callback); }

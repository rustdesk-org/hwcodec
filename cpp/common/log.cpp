
#include "log.h"

namespace gol {
enum {
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_WARN = 1,
  LOG_LEVEL_INFO = 2,
  LOG_LEVEL_DEBUG = 3,
  LOG_LEVEL_TRACE = 4,
};

extern "C" void hwcodec_log(int level, const char *message);

void log_to_rust(int level, const std::string &message) {
  const char *cstr = message.c_str();
  hwcodec_log(level, cstr);
}

void error(const std::string &message) {
  log_to_rust(LOG_LEVEL_ERROR, message);
}

void warn(const std::string &message) { log_to_rust(LOG_LEVEL_WARN, message); }

void info(const std::string &message) { log_to_rust(LOG_LEVEL_INFO, message); }

void debug(const std::string &message) {
  log_to_rust(LOG_LEVEL_DEBUG, message);
}

void trace(const std::string &message) {
  log_to_rust(LOG_LEVEL_TRACE, message);
}

} // namespace gol

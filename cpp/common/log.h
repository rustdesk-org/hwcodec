#ifndef LOG_H
#define LOG_H

#include <string>

#ifndef LOG_MODULE
#define LOG_MODULE "*"
#endif

namespace gol {
void error(const std::string &message);
void warn(const std::string &message);
void info(const std::string &message);
void debug(const std::string &message);
void trace(const std::string &message);
} // namespace gol

#define LOG_ERROR(message)                                                     \
  gol::error(std::string("[") + LOG_MODULE + "] " + message)
#define LOG_WARN(message)                                                      \
  gol::warn(std::string("[") + LOG_MODULE + "] " + message)
#define LOG_INFO(message)                                                      \
  gol::info(std::string("[") + LOG_MODULE + "] " + message)
#define LOG_DEBUG(message)                                                     \
  gol::debug(std::string("[") + LOG_MODULE + "] " + message)
#define LOG_TRACE(message)                                                     \
  gol::trace(std::string("[") + LOG_MODULE + "] " + message)

#endif
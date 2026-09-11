#include <public/common/AMFFactory.h>

#define LOG_MODULE "AMF_SUPPORT"
#include "log.h"

extern "C" {

int amf_driver_support() noexcept {
  try {
    AMFFactoryHelper factory;
    AMF_RESULT res = factory.Init();
    if (res == AMF_OK) {
      factory.Terminate();
      return 0;
    }
  } catch (const std::exception &e) {
    LOG_TRACE(std::string("AMF driver unavailable: ") + e.what());
  } catch (...) {
    // Logging can allocate, so do not log while handling an unknown exception.
  }
  return -1;
}
} // extern "C"

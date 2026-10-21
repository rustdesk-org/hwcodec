#include <public/common/AMFFactory.h>
#include <public/common/Thread.h>

#define LOG_MODULE "AMF_SUPPORT"
#include "log.h"

extern "C" {

int amf_driver_support() noexcept {
  // Keep the DLL loaded until any driver exception has been handled.
  struct ProbeFactory : AMFFactoryHelper {
    ~ProbeFactory() {
      // Failed Init has loaded the DLL without incrementing m_iRefCount.
      if (m_hDLLHandle && m_iRefCount == 0) {
        amf_free_library(m_hDLLHandle);
        m_hDLLHandle = nullptr;
      }
    }
  } factory;
  try {
    AMF_RESULT res = factory.Init();
    if (res == AMF_OK) {
      factory.Terminate();
      return 0;
    }
  } catch (const std::exception &e) {
    LOG_TRACE(std::string("AMF driver unavailable: ") + e.what());
  } catch (...) {
    LOG_ERROR("amf_driver_support: unknown exception");
  }
  return -1;
}
} // extern "C"

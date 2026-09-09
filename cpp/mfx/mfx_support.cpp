#if defined(_M_IX86) || defined(_M_X64)
#include <mfxvideo++.h>

#define LOG_MODULE "MFX_SUPPORT"
#include "log.h"

namespace {

mfxStatus InitSession(MFXVideoSession &session) {
  mfxInitParam mfxparams{};
  mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
  mfxparams.Implementation = impl;
  mfxparams.Version.Major = 1;
  mfxparams.Version.Minor = 0;
  mfxparams.GPUCopy = MFX_GPUCOPY_OFF;

  return session.InitEx(mfxparams);
}

} // namespace

extern "C" {

int mfx_driver_support() {
  // Never let an exception escape this extern "C" boundary. On some Intel
  // drivers MFXVideoSession::InitEx may throw (including non-std exceptions);
  // letting it propagate out of C calls std::terminate -> abort ->
  // __fastfail(FATAL_APP_EXIT) (0xc0000409 / subcode 7), crashing the host
  // process during startup hardware-codec probing (rustdesk/rustdesk#15218).
  try {
    MFXVideoSession session;
    return InitSession(session) == MFX_ERR_NONE ? 0 : -1;
  } catch (const std::exception &e) {
    LOG_ERROR(std::string("mfx_driver_support exception: ") + e.what());
  } catch (...) {
    LOG_ERROR(std::string("mfx_driver_support unknown exception"));
  }
  return -1;
}
} // extern "C"
#else
extern "C" int mfx_driver_support() { return -1; }
#endif

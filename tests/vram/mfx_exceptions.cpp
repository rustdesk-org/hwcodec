#include <windows.h>
#include <mfxvideo.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef NDEBUG
#error These tests require assertions.
#endif

extern "C" int mfx_driver_support() noexcept;

namespace {
int mode = 0;
int closes = 0;
int logs = 0;
} // namespace

namespace gol {
void error(const std::string &) { ++logs; }
} // namespace gol

// The production probe is linked from hwcodec.lib, so these driver calls are
// opaque while its optimized exception-handling code is compiled.
extern "C" {
mfxStatus MFX_CDECL MFXInitEx(mfxInitParam, mfxSession *session) {
  switch (mode) {
  case 1:
    throw std::runtime_error("injected MFXInitEx exception");
  case 2:
    throw 42;
  case 3:
    RaiseException(0xe0424242, 0, 0, nullptr);
    break;
  case 4:
    *static_cast<volatile int *>(nullptr) = 1;
  }
  *session = reinterpret_cast<mfxSession>(1);
  return MFX_ERR_NONE;
}

mfxStatus MFX_CDECL MFXClose(mfxSession) {
  ++closes;
  return MFX_ERR_NONE;
}

mfxStatus MFX_CDECL MFXInit(mfxIMPL, mfxVersion *, mfxSession *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXQueryIMPL(mfxSession, mfxIMPL *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXQueryVersion(mfxSession, mfxVersion *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXJoinSession(mfxSession, mfxSession) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXDisjoinSession(mfxSession) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXCloneSession(mfxSession, mfxSession *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXSetPriority(mfxSession, mfxPriority) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXGetPriority(mfxSession, mfxPriority *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXDoWork(mfxSession) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXVideoCORE_SetBufferAllocator(mfxSession, mfxBufferAllocator *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXVideoCORE_SetFrameAllocator(mfxSession, mfxFrameAllocator *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXVideoCORE_SetHandle(mfxSession, mfxHandleType, mfxHDL) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXVideoCORE_GetHandle(mfxSession, mfxHandleType, mfxHDL *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXVideoCORE_QueryPlatform(mfxSession, mfxPlatform *) { return MFX_ERR_NONE; }
mfxStatus MFX_CDECL MFXVideoCORE_SyncOperation(mfxSession, mfxSyncPoint, mfxU32) { return MFX_ERR_NONE; }
}

int main(int argc, char **argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (argc > 1) {
    mode = std::atoi(argv[1]);
    mfx_driver_support();
    std::puts("FAIL: SEH returned through the driver probe");
    return 1;
  }
  for (mode = 0; mode < 3; ++mode) {
    closes = logs = 0;
    assert(mfx_driver_support() == (mode ? -1 : 0));
    assert(closes == 1 && logs == (mode ? 1 : 0));
  }
  std::puts("PASS 3 driver probe exception/cleanup scenarios");
}

#define FFNV_LOG_FUNC
#define FFNV_DEBUG_LOG_FUNC
#include <dynlink_loader.h>

#define LOG_MODULE "NV_SUPPORT"
#include "log.h"

extern "C" {

int nv_encode_driver_support() noexcept {
  CudaFunctions *cuda = NULL;
  NvencFunctions *nvenc = NULL;
  try {
    int result = cuda_load_functions(&cuda, NULL);
    if (result == 0)
      result = nvenc_load_functions(&nvenc, NULL);
    nvenc_free_functions(&nvenc);
    cuda_free_functions(&cuda);
    if (result != 0)
      LOG_TRACE(std::string("CUDA or NVENC driver unavailable"));
    return result == 0 ? 0 : -1;
  } catch (...) {
    nvenc_free_functions(&nvenc);
    cuda_free_functions(&cuda);
    // Logging can allocate, so do not log again while handling an exception.
    return -1;
  }
}

int nv_decode_driver_support() noexcept {
  CudaFunctions *cuda = NULL;
  CuvidFunctions *cuvid = NULL;
  try {
    int result = cuda_load_functions(&cuda, NULL);
    if (result == 0)
      result = cuvid_load_functions(&cuvid, NULL);
    cuvid_free_functions(&cuvid);
    cuda_free_functions(&cuda);
    if (result != 0)
      LOG_TRACE(std::string("CUDA or CUVID driver unavailable"));
    return result == 0 ? 0 : -1;
  } catch (...) {
    cuvid_free_functions(&cuvid);
    cuda_free_functions(&cuda);
    return -1;
  }
}

} // extern "C"

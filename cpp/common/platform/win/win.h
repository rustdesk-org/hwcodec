#ifndef WIN_H
#define WIN_H

#include <d3d11.h>
#include <d3d11_1.h>
#include <iostream>
#include <vector>
#include <wrl/client.h>

#include "../../common.h"

using Microsoft::WRL::ComPtr;
using namespace std;

#define IF_FAILED_RETURN(X)                                                    \
  if (FAILED(hr = (X))) {                                                      \
    return hr;                                                                 \
  }

#define IF_FAILED_THROW(X)                                                     \
  if (FAILED(hr = (X))) {                                                      \
    throw hr;                                                                  \
  }
#define SAFE_RELEASE(p)                                                        \
  {                                                                            \
    if ((p)) {                                                                 \
      (p)->Release();                                                          \
      (p) = nullptr;                                                           \
    }                                                                          \
  }
#define LUID(desc)                                                             \
  (((int64_t)desc.AdapterLuid.HighPart << 32) | desc.AdapterLuid.LowPart)
#define HRB(f) MS_CHECK(f, return false;)
#define HRI(f) MS_CHECK(f, return -1;)
#define HRP(f) MS_CHECK(f, return nullptr;)
#define MS_CHECK(f, ...)                                                       \
  do {                                                                         \
    HRESULT __ms_hr__ = (f);                                                   \
    if (FAILED(__ms_hr__)) {                                                   \
      std::clog                                                                \
          << #f "  ERROR@" << __LINE__ << __FUNCTION__ << ": (" << std::hex    \
          << __ms_hr__ << std::dec << ") "                                     \
          << std::error_code(__ms_hr__, std::system_category()).message()      \
          << std::endl                                                         \
          << std::flush;                                                       \
      __VA_ARGS__                                                              \
    }                                                                          \
  } while (false)

class NativeDevice {
public:
  bool Init(int64_t luid, ID3D11Device *device, int pool_size = 1);
  bool EnsureTexture(int width, int height);
  bool SetTexture(ID3D11Texture2D *texture);
  HANDLE GetSharedHandle();
  ID3D11Texture2D *GetCurrentTexture();
  int next();
  void BeginQuery();
  void EndQuery();
  bool Query();
  bool Process(ID3D11Texture2D *in, ID3D11Texture2D *out,
               D3D11_VIDEO_PROCESSOR_CONTENT_DESC content_desc,
               DXGI_COLOR_SPACE_TYPE colorSpace_in,
               DXGI_COLOR_SPACE_TYPE colorSpace_out, int arraySlice);
  bool ToNV12(ID3D11Texture2D *texture, int width, int height,
              DXGI_COLOR_SPACE_TYPE colorSpace_in,
              DXGI_COLOR_SPACE_TYPE colorSpace_outt);

private:
  bool InitFromLuid(int64_t luid);
  bool InitFromDevice(ID3D11Device *device);
  bool SetMultithreadProtected();
  bool InitQuery();
  bool InitVideoDevice();

public:
  // Direct3D 11
  ComPtr<IDXGIFactory1> factory1_ = nullptr;
  ComPtr<IDXGIAdapter> adapter_ = nullptr;
  ComPtr<IDXGIAdapter1> adapter1_ = nullptr;
  ComPtr<ID3D11Device> device_ = nullptr;
  ComPtr<ID3D11DeviceContext> context_ = nullptr;
  ComPtr<ID3D11Query> query_ = nullptr;
  ComPtr<ID3D11VideoDevice> video_device_ = nullptr;
  ComPtr<ID3D11VideoContext> video_context_ = nullptr;
  ComPtr<ID3D11VideoContext1> video_context1_ = nullptr;
  ComPtr<ID3D11VideoProcessorEnumerator> video_processor_enumerator_ = nullptr;
  ComPtr<ID3D11VideoProcessor> video_processor_ = nullptr;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC last_content_desc_ = {};
  ComPtr<ID3D11Texture2D> nv12_texture_ = nullptr;

  int count_;
  int index_ = 0;

private:
  std::vector<ComPtr<ID3D11Texture2D>> texture_;
};

class Adapter {
public:
  bool Init(IDXGIAdapter1 *adapter1);

private:
  bool SetMultithreadProtected();

public:
  ComPtr<IDXGIAdapter> adapter_ = nullptr;
  ComPtr<IDXGIAdapter1> adapter1_ = nullptr;
  ComPtr<ID3D11Device> device_ = nullptr;
  ComPtr<ID3D11DeviceContext> context_ = nullptr;
  DXGI_ADAPTER_DESC1 desc1_;
};

class Adapters {
public:
  bool Init(AdapterVendor vendor);

public:
  ComPtr<IDXGIFactory1> factory1_ = nullptr;
  std::vector<std::unique_ptr<Adapter>> adapters_;
};

#endif
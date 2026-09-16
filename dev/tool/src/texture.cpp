#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>

extern "C" {
#include "tool_ffi.h"
}

using Microsoft::WRL::ComPtr;

extern "C" int tool_texture_write_bgra(void *texture, const uint8_t *pixels,
                                      int width, int height) {
  if (!texture || !pixels || width <= 0 || height <= 0)
    return E_INVALIDARG;
  auto source = static_cast<ID3D11Texture2D *>(texture);
  D3D11_TEXTURE2D_DESC desc = {};
  source->GetDesc(&desc);
  if (desc.Width != static_cast<UINT>(width) ||
      desc.Height != static_cast<UINT>(height) ||
      desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.MipLevels != 1 ||
      desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
      desc.Usage != D3D11_USAGE_DEFAULT)
    return E_INVALIDARG;

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  source->GetDevice(device.GetAddressOf());
  device->GetImmediateContext(context.GetAddressOf());
  context->UpdateSubresource(source, 0, nullptr, pixels, width * 4, 0);
  return device->GetDeviceRemovedReason();
}

extern "C" int tool_texture_read_bgra(void *texture, uint8_t *pixels, int width,
                                     int height) {
  if (!texture || !pixels || width <= 0 || height <= 0)
    return E_INVALIDARG;
  auto source = static_cast<ID3D11Texture2D *>(texture);
  D3D11_TEXTURE2D_DESC desc = {};
  source->GetDesc(&desc);
  if (desc.Width != static_cast<UINT>(width) ||
      desc.Height != static_cast<UINT>(height) ||
      desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.MipLevels != 1 ||
      desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
    return E_INVALIDARG;

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  source->GetDevice(device.GetAddressOf());
  device->GetImmediateContext(context.GetAddressOf());
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  if (FAILED(hr))
    return hr;

  context->CopyResource(staging.Get(), source);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  const size_t row_bytes = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    std::memcpy(pixels + y * row_bytes,
                static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch,
                row_bytes);
  }
  context->Unmap(staging.Get(), 0);
  return device->GetDeviceRemovedReason();
}

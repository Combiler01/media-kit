// This file is a part of media_kit
// (https://github.com/media-kit/media-kit).
//
// Copyright © 2026 Predidit.
// All rights reserved.
// Use of this source code is governed by MIT license that can be found in the
// LICENSE file.

#include "d3d11_renderer.h"

#include <iostream>
#include <iterator>
#include <stdexcept>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

D3D11Renderer::D3D11Renderer(int32_t width, int32_t height,
                             IDXGIAdapter* flutter_adapter)
    : width_(width > 0 ? width : 1), height_(height > 0 ? height : 1) {
  if (!CreateD3D11Device(flutter_adapter)) {
    throw std::runtime_error("Unable to create Direct3D 11 device.");
  }
  if (!CreateSurfaces()) {
    throw std::runtime_error("Unable to create Direct3D 11 surfaces.");
  }
}

D3D11Renderer::~D3D11Renderer() {
  ReleaseSurfaces();
  d3d_11_device_context_.Reset();
  d3d_11_device_.Reset();
}

bool D3D11Renderer::CreateD3D11Device(IDXGIAdapter* flutter_adapter) {
  const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };

  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  if (flutter_adapter) {
    // Take ownership of caller's reference without an extra AddRef.
    adapter.Attach(flutter_adapter);
  }

  D3D_DRIVER_TYPE driver_type =
      adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

  UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
  // Optional: creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  HRESULT hr = ::D3D11CreateDevice(
      adapter.Get(), driver_type, nullptr, creation_flags, feature_levels,
      static_cast<UINT>(std::size(feature_levels)), D3D11_SDK_VERSION,
      d3d_11_device_.GetAddressOf(), nullptr,
      d3d_11_device_context_.GetAddressOf());

  if (FAILED(hr)) {
    std::cout << "media_kit: D3D11Renderer: D3D11CreateDevice failed (hr=0x"
              << std::hex << hr << std::dec << ")" << std::endl;
    return false;
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  if (SUCCEEDED(d3d_11_device_->QueryInterface(__uuidof(IDXGIDevice),
                                               (void**)&dxgi_device)) &&
      dxgi_device) {
    dxgi_device->SetGPUThreadPriority(5);
  }

  return true;
}

bool D3D11Renderer::CreateSurfaces() {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width_);
  desc.Height = static_cast<UINT>(height_);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = 0;
  // Keyed mutex for robust cross-device synchronization (supported Win7 through Win11).
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  HRESULT hr =
      d3d_11_device_->CreateTexture2D(&desc, nullptr, &render_target_texture_);
  if (FAILED(hr)) return false;

  // Retrieve DXGI Shared Handle.
  Microsoft::WRL::ComPtr<IDXGIResource> resource;
  hr = render_target_texture_.As(&resource);
  if (FAILED(hr)) return false;

  hr = resource->GetSharedHandle(&shared_handle_);
  if (FAILED(hr)) return false;

  // Retrieve Keyed Mutex.
  hr = render_target_texture_.As(&keyed_mutex_);
  if (FAILED(hr)) return false;

  // Clear surface to black immediately (eradicating noise on frame 0).
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
  if (SUCCEEDED(d3d_11_device_->CreateRenderTargetView(
          render_target_texture_.Get(), nullptr, &rtv))) {
    const FLOAT black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    d3d_11_device_context_->ClearRenderTargetView(rtv.Get(), black);
  }

  return true;
}

void D3D11Renderer::ReleaseSurfaces() {
  keyed_mutex_.Reset();
  render_target_texture_.Reset();
  shared_handle_ = nullptr;
}

void D3D11Renderer::SetSize(int32_t width, int32_t height) {
  if (width == width_ && height == height_) return;
  width_ = (width > 0) ? width : 1;
  height_ = (height > 0) ? height : 1;
  ReleaseSurfaces();
  CreateSurfaces();
}

bool D3D11Renderer::ProducerAcquire() {
  if (!keyed_mutex_) return false;
  // 0 = producer key. 16ms timeout prevents blocking if compositor stalls.
  return SUCCEEDED(keyed_mutex_->AcquireSync(0, 16));
}

void D3D11Renderer::ProducerCommit() {
  if (d3d_11_device_context_) {
    d3d_11_device_context_->Flush();
  }
  if (keyed_mutex_) {
    // 1 = consumer key (signals Flutter that the frame is ready).
    keyed_mutex_->ReleaseSync(1);
  }
}

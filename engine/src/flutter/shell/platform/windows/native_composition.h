// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_NATIVE_COMPOSITION_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_NATIVE_COMPOSITION_H_
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "flutter/shell/platform/embedder/embedder.h"
namespace flutter {
// Composes Flutter backing stores and native visuals in one Windows target.
class NativeComposition {
 public:
  // Creates a target using the ANGLE device associated with this Flutter view.
  static std::shared_ptr<NativeComposition> Create(HWND window,
                                                   ID3D11Device* device);
  // Detaches the target before its owning HWND is destroyed.
  void Detach();
  // Allocates a visual retained by both its plugin lease and this compositor.
  bool CreateVisual(int64_t* id, IUnknown** visual);
  // Removes a plugin visual and commits its removal immediately.
  void RemoveVisual(int64_t id);
  // Records the rasterization scale of native content in physical pixels.
  bool SetScale(int64_t id, double scale);
  // Submits layers in paint order; the callback draws into a bound EGL surface.
  bool Present(
      const FlutterLayer** layers,
      size_t count,
      const std::function<bool(const FlutterLayer&, ID3D11Texture2D*)>& draw);

 private:
  template <typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;
  struct NativeVisual {
    ComPtr<IDCompositionVisual> visual;
    double scale = 1;
  };
  struct FlutterVisual {
    ComPtr<IDXGISwapChain1> swap_chain;
    ComPtr<IDCompositionVisual> visual;
    UINT width = 0;
    UINT height = 0;
  };
  // Initializes the per-view DirectComposition device and target.
  bool Initialize(HWND window, ID3D11Device* device);
  // Creates or resizes a premultiplied-alpha Flutter composition surface.
  bool PrepareFlutterVisual(size_t index, UINT width, UINT height);
  // Wraps native content in its ordered transform, clip and opacity mutations.
  bool ApplyMutations(const FlutterPlatformView& view,
                      IDCompositionVisual** result);
  std::mutex mutex_;
  ComPtr<IDCompositionDevice> device_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual> root_;
  ComPtr<ID3D11Device> d3d_device_;
  ComPtr<IDXGIFactory2> factory_;
  std::unordered_map<int64_t, NativeVisual> native_visuals_;
  std::vector<FlutterVisual> flutter_visuals_;
  std::vector<ComPtr<IDCompositionVisual>> frame_parents_;
};
}  // namespace flutter
#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_NATIVE_COMPOSITION_H_

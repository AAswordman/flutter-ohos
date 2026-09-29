// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <dwmapi.h>
#include <limits>
#include "flutter/shell/platform/windows/native_composition.h"
#include "gtest/gtest.h"
namespace flutter {
namespace testing {
namespace {
class NativeCompositionTest : public ::testing::Test {
 protected:
  // Uses a hardware D3D device and real hidden Win32 target for composition
  // tests.
  void SetUp() override {
    dpi_context_ = SetThreadDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    com_result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(com_result_) || com_result_ == RPC_E_CHANGED_MODE);
    ASSERT_TRUE(SUCCEEDED(
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                          D3D11_SDK_VERSION, &device_, nullptr, &context_)));
    window_ = CreateWindowW(L"STATIC", L"Native composition test",
                            WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr,
                            nullptr, GetModuleHandle(nullptr), nullptr);
    ASSERT_NE(window_, nullptr);
    composition_ = NativeComposition::Create(window_, device_.Get());
    ASSERT_NE(composition_, nullptr);
  }
  // Detaches the compositor while the target HWND and COM apartment are alive.
  void TearDown() override {
    if (composition_)
      composition_->Detach();
    composition_.reset();
    if (window_)
      DestroyWindow(window_);
    context_.Reset();
    device_.Reset();
    if (SUCCEEDED(com_result_))
      CoUninitialize();
    SetThreadDpiAwarenessContext(dpi_context_);
  }
  // Paints a Flutter backing surface with translucent red using its real GPU
  // target.
  bool Draw(const FlutterLayer&, ID3D11Texture2D* texture) {
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device_->CreateRenderTargetView(texture, nullptr, &target)))
      return false;
    const float color[] = {0.5f, 0, 0, 0.5f};
    context_->ClearRenderTargetView(target.Get(), color);
    context_->Flush();
    return true;
  }
  DPI_AWARENESS_CONTEXT dpi_context_ = nullptr;
  HRESULT com_result_ = E_FAIL;
  HWND window_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  std::shared_ptr<NativeComposition> composition_;
};

// Real DComp submission accepts repeated native layers interleaved with
// Flutter.
TEST_F(NativeCompositionTest, PresentsNativeBetweenFlutterLayersAcrossFrames) {
  int64_t id;
  Microsoft::WRL::ComPtr<IUnknown> visual;
  ASSERT_TRUE(composition_->CreateVisual(&id, &visual));
  EXPECT_GE(id, INT64_C(1) << 40);
  EXPECT_TRUE(composition_->SetScale(id, 1.5));
  FlutterPlatformViewMutation transform = {};
  transform.type = kFlutterPlatformViewMutationTypeTransformation;
  transform.transformation = {1.5, 0, 10, 0, 1.5, 20, 0, 0, 1};
  FlutterPlatformViewMutation opacity = {};
  opacity.type = kFlutterPlatformViewMutationTypeOpacity;
  opacity.opacity = 0.7;
  FlutterPlatformViewMutation clip = {};
  clip.type = kFlutterPlatformViewMutationTypeClipRect;
  clip.clip_rect = {0, 0, 60, 60};
  const FlutterPlatformViewMutation* mutations[] = {&transform, &clip,
                                                    &opacity};
  FlutterPlatformView view = {sizeof(FlutterPlatformView), id, 3, mutations};
  FlutterLayer native = {};
  native.struct_size = sizeof(native);
  native.type = kFlutterLayerContentTypePlatformView;
  native.platform_view = &view;
  FlutterLayer backing = {};
  backing.struct_size = sizeof(backing);
  backing.type = kFlutterLayerContentTypeBackingStore;
  backing.size = {128, 128};
  const FlutterLayer* layers[] = {&backing, &native, &backing};
  auto draw = [this](const FlutterLayer& layer, ID3D11Texture2D* target) {
    return Draw(layer, target);
  };
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(composition_->Present(layers, 3, draw));
  }
  // Removing the popup leaves the browser and Flutter base intact.
  EXPECT_TRUE(composition_->Present(layers, 2, draw));
  // Resize must replace old swapchains without retaining a stale target.
  backing.size = {64, 64};
  EXPECT_TRUE(composition_->Present(layers, 3, draw));
  EXPECT_TRUE(composition_->Present(nullptr, 0, draw));
  composition_->RemoveVisual(id);
  EXPECT_FALSE(composition_->SetScale(id, 1));
}

// Reads actual desktop pixels to verify paint order, alpha and layer removal.
TEST_F(NativeCompositionTest, VisiblePixelsPreserveFlutterPaintOrder) {
  ASSERT_TRUE(
      SetWindowPos(window_, HWND_TOPMOST, 40, 40, 160, 160, SWP_SHOWWINDOW));
  int64_t id;
  Microsoft::WRL::ComPtr<IUnknown> native;
  ASSERT_TRUE(composition_->CreateVisual(&id, &native));
  Microsoft::WRL::ComPtr<IDCompositionVisual> native_visual;
  ASSERT_TRUE(SUCCEEDED(native.As(&native_visual)));
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  ASSERT_TRUE(SUCCEEDED(device_.As(&dxgi)));
  ASSERT_TRUE(SUCCEEDED(dxgi->GetAdapter(&adapter)));
  ASSERT_TRUE(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))));
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = 128;
  desc.Height = 128;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
  ASSERT_TRUE(SUCCEEDED(factory->CreateSwapChainForComposition(
      device_.Get(), &desc, nullptr, &swap)));
  Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  ASSERT_TRUE(SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&buffer))));
  ASSERT_TRUE(SUCCEEDED(
      device_->CreateRenderTargetView(buffer.Get(), nullptr, &target)));
  const float green[] = {0, 1, 0, 1};
  context_->ClearRenderTargetView(target.Get(), green);
  context_->Flush();
  ASSERT_TRUE(SUCCEEDED(swap->Present(0, 0)));
  ASSERT_TRUE(SUCCEEDED(native_visual->SetContent(swap.Get())));
  FlutterPlatformView platform = {sizeof(FlutterPlatformView), id, 0, nullptr};
  FlutterLayer native_layer = {};
  native_layer.struct_size = sizeof(native_layer);
  native_layer.type = kFlutterLayerContentTypePlatformView;
  native_layer.platform_view = &platform;
  FlutterLayer base = {};
  base.struct_size = sizeof(base);
  base.type = kFlutterLayerContentTypeBackingStore;
  base.size = {128, 128};
  FlutterLayer overlay = base;
  overlay.size = {128, 40};
  const FlutterLayer* layers[] = {&base, &native_layer, &overlay};
  auto draw = [this, &base](const FlutterLayer& layer,
                            ID3D11Texture2D* texture) {
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device_->CreateRenderTargetView(texture, nullptr, &rtv))) {
      return false;
    }
    const float red[] = {1, 0, 0, 1};
    const float blue[] = {0, 0, 0.5f, 0.5f};
    context_->ClearRenderTargetView(rtv.Get(), &layer == &base ? red : blue);
    context_->Flush();
    return true;
  };
  // A DWM flush and message pump let committed visuals reach the desktop.
  auto pixel = [this](int y) {
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessage(&message);
    }
    DwmFlush();
    Sleep(100);
    DwmFlush();
    POINT point = {60, y};
    ClientToScreen(window_, &point);
    HDC dc = GetDC(nullptr);
    const COLORREF value = GetPixel(dc, point.x, point.y);
    ReleaseDC(nullptr, dc);
    return value;
  };
  ASSERT_TRUE(composition_->Present(layers, 3, draw));
  const COLORREF top = pixel(20);
  EXPECT_LE(GetRValue(top), 3);
  EXPECT_NEAR(GetGValue(top), 127, 3);
  EXPECT_NEAR(GetBValue(top), 128, 3);
  EXPECT_EQ(pixel(65), RGB(0, 255, 0));
  ASSERT_TRUE(composition_->Present(layers, 2, draw));
  EXPECT_EQ(pixel(20), RGB(0, 255, 0));
  ASSERT_TRUE(composition_->Present(layers, 1, draw));
  EXPECT_EQ(pixel(20), RGB(255, 0, 0));
}

// Detached targets reject new visuals and frames while existing leases are
// safe.
TEST_F(NativeCompositionTest, RejectsDetachedTargetsAndInvalidScale) {
  int64_t id;
  Microsoft::WRL::ComPtr<IUnknown> visual;
  ASSERT_TRUE(composition_->CreateVisual(&id, &visual));
  EXPECT_FALSE(composition_->SetScale(id, 0));
  EXPECT_FALSE(
      composition_->SetScale(id, std::numeric_limits<double>::quiet_NaN()));
  composition_->Detach();
  EXPECT_FALSE(composition_->CreateVisual(&id, &visual));
  EXPECT_FALSE(composition_->Present(
      nullptr, 0, [](const FlutterLayer&, ID3D11Texture2D*) { return true; }));
  composition_->RemoveVisual(id);
}
}  // namespace
}  // namespace testing
}  // namespace flutter

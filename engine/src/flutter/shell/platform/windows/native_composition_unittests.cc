// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
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

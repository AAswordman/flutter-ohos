// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "flutter/shell/platform/windows/native_composition.h"
#include <atomic>
#include <cmath>
#include <unordered_set>
#include "flutter/fml/logging.h"
#define CHECK_COM(expression) CheckCompositionResult((expression), __LINE__)
namespace flutter {
namespace {
std::atomic<int64_t> next_native_id{INT64_C(1) << 40};
// Logs a COM failure at the operation that caused it.
bool CheckCompositionResult(HRESULT result, int line) {
  if (FAILED(result)) {
    FML_LOG(ERROR) << "Native composition failed at line " << line << ": "
                   << std::hex << result;
    return false;
  }
  return true;
}
}  // namespace
// Creates a native compositor without changing the view on initialization
// failure.
std::shared_ptr<NativeComposition> NativeComposition::Create(
    HWND window,
    ID3D11Device* device) {
  auto result = std::make_shared<NativeComposition>();
  if (!result->Initialize(window, device))
    return nullptr;
  return result;
}
// Initializes a topmost composition target on the Flutter child window.
bool NativeComposition::Initialize(HWND window, ID3D11Device* device) {
  d3d_device_ = device;
  ComPtr<IDXGIDevice> dxgi;
  ComPtr<IDXGIAdapter> adapter;
  return CHECK_COM(device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
         CHECK_COM(dxgi->GetAdapter(&adapter)) &&
         CHECK_COM(adapter->GetParent(IID_PPV_ARGS(&factory_))) &&
         CHECK_COM(
             DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&device_))) &&
         CHECK_COM(device_->CreateTargetForHwnd(window, TRUE, &target_)) &&
         CHECK_COM(device_->CreateVisual(&root_)) &&
         CHECK_COM(target_->SetRoot(root_.Get())) &&
         CHECK_COM(device_->Commit());
}
// Releases target ownership while outstanding plugin leases remain safe to
// destroy.
void NativeComposition::Detach() {
  std::scoped_lock lock(mutex_);
  if (target_) {
    target_->SetRoot(nullptr);
    root_->RemoveAllVisuals();
    device_->Commit();
    target_.Reset();
    flutter_visuals_.clear();
  }
}
// Creates an empty native visual; returned COM ownership belongs to the caller.
bool NativeComposition::CreateVisual(int64_t* id, IUnknown** visual) {
  std::scoped_lock lock(mutex_);
  if (!target_)
    return false;
  NativeVisual entry;
  if (!CHECK_COM(device_->CreateVisual(&entry.visual)))
    return false;
  *id = next_native_id.fetch_add(1);
  if (!CHECK_COM(entry.visual.CopyTo(visual)))
    return false;
  native_visuals_.emplace(*id, std::move(entry));
  return true;
}
// Hides disposed browser content immediately, including between Flutter frames.
void NativeComposition::RemoveVisual(int64_t id) {
  std::scoped_lock lock(mutex_);
  const auto it = native_visuals_.find(id);
  if (it == native_visuals_.end())
    return;
  ComPtr<IDCompositionEffectGroup> hidden;
  if (CHECK_COM(device_->CreateEffectGroup(&hidden)) &&
      CHECK_COM(hidden->SetOpacity(0.0f))) {
    it->second.visual->SetEffect(hidden.Get());
  }
  native_visuals_.erase(it);
  device_->Commit();
}
// Normalizes browser physical pixels before applying Flutter logical
// transforms.
bool NativeComposition::SetScale(int64_t id, double scale) {
  if (!std::isfinite(scale) || scale <= 0)
    return false;
  std::scoped_lock lock(mutex_);
  auto it = native_visuals_.find(id);
  if (it == native_visuals_.end())
    return false;
  it->second.scale = scale;
  return true;
}
// Retains composition swapchains across frames and replaces them only on
// resize.
bool NativeComposition::PrepareFlutterVisual(size_t index,
                                             UINT width,
                                             UINT height) {
  if (flutter_visuals_.size() <= index)
    flutter_visuals_.resize(index + 1);
  auto& layer = flutter_visuals_[index];
  if (layer.swap_chain && layer.width == width && layer.height == height)
    return true;
  FlutterVisual replacement;
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  if (!CHECK_COM(factory_->CreateSwapChainForComposition(
          d3d_device_.Get(), &desc, nullptr, &replacement.swap_chain)) ||
      !CHECK_COM(device_->CreateVisual(&replacement.visual)) ||
      !CHECK_COM(replacement.visual->SetContent(replacement.swap_chain.Get())))
    return false;
  replacement.width = width;
  replacement.height = height;
  layer = std::move(replacement);
  return true;
}
// Nests visual mutations so clipping remains in its original coordinate space.
bool NativeComposition::ApplyMutations(const FlutterPlatformView& view,
                                       IDCompositionVisual** result) {
  const auto found = native_visuals_.find(view.identifier);
  if (found == native_visuals_.end())
    return false;
  ComPtr<IDCompositionVisual> current;
  if (!CHECK_COM(device_->CreateVisual(&current)) ||
      !CHECK_COM(
          current->AddVisual(found->second.visual.Get(), FALSE, nullptr)))
    return false;
  frame_parents_.push_back(current);
  const float inverse_scale = static_cast<float>(1.0 / found->second.scale);
  const D2D_MATRIX_3X2_F scale = {inverse_scale, 0, 0, inverse_scale, 0, 0};
  if (!CHECK_COM(current->SetTransform(scale)))
    return false;
  for (size_t i = view.mutations_count; i > 0; --i) {
    const auto& mutation = *view.mutations[i - 1];
    ComPtr<IDCompositionVisual> wrapper;
    if (!CHECK_COM(device_->CreateVisual(&wrapper)) ||
        !CHECK_COM(wrapper->AddVisual(current.Get(), FALSE, nullptr)))
      return false;
    frame_parents_.push_back(wrapper);
    switch (mutation.type) {
      case kFlutterPlatformViewMutationTypeOpacity: {
        ComPtr<IDCompositionEffectGroup> effect;
        if (!CHECK_COM(device_->CreateEffectGroup(&effect)) ||
            !CHECK_COM(
                effect->SetOpacity(static_cast<float>(mutation.opacity))) ||
            !CHECK_COM(wrapper->SetEffect(effect.Get())))
          return false;
        break;
      }
      case kFlutterPlatformViewMutationTypeTransformation: {
        const auto& m = mutation.transformation;
        ComPtr<IDCompositionMatrixTransform3D> transform;
        D3DMATRIX matrix = {};
        matrix._11 = static_cast<float>(m.scaleX);
        matrix._12 = static_cast<float>(m.skewY);
        matrix._14 = static_cast<float>(m.pers0);
        matrix._21 = static_cast<float>(m.skewX);
        matrix._22 = static_cast<float>(m.scaleY);
        matrix._24 = static_cast<float>(m.pers1);
        matrix._33 = 1;
        matrix._41 = static_cast<float>(m.transX);
        matrix._42 = static_cast<float>(m.transY);
        matrix._44 = static_cast<float>(m.pers2);
        if (!CHECK_COM(device_->CreateMatrixTransform3D(&transform)) ||
            !CHECK_COM(transform->SetMatrix(matrix)) ||
            !CHECK_COM(wrapper->SetEffect(transform.Get())))
          return false;
        break;
      }
      case kFlutterPlatformViewMutationTypeClipRect: {
        const auto& r = mutation.clip_rect;
        D2D_RECT_F clip = {
            static_cast<float>(r.left), static_cast<float>(r.top),
            static_cast<float>(r.right), static_cast<float>(r.bottom)};
        if (!CHECK_COM(wrapper->SetClip(clip)))
          return false;
        break;
      }
      case kFlutterPlatformViewMutationTypeClipRoundedRect: {
        const auto& r = mutation.clip_rounded_rect;
        ComPtr<IDCompositionRectangleClip> clip;
        if (!CHECK_COM(device_->CreateRectangleClip(&clip)) ||
            !CHECK_COM(clip->SetLeft(r.rect.left)) ||
            !CHECK_COM(clip->SetTop(r.rect.top)) ||
            !CHECK_COM(clip->SetRight(r.rect.right)) ||
            !CHECK_COM(clip->SetBottom(r.rect.bottom)) ||
            !CHECK_COM(
                clip->SetTopLeftRadiusX(r.upper_left_corner_radius.width)) ||
            !CHECK_COM(
                clip->SetTopLeftRadiusY(r.upper_left_corner_radius.height)) ||
            !CHECK_COM(
                clip->SetTopRightRadiusX(r.upper_right_corner_radius.width)) ||
            !CHECK_COM(
                clip->SetTopRightRadiusY(r.upper_right_corner_radius.height)) ||
            !CHECK_COM(
                clip->SetBottomLeftRadiusX(r.lower_left_corner_radius.width)) ||
            !CHECK_COM(clip->SetBottomLeftRadiusY(
                r.lower_left_corner_radius.height)) ||
            !CHECK_COM(clip->SetBottomRightRadiusX(
                r.lower_right_corner_radius.width)) ||
            !CHECK_COM(clip->SetBottomRightRadiusY(
                r.lower_right_corner_radius.height)) ||
            !CHECK_COM(wrapper->SetClip(clip.Get())))
          return false;
        break;
      }
    }
    current = std::move(wrapper);
  }
  return CHECK_COM(current.CopyTo(result));
}
// Imports only Flutter swapchain buffers into ANGLE; native content stays in
// DWM.
bool NativeComposition::Present(
    const FlutterLayer** layers,
    size_t count,
    const std::function<bool(const FlutterLayer&, ID3D11Texture2D*)>& draw) {
  std::scoped_lock lock(mutex_);
  if (!target_)
    return false;
  // A DComp visual has one parent. Queue detaches before rebuilding; all edits
  // become visible together at Commit, preserving the last displayed frame.
  for (auto& parent : frame_parents_) {
    if (!CHECK_COM(parent->RemoveAllVisuals()))
      return false;
  }
  frame_parents_.clear();
  ComPtr<IDCompositionVisual> frame;
  if (!CHECK_COM(device_->CreateVisual(&frame)))
    return false;
  frame_parents_.push_back(frame);
  std::unordered_set<int64_t> seen;
  size_t backing_index = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto& layer = *layers[i];
    ComPtr<IDCompositionVisual> visual;
    if (layer.type == kFlutterLayerContentTypePlatformView) {
      if (!seen.insert(layer.platform_view->identifier).second ||
          !ApplyMutations(*layer.platform_view, &visual))
        return false;
    } else if (layer.type == kFlutterLayerContentTypeBackingStore) {
      const auto width = static_cast<UINT>(layer.size.width);
      const auto height = static_cast<UINT>(layer.size.height);
      if (width == 0 || height == 0)
        continue;
      if (!PrepareFlutterVisual(backing_index, width, height))
        return false;
      auto& backing = flutter_visuals_[backing_index++];
      ComPtr<ID3D11Texture2D> buffer;
      if (!CHECK_COM(backing.swap_chain->GetBuffer(0, IID_PPV_ARGS(&buffer))))
        return false;
      if (!draw(layer, buffer.Get()))
        return false;
      DXGI_PRESENT_PARAMETERS present = {};
      if (!CHECK_COM(backing.swap_chain->Present1(0, 0, &present)))
        return false;
      visual = backing.visual;
      if (!CHECK_COM(visual->SetOffsetX(static_cast<float>(layer.offset.x))) ||
          !CHECK_COM(visual->SetOffsetY(static_cast<float>(layer.offset.y))))
        return false;
    } else {
      return false;
    }
    if (!CHECK_COM(frame->AddVisual(visual.Get(), TRUE, nullptr)))
      return false;
  }
  if (!CHECK_COM(root_->RemoveAllVisuals()) ||
      !CHECK_COM(root_->AddVisual(frame.Get(), FALSE, nullptr)) ||
      !CHECK_COM(device_->Commit()))
    return false;
  flutter_visuals_.resize(backing_index);
  return true;
}
}  // namespace flutter

#undef CHECK_COM

// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_GPU_GPU_SURFACE_VULKAN_IMPELLER_H_
#define FLUTTER_SHELL_GPU_GPU_SURFACE_VULKAN_IMPELLER_H_

#include "flutter/common/graphics/gl_context_switch.h"
#include "flutter/flow/surface.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/memory/weak_ptr.h"
#include "flutter/impeller/display_list/aiks_context.h"
#include "flutter/impeller/renderer/context.h"
#include "flutter/shell/gpu/gpu_surface_vulkan_delegate.h"
#include "impeller/renderer/backend/vulkan/swapchain/swapchain_transients_vk.h"

namespace flutter {

class GPUSurfaceVulkanImpeller final : public Surface {
 public:
  /// @brief Whether partial repaint is enabled unless the embedder says
  ///        otherwise. It is on by default on OHOS where it has been validated
  ///        and opt-in elsewhere (e.g. via the --enable-partial-repaint
  ///        switch).
#ifdef __OHOS__
  static constexpr bool kDefaultEnablePartialRepaint = true;
#else
  static constexpr bool kDefaultEnablePartialRepaint = false;
#endif

  explicit GPUSurfaceVulkanImpeller(
      GPUSurfaceVulkanDelegate* delegate,
      std::shared_ptr<impeller::Context> context,
      bool enable_partial_repaint = kDefaultEnablePartialRepaint);

  // |Surface|
  ~GPUSurfaceVulkanImpeller() override;

  // |Surface|
  bool IsValid() override;

  void SetOhosDelegate(GPUSurfaceVulkanDelegate* ohos_delegate) {
    this->ohos_delegate_ = ohos_delegate;
  };

 private:
  GPUSurfaceVulkanDelegate* delegate_;
  // Only assigned on OHOS; on other platforms it must stay null or the
  // submit callback calls SetPresentInfo through uninitialized memory.
  GPUSurfaceVulkanDelegate* ohos_delegate_ = nullptr;
  std::shared_ptr<impeller::Context> impeller_context_;
  std::shared_ptr<impeller::AiksContext> aiks_context_;
  std::shared_ptr<impeller::SwapchainTransientsVK> transients_;
  bool is_valid_ = false;

  bool disable_partial_repaint_ = !kDefaultEnablePartialRepaint;

  // Frame counter used to rate-limit partial repaint diagnostics logs.
  uint32_t frames_since_partial_repaint_log_ = 0;

  // Accumulated damage for each framebuffer; Key is the swapchain image index
  // for each drawable.
  std::map<int, DlIRect> damage_;

  // |Surface|
  std::unique_ptr<SurfaceFrame> AcquireFrame(const DlISize& size) override;

  // |Surface|
  DlMatrix GetRootTransformation() const override;

  // |Surface|
  GrDirectContext* GetContext() override;

  // |Surface|
  std::unique_ptr<GLContextResult> MakeRenderContextCurrent() override;

  // |Surface|
  bool EnableRasterCache() const override;

  // |Surface|
  std::shared_ptr<impeller::AiksContext> GetAiksContext() const override;

  // |Surface|
  Surface::SurfaceDamageData GetSurfaceDamageData() const override;

  FML_DISALLOW_COPY_AND_ASSIGN(GPUSurfaceVulkanImpeller);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_GPU_GPU_SURFACE_VULKAN_IMPELLER_H_

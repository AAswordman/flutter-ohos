// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/gpu/gpu_surface_gl_impeller.h"

#include <memory>
#include <optional>
#include <string>

#include "flow/surface_frame.h"
#include "flutter/fml/make_copyable.h"
#include "flutter/fml/trace_event.h"
#include "impeller/display_list/dl_dispatcher.h"
#include "impeller/renderer/backend/gles/surface_gles.h"
#include "impeller/typographer/backends/skia/typographer_context_skia.h"

namespace flutter {

GPUSurfaceGLImpeller::GPUSurfaceGLImpeller(
    GPUSurfaceGLDelegate* delegate,
    std::shared_ptr<impeller::Context> context,
    bool render_to_surface)
    : weak_factory_(this) {
  if (delegate == nullptr) {
    return;
  }

  if (!context || !context->IsValid()) {
    return;
  }

  auto aiks_context = std::make_shared<impeller::AiksContext>(
      context, impeller::TypographerContextSkia::Make());

  if (!aiks_context->IsValid()) {
    return;
  }

  delegate_ = delegate;
  impeller_context_ = std::move(context);
  render_to_surface_ = render_to_surface;
  aiks_context_ = std::move(aiks_context);
  is_valid_ = true;
}

// |Surface|
GPUSurfaceGLImpeller::~GPUSurfaceGLImpeller() = default;

// |Surface|
bool GPUSurfaceGLImpeller::IsValid() {
  return is_valid_;
}

// |Surface|
std::unique_ptr<SurfaceFrame> GPUSurfaceGLImpeller::AcquireFrame(
    const DlISize& size) {
  if (!IsValid()) {
    FML_LOG(ERROR) << "OpenGL surface was invalid.";
    return nullptr;
  }

  // Shared between the encode, submit, and swap callbacks so that the damage
  // computed for the frame reaches the delegate at present time.
  struct FrameDamageState {
    std::optional<DlIRect> frame_damage;
    std::optional<DlIRect> buffer_damage;
  };
  auto frame_damage_state = std::make_shared<FrameDamageState>();

  auto swap_callback = [weak = weak_factory_.GetWeakPtr(), delegate = delegate_,
                        frame_damage_state]() -> bool {
    if (weak) {
      GLPresentInfo present_info = {
          .fbo_id = 0u,
          .frame_damage = frame_damage_state->frame_damage,
          // TODO (https://github.com/flutter/flutter/issues/105597): wire-up
          // presentation time to impeller backend.
          .presentation_time = std::nullopt,
          .buffer_damage = frame_damage_state->buffer_damage,
      };
      delegate->GLContextPresent(present_info);
    }
    return true;
  };

  auto context_switch = delegate_->GLContextMakeCurrent();
  if (!context_switch->GetResult()) {
    FML_LOG(ERROR)
        << "Could not make the context current to acquire the frame.";
    return nullptr;
  }

  if (!render_to_surface_) {
    return std::make_unique<SurfaceFrame>(
        nullptr, SurfaceFrame::FramebufferInfo{.supports_readback = true},
        [](const SurfaceFrame& surface_frame, DlCanvas* canvas) {
          return true;
        },
        [](const SurfaceFrame& surface_frame) { return true; }, size);
  }

  GLFrameInfo frame_info = {static_cast<uint32_t>(size.width),
                            static_cast<uint32_t>(size.height)};
  const GLFBOInfo fbo_info = delegate_->GLContextFBO(frame_info);
  auto surface = impeller::SurfaceGLES::WrapFBO(
      impeller_context_,                         // context
      swap_callback,                             // swap_callback
      fbo_info.fbo_id,                           // fbo
      impeller::PixelFormat::kR8G8B8A8UNormInt,  // color_format
      impeller::ISize{size.width, size.height}   // fbo_size
  );

  impeller::RenderTarget render_target = surface->GetRenderTarget();

  SurfaceFrame::EncodeCallback encode_callback =
      [aiks_context = aiks_context_,  //
       render_target,                 //
       frame_damage_state,            //
       this](SurfaceFrame& surface_frame, DlCanvas* canvas) mutable -> bool {
    if (!aiks_context) {
      return false;
    }

    auto display_list = surface_frame.BuildDisplayList();
    if (!display_list) {
      FML_LOG(ERROR) << "Could not build display list for surface frame.";
      return false;
    }

    auto buffer_damage = surface_frame.submit_info().buffer_damage;
    if (buffer_damage.has_value()) {
      if (buffer_damage->IsEmpty()) {
        // No damage to render. Skip rendering.
        TRACE_EVENT0("flutter", "SkipFrameNoDamage");
        return true;
      }
      render_target.SetRenderArea(impeller::IRect::MakeXYWH(
          buffer_damage->GetX(), buffer_damage->GetY(),
          buffer_damage->GetWidth(), buffer_damage->GetHeight()));
      ++frames_since_partial_repaint_log_;
      if (frames_since_partial_repaint_log_ <= 5 ||
          frames_since_partial_repaint_log_ % 300 == 0) {
        const auto& frame_damage = surface_frame.submit_info().frame_damage;
        const std::string frame_damage_text =
            frame_damage.has_value()
                ? std::to_string(frame_damage->GetWidth()) + "x" +
                      std::to_string(frame_damage->GetHeight())
                : std::string("none");
        FML_LOG(INFO) << "PartialRepaint(GL) #"
                      << frames_since_partial_repaint_log_
                      << ": frame_damage=" << frame_damage_text
                      << " buffer_damage="
                      << std::to_string(buffer_damage->GetWidth()) + "x" +
                             std::to_string(buffer_damage->GetHeight());
      }
    }

    auto cull_rect =
        impeller::Rect::MakeSize(render_target.GetRenderTargetSize());
    return impeller::RenderToTarget(aiks_context->GetContentContext(),  //
                                    render_target,                      //
                                    display_list,                       //
                                    cull_rect,                          //
                                    /*reset_host_buffer=*/true          //
    );
  };

  return std::make_unique<SurfaceFrame>(
      nullptr,                                // surface
      delegate_->GLContextFramebufferInfo(),  // framebuffer info
      encode_callback,                        // encode callback
      fml::MakeCopyable([surface = std::move(surface), delegate = delegate_,
                         frame_damage_state](
                            const SurfaceFrame& surface_frame) {
        frame_damage_state->frame_damage =
            surface_frame.submit_info().frame_damage;
        frame_damage_state->buffer_damage =
            surface_frame.submit_info().buffer_damage;
        // Set the damage region right before the swap, mirroring the order
        // used by the Skia GL surface. Some drivers reject calls made earlier
        // in the frame.
        delegate->GLContextSetDamageRegion(frame_damage_state->buffer_damage);
        return surface->Present();
      }),                         // submit callback
      size,                       // frame size
      std::move(context_switch),  // context result
      true                        // display list fallback
  );
}

// |Surface|
DlMatrix GPUSurfaceGLImpeller::GetRootTransformation() const {
  // This backend does not currently support root surface transformations. Just
  // return identity.
  return {};
}

// |Surface|
GrDirectContext* GPUSurfaceGLImpeller::GetContext() {
  // Impeller != Skia.
  return nullptr;
}

// |Surface|
std::unique_ptr<GLContextResult>
GPUSurfaceGLImpeller::MakeRenderContextCurrent() {
  return delegate_->GLContextMakeCurrent();
}

// |Surface|
bool GPUSurfaceGLImpeller::ClearRenderContext() {
  return delegate_->GLContextClearCurrent();
}

bool GPUSurfaceGLImpeller::AllowsDrawingWhenGpuDisabled() const {
  return delegate_->AllowsDrawingWhenGpuDisabled();
}

// |Surface|
bool GPUSurfaceGLImpeller::EnableRasterCache() const {
  return false;
}

// |Surface|
std::shared_ptr<impeller::AiksContext> GPUSurfaceGLImpeller::GetAiksContext()
    const {
  return aiks_context_;
}

}  // namespace flutter

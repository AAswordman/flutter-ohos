// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/android_egl_surface.h"

#include <EGL/eglext.h>
#include <sys/system_properties.h>

#include <array>
#include <deque>
#include <list>
#include <string>

#include "flutter/fml/trace_event.h"

namespace flutter {

void LogLastEGLError() {
  struct EGLNameErrorPair {
    const char* name;
    EGLint code;
  };

#define _EGL_ERROR_DESC(a) \
  {                        \
    #a, a                  \
  }

  const EGLNameErrorPair pairs[] = {
      _EGL_ERROR_DESC(EGL_SUCCESS),
      _EGL_ERROR_DESC(EGL_NOT_INITIALIZED),
      _EGL_ERROR_DESC(EGL_BAD_ACCESS),
      _EGL_ERROR_DESC(EGL_BAD_ALLOC),
      _EGL_ERROR_DESC(EGL_BAD_ATTRIBUTE),
      _EGL_ERROR_DESC(EGL_BAD_CONTEXT),
      _EGL_ERROR_DESC(EGL_BAD_CONFIG),
      _EGL_ERROR_DESC(EGL_BAD_CURRENT_SURFACE),
      _EGL_ERROR_DESC(EGL_BAD_DISPLAY),
      _EGL_ERROR_DESC(EGL_BAD_SURFACE),
      _EGL_ERROR_DESC(EGL_BAD_MATCH),
      _EGL_ERROR_DESC(EGL_BAD_PARAMETER),
      _EGL_ERROR_DESC(EGL_BAD_NATIVE_PIXMAP),
      _EGL_ERROR_DESC(EGL_BAD_NATIVE_WINDOW),
      _EGL_ERROR_DESC(EGL_CONTEXT_LOST),
  };

#undef _EGL_ERROR_DESC

  const auto count = sizeof(pairs) / sizeof(EGLNameErrorPair);

  EGLint last_error = eglGetError();

  for (size_t i = 0; i < count; i++) {
    if (last_error == pairs[i].code) {
      FML_LOG(ERROR) << "EGL Error: " << pairs[i].name << " (" << pairs[i].code
                     << ")";
      return;
    }
  }

  FML_LOG(ERROR) << "Unknown EGL Error";
}

namespace {

// The maximum number of frames worth of damage to remember. Some devices use
// quadruple buffering, so the age of a back buffer may be as high as 4.
constexpr size_t kMaxDamageHistorySize = 4u;

bool HasEGLExtension(EGLDisplay display, const char* extension) {
  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
  if (extensions == nullptr) {
    return false;
  }
  const std::string all_extensions(extensions);
  const std::string target(extension);
  size_t position = all_extensions.find(target);
  while (position != std::string::npos) {
    const bool starts_at_boundary =
        position == 0u || all_extensions[position - 1u] == ' ';
    const size_t end = position + target.size();
    const bool ends_at_boundary =
        end == all_extensions.size() || all_extensions[end] == ' ';
    if (starts_at_boundary && ends_at_boundary) {
      return true;
    }
    position = all_extensions.find(target, position + 1u);
  }
  return false;
}

// Convert a rect from the top-left origin space used by the engine to the
// bottom-left origin space used by EGL. Returns false if the surface size is
// unknown.
bool RectToEGL(EGLDisplay display,
               EGLSurface surface,
               const DlIRect& rect,
               EGLint out_rect[4]) {
  EGLint height = 0;
  if (eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE) {
    return false;
  }
  out_rect[0] = static_cast<EGLint>(rect.GetX());
  out_rect[1] = static_cast<EGLint>(height - (rect.GetY() + rect.GetHeight()));
  out_rect[2] = static_cast<EGLint>(rect.GetWidth());
  out_rect[3] = static_cast<EGLint>(rect.GetHeight());
  return true;
}

}  // namespace

class AndroidEGLSurfaceDamage {
 public:
  void init(EGLDisplay display, EGLContext context) {
    if (display == EGL_NO_DISPLAY) {
      return;
    }

    // EGL_KHR_partial_update is intentionally not used: Adreno drivers reject
    // eglSetDamageRegionKHR with EGL_BAD_ACCESS, and the region is only a
    // tile-rendering optimization - correctness comes from the buffer age and
    // the swap-with-damage hint below.

    if (HasEGLExtension(display, "EGL_EXT_swap_buffers_with_damage")) {
      swap_buffers_with_damage_ =
          reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC>(
              eglGetProcAddress("eglSwapBuffersWithDamageEXT"));
    }
    if (swap_buffers_with_damage_ == nullptr &&
        HasEGLExtension(display, "EGL_KHR_swap_buffers_with_damage")) {
      swap_buffers_with_damage_ =
          reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC>(
              eglGetProcAddress("eglSwapBuffersWithDamageKHR"));
    }

    has_buffer_age_ = HasEGLExtension(display, "EGL_EXT_buffer_age");

    FML_LOG(IMPORTANT) << "EGL damage init: swap_with_damage="
                       << (swap_buffers_with_damage_ != nullptr)
                       << " set_damage_region="
                       << (set_damage_region_ != nullptr)
                       << " buffer_age=" << has_buffer_age_;
  }

  void SetDamageRegion(EGLDisplay display,
                       EGLSurface surface,
                       const std::optional<DlIRect>& region) {
    if (set_damage_region_ == nullptr || !region.has_value()) {
      return;
    }
    EGLint rect[4] = {};
    if (!RectToEGL(display, surface, *region, rect)) {
      return;
    }
    if (set_damage_region_(display, surface, rect, 1) != EGL_TRUE) {
      LogLastEGLError();
    }
  }

  bool SupportsPartialRepaint() const {
    // EGL_EXT_buffer_age is required. Without it the stale region of a back
    // buffer cannot be known reliably: the age is unbounded when frames are
    // intermittent (e.g. after idle), assuming a worst case under-repaints,
    // and some drivers additionally mishandle the swap-with-damage hint.
    return has_buffer_age_ && swap_buffers_with_damage_ != nullptr;
  }

  std::optional<DlIRect> InitialDamage(EGLDisplay display, EGLSurface surface) {
    if (!SupportsPartialRepaint()) {
      return std::nullopt;
    }

    EGLint buffer_age = 0;
    if (has_buffer_age_) {
      if (eglQuerySurface(display, surface, EGL_BUFFER_AGE_EXT, &buffer_age) !=
              EGL_TRUE ||
          buffer_age <= 0) {
        // The contents of the back buffer are undefined. Repaint everything.
        return std::nullopt;
      }
    } else {
      // No buffer age extension: assume the maximum buffering this device
      // may use, so the union below covers every possibly-stale region.
      buffer_age = static_cast<EGLint>(kMaxDamageHistorySize);
    }

    if (buffer_age == 1) {
      // This back buffer was presented on the previous frame and is up to date.
      return DlIRect();
    }

    const size_t required_history = static_cast<size_t>(buffer_age - 1);
    if (damage_history_.size() < required_history) {
      // Not enough history to determine what is stale. Repaint everything.
      return std::nullopt;
    }

    DlIRect existing_damage;
    size_t i = 0u;
    for (auto it = damage_history_.rbegin();
         it != damage_history_.rend() && i < required_history; ++it, ++i) {
      existing_damage = existing_damage.Union(*it);
    }
    return existing_damage;
  }

  bool SwapBuffersWithDamage(EGLDisplay display,
                             EGLSurface surface,
                             const std::optional<DlIRect>& damage) {
    bool result = false;
    EGLint rect[4] = {};
    if (damage.has_value() && swap_buffers_with_damage_ != nullptr &&
        RectToEGL(display, surface, *damage, rect)) {
      result = swap_buffers_with_damage_(display, surface, rect, 1) == EGL_TRUE;
      if (!result) {
        LogLastEGLError();
      }
    } else {
      result = eglSwapBuffers(display, surface) == EGL_TRUE;
    }
    if (result) {
      RecordFrameDamage(display, surface, damage);
    }
    return result;
  }

 private:
  void RecordFrameDamage(EGLDisplay display,
                         EGLSurface surface,
                         const std::optional<DlIRect>& damage) {
    DlIRect recorded;
    if (damage.has_value()) {
      recorded = *damage;
    } else {
      EGLint width = 0;
      EGLint height = 0;
      if (eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
          eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE) {
        return;
      }
      recorded = DlIRect::MakeWH(width, height);
    }
    damage_history_.push_back(recorded);
    while (damage_history_.size() > kMaxDamageHistorySize) {
      damage_history_.pop_front();
    }
  }

  PFNEGLSETDAMAGEREGIONKHRPROC set_damage_region_ = nullptr;
  PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC swap_buffers_with_damage_ = nullptr;
  bool has_buffer_age_ = false;
  std::deque<DlIRect> damage_history_;
};

AndroidEGLSurface::AndroidEGLSurface(EGLSurface surface,
                                     EGLDisplay display,
                                     EGLContext context)
    : surface_(surface),
      display_(display),
      context_(context),
      damage_(std::make_unique<AndroidEGLSurfaceDamage>()) {
  damage_->init(display_, context);
}

AndroidEGLSurface::~AndroidEGLSurface() {
  [[maybe_unused]] auto result = eglDestroySurface(display_, surface_);
  FML_DCHECK(result == EGL_TRUE);
}

bool AndroidEGLSurface::IsValid() const {
  return surface_ != EGL_NO_SURFACE;
}

bool AndroidEGLSurface::IsContextCurrent() const {
  EGLContext current_egl_context = eglGetCurrentContext();
  if (context_ != current_egl_context) {
    return false;
  }

  EGLDisplay current_egl_display = eglGetCurrentDisplay();
  if (display_ != current_egl_display) {
    return false;
  }

  EGLSurface draw_surface = eglGetCurrentSurface(EGL_DRAW);
  if (draw_surface != surface_) {
    return false;
  }

  EGLSurface read_surface = eglGetCurrentSurface(EGL_READ);
  if (read_surface != surface_) {
    return false;
  }

  return true;
}

AndroidEGLSurfaceMakeCurrentStatus AndroidEGLSurface::MakeCurrent() const {
  if (IsContextCurrent()) {
    return AndroidEGLSurfaceMakeCurrentStatus::kSuccessAlreadyCurrent;
  }
  if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
    FML_LOG(ERROR) << "Could not make the context current";
    LogLastEGLError();
    return AndroidEGLSurfaceMakeCurrentStatus::kFailure;
  }
  return AndroidEGLSurfaceMakeCurrentStatus::kSuccessMadeCurrent;
}

void AndroidEGLSurface::SetDamageRegion(
    const std::optional<DlIRect>& buffer_damage) {
  damage_->SetDamageRegion(display_, surface_, buffer_damage);
}

bool AndroidEGLSurface::SetPresentationTime(
    const fml::TimePoint& presentation_time) {
  if (presentation_time_proc_) {
    const auto time_ns = presentation_time.ToEpochDelta().ToNanoseconds();
    return presentation_time_proc_(display_, surface_, time_ns);
  } else {
    return false;
  }
}

bool AndroidEGLSurface::SwapBuffers(
    const std::optional<DlIRect>& surface_damage) {
  TRACE_EVENT0("flutter", "AndroidContextGL::SwapBuffers");
  return damage_->SwapBuffersWithDamage(display_, surface_, surface_damage);
}

bool AndroidEGLSurface::SupportsPartialRepaint() const {
  return damage_->SupportsPartialRepaint();
}

std::optional<DlIRect> AndroidEGLSurface::InitialDamage() {
  return damage_->InitialDamage(display_, surface_);
}

DlISize AndroidEGLSurface::GetSize() const {
  EGLint width = 0;
  EGLint height = 0;

  if (!eglQuerySurface(display_, surface_, EGL_WIDTH, &width) ||
      !eglQuerySurface(display_, surface_, EGL_HEIGHT, &height)) {
    FML_LOG(ERROR) << "Unable to query EGL surface size";
    LogLastEGLError();
    return DlISize();
  }
  return DlISize(width, height);
}

}  // namespace flutter

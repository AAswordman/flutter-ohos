// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/egl/surface.h"

#include <string>

#include "flutter/fml/logging.h"

namespace impeller {
namespace egl {

namespace {

// The maximum number of frames worth of damage to remember. Some devices use
// quadruple buffering, so the age of a back buffer may be as high as 4.
constexpr size_t kMaxDamageHistorySize = 4u;

bool HasEGLExtension(EGLDisplay display, const char* extension) {
  const char* extensions = ::eglQueryString(display, EGL_EXTENSIONS);
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

}  // namespace

Surface::Surface(EGLDisplay display, EGLSurface surface)
    : display_(display), surface_(surface) {
  InitDamageSupport();
}

Surface::~Surface() {
  if (surface_ != EGL_NO_SURFACE) {
    if (::eglDestroySurface(display_, surface_) != EGL_TRUE) {
      IMPELLER_LOG_EGL_ERROR;
    }
  }
}

void Surface::InitDamageSupport() {
  if (display_ == EGL_NO_DISPLAY) {
    return;
  }

  // EGL_KHR_partial_update is intentionally not used: Adreno drivers reject
  // eglSetDamageRegionKHR with EGL_BAD_ACCESS, and the region is only a
  // tile-rendering optimization - correctness comes from the buffer age and
  // the swap-with-damage hint below.

  if (HasEGLExtension(display_, "EGL_EXT_swap_buffers_with_damage")) {
    swap_buffers_with_damage_ =
        reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC>(
            ::eglGetProcAddress("eglSwapBuffersWithDamageEXT"));
  }
  if (swap_buffers_with_damage_ == nullptr &&
      HasEGLExtension(display_, "EGL_KHR_swap_buffers_with_damage")) {
    swap_buffers_with_damage_ =
        reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC>(
            ::eglGetProcAddress("eglSwapBuffersWithDamageKHR"));
  }

  has_buffer_age_ = HasEGLExtension(display_, "EGL_EXT_buffer_age");

  FML_LOG(IMPORTANT) << "EGL damage init: swap_with_damage="
                     << (swap_buffers_with_damage_ != nullptr)
                     << " set_damage_region=" << (set_damage_region_ != nullptr)
                     << " buffer_age=" << has_buffer_age_;
}

const EGLSurface& Surface::GetHandle() const {
  return surface_;
}

bool Surface::IsValid() const {
  return surface_ != EGL_NO_SURFACE;
}

bool Surface::SupportsPartialRepaint() const {
  // EGL_EXT_buffer_age is required. Without it the stale region of a back
  // buffer cannot be known reliably: the age is unbounded when frames are
  // intermittent (e.g. after idle), assuming a worst case under-repaints, and
  // some drivers additionally mishandle the swap-with-damage hint.
  return has_buffer_age_ && swap_buffers_with_damage_ != nullptr;
}

bool Surface::RectToEGL(const IRect& rect, EGLint out_rect[4]) const {
  EGLint height = 0;
  if (::eglQuerySurface(display_, surface_, EGL_HEIGHT, &height) != EGL_TRUE) {
    return false;
  }
  out_rect[0] = static_cast<EGLint>(rect.GetX());
  // EGL uses a bottom-left origin while Impeller uses a top-left origin.
  out_rect[1] = static_cast<EGLint>(
      height - static_cast<EGLint>(rect.GetY() + rect.GetHeight()));
  out_rect[2] = static_cast<EGLint>(rect.GetWidth());
  out_rect[3] = static_cast<EGLint>(rect.GetHeight());
  return true;
}

std::optional<IRect> Surface::InitialDamage() const {
  if (!SupportsPartialRepaint()) {
    return std::nullopt;
  }

  EGLint buffer_age = 0;
  if (has_buffer_age_) {
    if (::eglQuerySurface(display_, surface_, EGL_BUFFER_AGE_EXT,
                          &buffer_age) != EGL_TRUE ||
        buffer_age <= 0) {
      // The contents of the back buffer are undefined. Repaint everything.
      return std::nullopt;
    }
  } else {
    // No buffer age extension: assume the maximum buffering this device may
    // use, so the union below covers every possibly-stale region.
    buffer_age = static_cast<EGLint>(kMaxDamageHistorySize);
  }

  if (buffer_age == 1) {
    // This back buffer was presented on the previous frame and is up to date.
    return IRect();
  }

  const size_t required_history = static_cast<size_t>(buffer_age - 1);
  if (damage_history_.size() < required_history) {
    // Not enough history to determine what is stale. Repaint everything.
    return std::nullopt;
  }

  IRect existing_damage;
  size_t i = 0u;
  for (auto it = damage_history_.rbegin();
       it != damage_history_.rend() && i < required_history; ++it, ++i) {
    existing_damage = existing_damage.Union(*it);
  }
  return existing_damage;
}

void Surface::SetDamageRegion(std::optional<IRect> buffer_damage) {
  if (set_damage_region_ == nullptr || !buffer_damage.has_value()) {
    return;
  }
  EGLint rect[4] = {};
  if (!RectToEGL(*buffer_damage, rect)) {
    return;
  }
  if (set_damage_region_(display_, surface_, rect, 1) != EGL_TRUE) {
    IMPELLER_LOG_EGL_ERROR;
    // The driver rejected the damage region. Disable partial repaint entirely
    // so that rendering falls back to full frames, which also repairs any
    // buffer contents that have already diverged.
    FML_LOG(ERROR)
        << "eglSetDamageRegionKHR failed, disabling partial repaint.";
    set_damage_region_ = nullptr;
    swap_buffers_with_damage_ = nullptr;
  }
}

void Surface::RecordFrameDamage(std::optional<IRect> frame_damage) {
  IRect recorded;
  if (frame_damage.has_value()) {
    recorded = *frame_damage;
  } else {
    EGLint width = 0;
    EGLint height = 0;
    if (::eglQuerySurface(display_, surface_, EGL_WIDTH, &width) != EGL_TRUE ||
        ::eglQuerySurface(display_, surface_, EGL_HEIGHT, &height) !=
            EGL_TRUE) {
      return;
    }
    recorded = IRect::MakeWH(width, height);
  }
  damage_history_.push_back(recorded);
  while (damage_history_.size() > kMaxDamageHistorySize) {
    damage_history_.pop_front();
  }
}

bool Surface::Present() const {
  const auto result = ::eglSwapBuffers(display_, surface_) == EGL_TRUE;
  if (!result) {
    IMPELLER_LOG_EGL_ERROR;
  }
  return result;
}

bool Surface::Present(std::optional<IRect> frame_damage) {
  bool result = false;
  EGLint rect[4] = {};
  if (frame_damage.has_value() && swap_buffers_with_damage_ != nullptr &&
      RectToEGL(*frame_damage, rect)) {
    result = swap_buffers_with_damage_(display_, surface_, rect, 1) == EGL_TRUE;
    if (!result) {
      IMPELLER_LOG_EGL_ERROR;
    }
  } else {
    result = ::eglSwapBuffers(display_, surface_) == EGL_TRUE;
    if (!result) {
      IMPELLER_LOG_EGL_ERROR;
    }
  }
  if (result) {
    RecordFrameDamage(frame_damage);
  }
  return result;
}

}  // namespace egl
}  // namespace impeller

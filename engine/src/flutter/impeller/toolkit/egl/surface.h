// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_EGL_SURFACE_H_
#define FLUTTER_IMPELLER_TOOLKIT_EGL_SURFACE_H_

#include <deque>
#include <optional>

#include "impeller/geometry/rect.h"
#include "impeller/toolkit/egl/egl.h"

namespace impeller {
namespace egl {

//------------------------------------------------------------------------------
/// @brief      An instance of an EGL surface. There is no ability to create
///             surfaces directly. Instead, one must be created using a Display
///             connection.
///
class Surface {
 public:
  ~Surface();

  //----------------------------------------------------------------------------
  /// @return     True if this is a valid surface.
  ///
  bool IsValid() const;

  //----------------------------------------------------------------------------
  /// @return     Get the handle to the underlying surface.
  ///
  const EGLSurface& GetHandle() const;

  //----------------------------------------------------------------------------
  /// @brief      Present the surface. For an offscreen pixel buffer surface,
  ///             this is a no-op.
  ///
  /// @return     True if the surface could be presented.
  ///
  bool Present() const;

  //----------------------------------------------------------------------------
  /// @brief      Present the surface, hinting that only the given region of
  ///             the surface changed. The region is in Impeller's top-left
  ///             origin coordinate space.
  ///
  /// @param[in]  frame_damage  The region of the frame that changed, or
  ///                           `std::nullopt` if the whole frame changed.
  ///
  /// @return     True if the surface could be presented.
  ///
  bool Present(std::optional<IRect> frame_damage);

  //----------------------------------------------------------------------------
  /// @return     Whether the surface can be presented partially, i.e. whether
  ///             the required EGL extensions are available.
  ///
  bool SupportsPartialRepaint() const;

  //----------------------------------------------------------------------------
  /// @brief      Get the region of the current back buffer that lags behind
  ///             the front buffer, i.e. the area that must be repainted in
  ///             addition to the current frame's damage.
  ///
  /// @return     The existing damage, an empty rect if the back buffer is up to
  ///             date, or `std::nullopt` if it cannot be determined and the
  ///             whole surface must be repainted.
  ///
  std::optional<IRect> InitialDamage() const;

  //----------------------------------------------------------------------------
  /// @brief      Set the region of the surface that will be modified this
  ///             frame. Corresponds to `eglSetDamageRegionKHR`.
  ///
  void SetDamageRegion(std::optional<IRect> buffer_damage);

 private:
  friend class Display;

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLSurface surface_ = EGL_NO_SURFACE;

  PFNEGLSETDAMAGEREGIONKHRPROC set_damage_region_ = nullptr;
  PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC swap_buffers_with_damage_ = nullptr;
  bool has_buffer_age_ = false;

  // Frame damage of the previous presents, newest last.
  std::deque<IRect> damage_history_;

  Surface(EGLDisplay display, EGLSurface surface);

  Surface(const Surface&) = delete;

  Surface& operator=(const Surface&) = delete;

  void InitDamageSupport();

  // Convert a rect from Impeller's top-left origin space to the bottom-left
  // origin space used by EGL. Returns false if the surface size is unknown.
  bool RectToEGL(const IRect& rect, EGLint out_rect[4]) const;

  void RecordFrameDamage(std::optional<IRect> frame_damage);
};

}  // namespace egl
}  // namespace impeller

#endif  // FLUTTER_IMPELLER_TOOLKIT_EGL_SURFACE_H_

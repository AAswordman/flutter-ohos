// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_COMPOSITOR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_COMPOSITOR_H_

#include <cairo.h>
#include <gdk/gdk.h>

#include <memory>
#include <vector>

#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/fl_framebuffer.h"

// Owns a copied backing store; the compositor pool releases GPU objects only
// after the engine stops, with the producing OpenGL context current.
struct FlCompositorBacking {
  FlFramebuffer* framebuffer = nullptr;
  cairo_surface_t* surface = nullptr;
  std::vector<uint8_t> pixels;

  // Releases copied pixels and framebuffer resources on the owning thread.
  ~FlCompositorBacking() {
    g_clear_object(&framebuffer);
    g_clear_pointer(&surface, cairo_surface_destroy);
  }
};

// Describes one immutable item in the Flutter embedder's paint order.
struct FlCompositorLayer {
  FlutterLayerContentType type;
  FlutterPoint offset;
  FlutterSize size;
  int64_t platform_view_id = 0;
  std::shared_ptr<FlCompositorBacking> backing;
};

// Retains all backing stores until GTK has finished drawing the entire scene.
struct FlCompositorFrame {
  std::vector<FlCompositorLayer> layers;
  size_t width = 0;
  size_t height = 0;
};

G_BEGIN_DECLS

G_DECLARE_DERIVABLE_TYPE(FlCompositor, fl_compositor, FL, COMPOSITOR, GObject)

struct _FlCompositorClass {
  GObjectClass parent_class;

  gboolean (*present_layers)(FlCompositor* compositor,
                             const FlutterLayer** layers,
                             size_t layers_count);

  gboolean (*render)(FlCompositor* compositor, cairo_t* cr, GdkWindow* window);

  gboolean (*render_layer)(FlCompositor* compositor,
                           const FlCompositorLayer& layer,
                           cairo_t* cr,
                           GdkWindow* window);
};

/**
 * FlCompositor:
 *
 * #FlCompositor is an abstract class that implements Flutter compositing.
 */

/**
 * fl_compositor_present_layers:
 * @compositor: an #FlCompositor.
 * @layers: layers to be composited.
 * @layers_count: number of layers.
 *
 * Composite layers. Called from the Flutter rendering thread.
 *
 * Returns %TRUE if successful.
 */
gboolean fl_compositor_present_layers(FlCompositor* compositor,
                                      const FlutterLayer** layers,
                                      size_t layers_count);

/**
 * fl_compositor_render:
 * @compositor: an #FlCompositor.
 * @cr: a Cairo rendering context.
 * @window: window being rendered into.
 *
 * Renders the current frame. Called from the GTK thread.
 *
 * Returns %TRUE if successful.
 */
gboolean fl_compositor_render(FlCompositor* compositor,
                              cairo_t* cr,
                              GdkWindow* window);

G_END_DECLS

// Acquires a frame whose buffers are no longer being read by GTK.
std::shared_ptr<FlCompositorFrame> fl_compositor_acquire_frame(
    FlCompositor* compositor,
    const FlutterLayer** layers,
    size_t layers_count);

// Atomically publishes a complete frame to the GTK thread.
void fl_compositor_publish_frame(FlCompositor* compositor,
                                 std::shared_ptr<FlCompositorFrame> frame);

// Pins the most recently published frame for a complete GTK paint cycle.
std::shared_ptr<FlCompositorFrame> fl_compositor_get_frame(
    FlCompositor* compositor);

// Drops all buffers after scene widgets release their frame references.
void fl_compositor_clear_frames(FlCompositor* compositor);

// Draws exactly one backing store, preserving the native widgets between
// stores.
gboolean fl_compositor_render_layer(FlCompositor* compositor,
                                    const FlCompositorLayer& layer,
                                    cairo_t* cr,
                                    GdkWindow* window);

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_COMPOSITOR_H_

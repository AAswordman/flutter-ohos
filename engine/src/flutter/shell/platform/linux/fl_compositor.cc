// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_compositor.h"

#include <algorithm>
#include <cmath>
#include <utility>

struct FrameState {
  GMutex mutex;
  std::shared_ptr<FlCompositorFrame> current;
  std::vector<std::shared_ptr<FlCompositorFrame>> pool;

  // Initializes synchronization shared by the raster and GTK threads.
  FrameState() { g_mutex_init(&mutex); }

  // Releases the mutex after the renderer has released its frame buffers.
  ~FrameState() { g_mutex_clear(&mutex); }
};

struct FlCompositorPrivate {
  FrameState* frames;
};

G_DEFINE_QUARK(fl_compositor_error_quark, fl_compositor_error)
G_DEFINE_TYPE_WITH_PRIVATE(FlCompositor, fl_compositor, G_TYPE_OBJECT)

// Gets the private frame exchange without exposing it in the public API.
static FrameState* get_frames(FlCompositor* self) {
  auto* priv = static_cast<FlCompositorPrivate*>(
      fl_compositor_get_instance_private(self));
  return priv->frames;
}

// Releases CPU bookkeeping after the renderer has disposed its buffers.
static void fl_compositor_finalize(GObject* object) {
  delete get_frames(FL_COMPOSITOR(object));
  G_OBJECT_CLASS(fl_compositor_parent_class)->finalize(object);
}

// Installs the common frame exchange lifecycle.
static void fl_compositor_class_init(FlCompositorClass* klass) {
  G_OBJECT_CLASS(klass)->finalize = fl_compositor_finalize;
}

// Allocates C++ frame state outside the zero-initialized GObject instance.
static void fl_compositor_init(FlCompositor* self) {
  auto* priv = static_cast<FlCompositorPrivate*>(
      fl_compositor_get_instance_private(self));
  priv->frames = new FrameState();
}

// Copies the embedder layers into buffers owned by the renderer.
gboolean fl_compositor_present_layers(FlCompositor* self,
                                      const FlutterLayer** layers,
                                      size_t layers_count) {
  g_return_val_if_fail(FL_IS_COMPOSITOR(self), FALSE);
  return FL_COMPOSITOR_GET_CLASS(self)->present_layers(self, layers,
                                                       layers_count);
}

// Renders a frame without native children for standalone compositor callers.
gboolean fl_compositor_render(FlCompositor* self,
                              cairo_t* cr,
                              GdkWindow* window) {
  g_return_val_if_fail(FL_IS_COMPOSITOR(self), FALSE);
  return FL_COMPOSITOR_GET_CLASS(self)->render(self, cr, window);
}

// Recycles only unleased frames; the raster thread exclusively owns the pool.
std::shared_ptr<FlCompositorFrame> fl_compositor_acquire_frame(
    FlCompositor* self,
    const FlutterLayer** layers,
    size_t layers_count) {
  FrameState* state = get_frames(self);
  std::shared_ptr<FlCompositorFrame> frame;
  for (const auto& entry : state->pool) {
    if (entry.use_count() == 1) {
      frame = entry;
      break;
    }
  }
  if (!frame) {
    frame = std::make_shared<FlCompositorFrame>();
    state->pool.push_back(frame);
  }
  frame->layers.resize(layers_count);
  frame->width = 0;
  frame->height = 0;
  for (size_t i = 0; i < layers_count; ++i) {
    const FlutterLayer* source = layers[i];
    auto& target = frame->layers[i];
    target.type = source->type;
    target.offset = source->offset;
    target.size = source->size;
    target.platform_view_id =
        source->type == kFlutterLayerContentTypePlatformView
            ? source->platform_view->identifier
            : 0;
    frame->width =
        std::max(frame->width,
                 static_cast<size_t>(std::max(
                     0.0, std::ceil(source->offset.x + source->size.width))));
    frame->height =
        std::max(frame->height,
                 static_cast<size_t>(std::max(
                     0.0, std::ceil(source->offset.y + source->size.height))));
    if (source->type == kFlutterLayerContentTypeBackingStore &&
        !target.backing) {
      target.backing = std::make_shared<FlCompositorBacking>();
    }
  }
  return frame;
}

// Publishes metadata and copied pixels as a single indivisible frame.
void fl_compositor_publish_frame(FlCompositor* self,
                                 std::shared_ptr<FlCompositorFrame> frame) {
  FrameState* state = get_frames(self);
  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&state->mutex);
  state->current = std::move(frame);
}

// Returns a lease that prevents the raster thread from recycling this frame.
std::shared_ptr<FlCompositorFrame> fl_compositor_get_frame(FlCompositor* self) {
  FrameState* state = get_frames(self);
  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&state->mutex);
  return state->current;
}

// Releases pool resources in the context selected by the concrete renderer.
void fl_compositor_clear_frames(FlCompositor* self) {
  FrameState* state = get_frames(self);
  state->current.reset();
  state->pool.clear();
}

// Draws an individual copied backing store at its embedder offset.
gboolean fl_compositor_render_layer(FlCompositor* self,
                                    const FlCompositorLayer& layer,
                                    cairo_t* cr,
                                    GdkWindow* window) {
  g_return_val_if_fail(FL_IS_COMPOSITOR(self), FALSE);
  g_return_val_if_fail(layer.type == kFlutterLayerContentTypeBackingStore,
                       FALSE);
  return FL_COMPOSITOR_GET_CLASS(self)->render_layer(self, layer, cr, window);
}

// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_compositor_software.h"

struct _FlCompositorSoftware {
  FlCompositor parent_instance;

  // Task runner to wait for frames on.
  FlTaskRunner* task_runner;
};

G_DEFINE_TYPE(FlCompositorSoftware,
              fl_compositor_software,
              fl_compositor_get_type())

// Copies each store independently so transparent native views retain lower
// layers.
static gboolean fl_compositor_software_present_layers(
    FlCompositor* compositor,
    const FlutterLayer** layers,
    size_t layers_count) {
  auto* self = FL_COMPOSITOR_SOFTWARE(compositor);
  auto frame = fl_compositor_acquire_frame(compositor, layers, layers_count);
  for (size_t i = 0; i < layers_count; ++i) {
    const auto* layer = layers[i];
    if (layer->type != kFlutterLayerContentTypeBackingStore) {
      continue;
    }
    const auto* store = layer->backing_store;
    g_return_val_if_fail(store->type == kFlutterBackingStoreTypeSoftware,
                         FALSE);
    auto& backing = *frame->layers[i].backing;
    const int width = layer->size.width;
    const int height = layer->size.height;
    if (backing.surface == nullptr ||
        cairo_image_surface_get_width(backing.surface) != width ||
        cairo_image_surface_get_height(backing.surface) != height) {
      g_clear_pointer(&backing.surface, cairo_surface_destroy);
      backing.surface =
          cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    }
    cairo_surface_t* source = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(
            const_cast<void*>(store->software.allocation)),
        CAIRO_FORMAT_ARGB32, width, height, store->software.row_bytes);
    cairo_t* cr = cairo_create(backing.surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(source);
  }
  fl_compositor_publish_frame(compositor, std::move(frame));
  fl_task_runner_stop_wait(self->task_runner);
  return TRUE;
}

// Paints a premultiplied software store without changing its shared surface
// scale.
static gboolean fl_compositor_software_render_layer(
    FlCompositor*,
    const FlCompositorLayer& layer,
    cairo_t* cr,
    GdkWindow* window) {
  const gint scale =
      window == nullptr ? 1 : gdk_window_get_scale_factor(window);
  cairo_save(cr);
  cairo_scale(cr, 1.0 / scale, 1.0 / scale);
  cairo_rectangle(cr, layer.offset.x, layer.offset.y, layer.size.width,
                  layer.size.height);
  cairo_clip(cr);
  cairo_set_source_surface(cr, layer.backing->surface, layer.offset.x,
                           layer.offset.y);
  cairo_paint(cr);
  cairo_restore(cr);
  return TRUE;
}

// Draws backing stores in order for compositor clients without native widgets.
static gboolean fl_compositor_software_render(FlCompositor* compositor,
                                              cairo_t* cr,
                                              GdkWindow* window) {
  auto frame = fl_compositor_get_frame(compositor);
  if (!frame) {
    return FALSE;
  }
  for (const auto& layer : frame->layers) {
    if (layer.type == kFlutterLayerContentTypeBackingStore) {
      fl_compositor_software_render_layer(compositor, layer, cr, window);
    }
  }
  return TRUE;
}

static void fl_compositor_software_dispose(GObject* object) {
  FlCompositorSoftware* self = FL_COMPOSITOR_SOFTWARE(object);

  g_clear_object(&self->task_runner);
  fl_compositor_clear_frames(FL_COMPOSITOR(self));

  G_OBJECT_CLASS(fl_compositor_software_parent_class)->dispose(object);
}

static void fl_compositor_software_class_init(
    FlCompositorSoftwareClass* klass) {
  FL_COMPOSITOR_CLASS(klass)->present_layers =
      fl_compositor_software_present_layers;
  FL_COMPOSITOR_CLASS(klass)->render = fl_compositor_software_render;
  FL_COMPOSITOR_CLASS(klass)->render_layer =
      fl_compositor_software_render_layer;

  G_OBJECT_CLASS(klass)->dispose = fl_compositor_software_dispose;
}

// Initializes the software renderer; frame storage belongs to FlCompositor.
static void fl_compositor_software_init(FlCompositorSoftware*) {}

FlCompositorSoftware* fl_compositor_software_new(FlTaskRunner* task_runner) {
  FlCompositorSoftware* self = FL_COMPOSITOR_SOFTWARE(
      g_object_new(fl_compositor_software_get_type(), nullptr));
  self->task_runner = FL_TASK_RUNNER(g_object_ref(task_runner));
  return self;
}

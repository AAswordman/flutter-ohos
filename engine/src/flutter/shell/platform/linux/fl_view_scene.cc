// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_view_scene.h"

#include <cmath>
#include <unordered_set>

struct _FlViewScene {
  GtkOverlay parent_instance;
  GHashTable* native_views;
  GHashTable* retired_views;
  GPtrArray* backing_widgets;
};

G_DEFINE_TYPE(FlViewScene, fl_view_scene, GTK_TYPE_OVERLAY)

struct LayerPaint {
  std::shared_ptr<FlCompositorFrame> frame;
  size_t index;
  FlCompositor* compositor;
  GdkGLContext* context;

  // Pins the compositor and context for the lifetime of a layer widget.
  LayerPaint(std::shared_ptr<FlCompositorFrame> frame,
             size_t index,
             FlCompositor* compositor,
             GdkGLContext* context)
      : frame(std::move(frame)),
        index(index),
        compositor(FL_COMPOSITOR(g_object_ref(compositor))),
        context(context == nullptr ? nullptr
                                   : GDK_GL_CONTEXT(g_object_ref(context))) {}

  // Returns the frame lease before releasing its renderer and GTK context.
  ~LayerPaint() {
    frame.reset();
    g_clear_object(&compositor);
    g_clear_object(&context);
  }
};

// Positions each native viewport from the same snapshot as the Flutter pixels.
static gboolean get_child_position_cb(GtkOverlay*,
                                      GtkWidget* widget,
                                      GtkAllocation* allocation,
                                      gpointer) {
  auto* bounds = static_cast<GtkAllocation*>(
      g_object_get_data(G_OBJECT(widget), "fl-native-layer-bounds"));
  if (bounds == nullptr) {
    return FALSE;
  }
  *allocation = *bounds;
  return TRUE;
}

// Releases per-widget paint state after GTK stops drawing the old frame.
static void destroy_layer_paint(gpointer data) {
  delete static_cast<LayerPaint*>(data);
}

// Paints a single backing store between the neighboring native children.
static gboolean draw_layer_cb(GtkWidget* widget, cairo_t* cr, gpointer) {
  auto* paint = static_cast<LayerPaint*>(
      g_object_get_data(G_OBJECT(widget), "fl-layer-paint"));
  if (paint->context != nullptr) {
    gdk_gl_context_make_current(paint->context);
  }
  const gboolean result = fl_compositor_render_layer(
      paint->compositor, paint->frame->layers[paint->index], cr,
      gtk_widget_get_window(widget));
  if (paint->context != nullptr) {
    gdk_gl_context_clear_current();
  }
  return result;
}

// Drops obsolete drawing widgets while retaining registered native children.
void fl_view_scene_clear(FlViewScene* self) {
  g_return_if_fail(FL_IS_VIEW_SCENE(self));
  for (guint i = 0; i < self->backing_widgets->len; ++i) {
    auto* widget = GTK_WIDGET(g_ptr_array_index(self->backing_widgets, i));
    g_object_set_data(G_OBJECT(widget), "fl-layer-paint", nullptr);
    gtk_widget_destroy(widget);
  }
  g_ptr_array_set_size(self->backing_widgets, 0);
}

// Applies metadata and pixel references from one immutable frame together.
void fl_view_scene_set_frame(FlViewScene* self,
                             FlCompositor* compositor,
                             std::shared_ptr<FlCompositorFrame> frame,
                             GdkGLContext* context) {
  g_return_if_fail(FL_IS_VIEW_SCENE(self));
  std::unordered_set<int64_t> visible;
  for (const auto& layer : frame->layers) {
    if (layer.type == kFlutterLayerContentTypePlatformView) {
      visible.insert(layer.platform_view_id);
    }
  }
  GHashTableIter iterator;
  gpointer key;
  gpointer value;
  g_hash_table_iter_init(&iterator, self->native_views);
  while (g_hash_table_iter_next(&iterator, &key, &value)) {
    const auto id = *static_cast<int64_t*>(key);
    gtk_widget_set_child_visible(GTK_WIDGET(value),
                                 visible.find(id) != visible.end());
  }
  bool allocation_changed = false;
  size_t backing_index = 0;
  int position = 0;
  for (size_t i = 0; i < frame->layers.size(); ++i) {
    const auto& layer = frame->layers[i];
    GtkWidget* widget;
    if (layer.type == kFlutterLayerContentTypePlatformView) {
      widget = GTK_WIDGET(
          g_hash_table_lookup(self->native_views, &layer.platform_view_id));
      // A queued raster frame can still mention a view disposed on GTK.
      if (g_hash_table_contains(self->retired_views, &layer.platform_view_id)) {
        continue;
      }
      if (widget == nullptr) {
        g_critical(
            "Flutter frame references unregistered native view "
            "%" G_GINT64_FORMAT,
            layer.platform_view_id);
        continue;
      }
      const double scale = gtk_widget_get_scale_factor(GTK_WIDGET(self));
      auto* previous = static_cast<GtkAllocation*>(
          g_object_get_data(G_OBJECT(widget), "fl-native-layer-bounds"));
      auto* bounds = g_new(GtkAllocation, 1);
      bounds->x = std::floor(layer.offset.x / scale);
      bounds->y = std::floor(layer.offset.y / scale);
      bounds->width =
          std::ceil((layer.offset.x + layer.size.width) / scale) - bounds->x;
      bounds->height =
          std::ceil((layer.offset.y + layer.size.height) / scale) - bounds->y;
      allocation_changed |= previous == nullptr || previous->x != bounds->x ||
                            previous->y != bounds->y ||
                            previous->width != bounds->width ||
                            previous->height != bounds->height;
      g_object_set_data_full(G_OBJECT(widget), "fl-native-layer-bounds", bounds,
                             g_free);
    } else {
      if (backing_index == self->backing_widgets->len) {
        widget = gtk_drawing_area_new();
        gtk_widget_set_has_window(widget, TRUE);
        gtk_widget_set_app_paintable(widget, TRUE);
        gtk_widget_set_hexpand(widget, TRUE);
        gtk_widget_set_vexpand(widget, TRUE);
        gtk_overlay_add_overlay(GTK_OVERLAY(self), widget);
        gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(self), widget, TRUE);
        g_signal_connect(widget, "draw", G_CALLBACK(draw_layer_cb), nullptr);
        g_ptr_array_add(self->backing_widgets, widget);
        gtk_widget_show(widget);
      } else {
        widget =
            GTK_WIDGET(g_ptr_array_index(self->backing_widgets, backing_index));
      }
      g_object_set_data_full(G_OBJECT(widget), "fl-layer-paint",
                             new LayerPaint(frame, i, compositor, context),
                             destroy_layer_paint);
      ++backing_index;
    }
    gtk_overlay_reorder_overlay(GTK_OVERLAY(self), widget, position++);
  }
  while (self->backing_widgets->len > backing_index) {
    auto* widget = GTK_WIDGET(g_ptr_array_index(
        self->backing_widgets, self->backing_widgets->len - 1));
    g_object_set_data(G_OBJECT(widget), "fl-layer-paint", nullptr);
    gtk_widget_destroy(widget);
    g_ptr_array_remove_index(self->backing_widgets,
                             self->backing_widgets->len - 1);
  }
  // Once a retired ID disappears from a frame, no older frame can return.
  g_hash_table_iter_init(&iterator, self->retired_views);
  while (g_hash_table_iter_next(&iterator, &key, nullptr)) {
    if (visible.find(*static_cast<int64_t*>(key)) == visible.end()) {
      g_hash_table_iter_remove(&iterator);
    }
  }
  if (allocation_changed) {
    gtk_widget_queue_allocate(GTK_WIDGET(self));
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

// Parents a native widget without allowing it to bypass Flutter hit testing.
void fl_view_scene_register_native_view(FlViewScene* self,
                                        int64_t id,
                                        GtkWidget* widget) {
  g_return_if_fail(FL_IS_VIEW_SCENE(self));
  g_return_if_fail(GTK_IS_WIDGET(widget));
  g_return_if_fail(!g_hash_table_contains(self->native_views, &id));
  g_return_if_fail(!g_hash_table_contains(self->retired_views, &id));
  int64_t* key = g_new(int64_t, 1);
  *key = id;
  gtk_overlay_add_overlay(GTK_OVERLAY(self), widget);
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(self), widget, TRUE);
  g_hash_table_insert(self->native_views, key, g_object_ref(widget));
  gtk_widget_set_child_visible(widget, FALSE);
}

// Unparents a native widget before releasing the reference held by the
// registry.
void fl_view_scene_unregister_native_view(FlViewScene* self, int64_t id) {
  g_return_if_fail(FL_IS_VIEW_SCENE(self));
  auto* widget = GTK_WIDGET(g_hash_table_lookup(self->native_views, &id));
  g_return_if_fail(widget != nullptr);
  gtk_container_remove(GTK_CONTAINER(self), widget);
  g_hash_table_remove(self->native_views, &id);
  auto* retired_id = g_new(int64_t, 1);
  *retired_id = id;
  g_hash_table_add(self->retired_views, retired_id);
}

// Keeps native keyboard and IME events out of Flutter redispatch.
gboolean fl_view_scene_has_native_focus(FlViewScene* self) {
  g_return_val_if_fail(FL_IS_VIEW_SCENE(self), FALSE);
  auto* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self));
  if (!GTK_IS_WINDOW(toplevel)) {
    return FALSE;
  }
  auto* focus = gtk_window_get_focus(GTK_WINDOW(toplevel));
  if (focus == nullptr) {
    return FALSE;
  }
  GHashTableIter iterator;
  gpointer value;
  g_hash_table_iter_init(&iterator, self->native_views);
  while (g_hash_table_iter_next(&iterator, nullptr, &value)) {
    auto* native = GTK_WIDGET(value);
    if (focus == native || gtk_widget_is_ancestor(focus, native)) {
      return TRUE;
    }
  }
  return FALSE;
}

// Clears frame leases before GTK recursively disposes child widgets.
static void fl_view_scene_dispose(GObject* object) {
  fl_view_scene_clear(FL_VIEW_SCENE(object));
  G_OBJECT_CLASS(fl_view_scene_parent_class)->dispose(object);
}

// Releases registry references after GTK destroys the widget hierarchy.
static void fl_view_scene_finalize(GObject* object) {
  auto* self = FL_VIEW_SCENE(object);
  g_hash_table_unref(self->native_views);
  g_hash_table_unref(self->retired_views);
  g_ptr_array_unref(self->backing_widgets);
  G_OBJECT_CLASS(fl_view_scene_parent_class)->finalize(object);
}

// Installs the scene lifecycle hooks.
static void fl_view_scene_class_init(FlViewSceneClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_view_scene_dispose;
  G_OBJECT_CLASS(klass)->finalize = fl_view_scene_finalize;
}

// Allocates registry and layer widget storage on the GTK thread.
static void fl_view_scene_init(FlViewScene* self) {
  self->native_views = g_hash_table_new_full(g_int64_hash, g_int64_equal,
                                             g_free, g_object_unref);
  self->retired_views =
      g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, nullptr);
  self->backing_widgets = g_ptr_array_new();
  g_signal_connect(self, "get-child-position",
                   G_CALLBACK(get_child_position_cb), nullptr);
}

// Constructs an empty scene for a Flutter view.
FlViewScene* fl_view_scene_new() {
  return FL_VIEW_SCENE(g_object_new(fl_view_scene_get_type(), nullptr));
}

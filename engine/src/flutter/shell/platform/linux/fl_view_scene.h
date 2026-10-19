// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_VIEW_SCENE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_VIEW_SCENE_H_

#include <gtk/gtk.h>
#include "flutter/shell/platform/linux/fl_compositor.h"

G_BEGIN_DECLS
G_DECLARE_FINAL_TYPE(FlViewScene, fl_view_scene, FL, VIEW_SCENE, GtkOverlay)

// Creates the GTK container that interleaves Flutter stores and native widgets.
FlViewScene* fl_view_scene_new();

// Installs one pinned frame in exact embedder paint order on the GTK thread.
void fl_view_scene_set_frame(FlViewScene* scene,
                             FlCompositor* compositor,
                             std::shared_ptr<FlCompositorFrame> frame,
                             GdkGLContext* context);

// Registers an initially hidden native widget identified by platform layers.
void fl_view_scene_register_native_view(FlViewScene* scene,
                                        int64_t id,
                                        GtkWidget* widget);

// Removes the native widget and releases its registry reference.
void fl_view_scene_unregister_native_view(FlViewScene* scene, int64_t id);

// Reports whether GTK keyboard focus belongs to a registered native child.
gboolean fl_view_scene_has_native_focus(FlViewScene* scene);

// Releases all frame leases before the engine and compositor are shut down.
void fl_view_scene_clear(FlViewScene* scene);

G_END_DECLS
#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_VIEW_SCENE_H_

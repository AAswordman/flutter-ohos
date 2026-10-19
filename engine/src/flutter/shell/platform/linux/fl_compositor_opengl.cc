// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_compositor_opengl.h"

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include "flutter/shell/platform/linux/fl_framebuffer.h"

struct _FlCompositorOpenGL {
  FlCompositor parent_instance;

  // Task runner to wait for frames on.
  FlTaskRunner* task_runner;

  // TRUE if can share framebuffers between contexts.
  gboolean shareable;

  // Flutter OpenGL contexts.
  FlOpenGLManager* opengl_manager;
};

G_DEFINE_TYPE(FlCompositorOpenGL,
              fl_compositor_opengl,
              fl_compositor_get_type())

// Copies complete backing stores into frame-owned textures on the raster
// thread.
static gboolean fl_compositor_opengl_present_layers(FlCompositor* compositor,
                                                    const FlutterLayer** layers,
                                                    size_t layers_count) {
  auto* self = FL_COMPOSITOR_OPENGL(compositor);
  auto frame = fl_compositor_acquire_frame(compositor, layers, layers_count);
  GLint saved_texture, saved_read, saved_draw;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved_read);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved_draw);
  const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_SCISSOR_TEST);
  for (size_t i = 0; i < layers_count; ++i) {
    const auto* layer = layers[i];
    if (layer->type != kFlutterLayerContentTypeBackingStore) {
      continue;
    }
    const auto* store = layer->backing_store;
    g_assert(store->type == kFlutterBackingStoreTypeOpenGL);
    auto* source = FL_FRAMEBUFFER(store->open_gl.framebuffer.user_data);
    auto& backing = *frame->layers[i].backing;
    const size_t width = fl_framebuffer_get_width(source);
    const size_t height = fl_framebuffer_get_height(source);
    if (backing.framebuffer == nullptr ||
        fl_framebuffer_get_width(backing.framebuffer) != width ||
        fl_framebuffer_get_height(backing.framebuffer) != height) {
      g_clear_object(&backing.framebuffer);
      backing.framebuffer =
          fl_framebuffer_new(GL_RGBA, width, height, self->shareable);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fl_framebuffer_get_id(source));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      fl_framebuffer_get_id(backing.framebuffer));
    // Frame offsets position layers in GTK, not coordinates inside the source
    // FBO.
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (!self->shareable) {
      backing.pixels.resize(width * height * 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER,
                        fl_framebuffer_get_id(backing.framebuffer));
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                   backing.pixels.data());
    }
  }
  if (scissor) {
    glEnable(GL_SCISSOR_TEST);
  }
  glBindTexture(GL_TEXTURE_2D, saved_texture);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_read);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved_draw);
  // Complete writes before the GTK context imports any of this frame's images.
  glFinish();
  fl_compositor_publish_frame(compositor, std::move(frame));
  fl_task_runner_stop_wait(self->task_runner);
  return TRUE;
}

// Paints one immutable backing store into its place in the GTK scene.
static gboolean fl_compositor_opengl_render_layer(
    FlCompositor*,
    const FlCompositorLayer& layer,
    cairo_t* cr,
    GdkWindow* window) {
  const auto& backing = *layer.backing;
  const gint scale =
      window == nullptr ? 1 : gdk_window_get_scale_factor(window);
  const size_t width = fl_framebuffer_get_width(backing.framebuffer);
  const size_t height = fl_framebuffer_get_height(backing.framebuffer);
  cairo_save(cr);
  cairo_rectangle(cr, layer.offset.x / scale, layer.offset.y / scale,
                  layer.size.width / scale, layer.size.height / scale);
  cairo_clip(cr);
  cairo_translate(cr, layer.offset.x / scale, layer.offset.y / scale);
  if (fl_framebuffer_get_shareable(backing.framebuffer)) {
    g_autoptr(FlFramebuffer) sibling =
        fl_framebuffer_create_sibling(backing.framebuffer);
    gdk_cairo_draw_from_gl(cr, window, fl_framebuffer_get_texture_id(sibling),
                           GL_TEXTURE, scale, 0, 0, width, height);
  } else {
    GLint saved_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, backing.pixels.data());
    gdk_cairo_draw_from_gl(cr, window, texture, GL_TEXTURE, scale, 0, 0, width,
                           height);
    glDeleteTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, saved_texture);
  }
  cairo_restore(cr);
  // Shared images must finish sampling before GTK releases this frame lease.
  if (fl_framebuffer_get_shareable(backing.framebuffer)) {
    glFinish();
  }
  glFlush();
  return TRUE;
}

// Draws backing stores in order for compositor clients without native widgets.
static gboolean fl_compositor_opengl_render(FlCompositor* compositor,
                                            cairo_t* cr,
                                            GdkWindow* window) {
  auto frame = fl_compositor_get_frame(compositor);
  if (!frame) {
    return FALSE;
  }
  for (const auto& layer : frame->layers) {
    if (layer.type == kFlutterLayerContentTypeBackingStore) {
      fl_compositor_opengl_render_layer(compositor, layer, cr, window);
    }
  }
  return TRUE;
}

// Releases pooled GPU stores after the raster thread has stopped.
static void fl_compositor_opengl_dispose(GObject* object) {
  FlCompositorOpenGL* self = FL_COMPOSITOR_OPENGL(object);

  if (self->opengl_manager == nullptr) {
    G_OBJECT_CLASS(fl_compositor_opengl_parent_class)->dispose(object);
    return;
  }
  // Engine shutdown joins the raster thread before this context is acquired.
  fl_opengl_manager_make_current(self->opengl_manager);
  fl_compositor_clear_frames(FL_COMPOSITOR(self));

  g_clear_object(&self->task_runner);
  fl_opengl_manager_clear_current(self->opengl_manager);
  g_clear_object(&self->opengl_manager);

  G_OBJECT_CLASS(fl_compositor_opengl_parent_class)->dispose(object);
}

static void fl_compositor_opengl_class_init(FlCompositorOpenGLClass* klass) {
  FL_COMPOSITOR_CLASS(klass)->present_layers =
      fl_compositor_opengl_present_layers;
  FL_COMPOSITOR_CLASS(klass)->render = fl_compositor_opengl_render;
  FL_COMPOSITOR_CLASS(klass)->render_layer = fl_compositor_opengl_render_layer;

  G_OBJECT_CLASS(klass)->dispose = fl_compositor_opengl_dispose;
}

// Initializes the OpenGL renderer; frame storage belongs to FlCompositor.
static void fl_compositor_opengl_init(FlCompositorOpenGL*) {}

FlCompositorOpenGL* fl_compositor_opengl_new(FlTaskRunner* task_runner,
                                             FlOpenGLManager* opengl_manager,
                                             gboolean shareable) {
  FlCompositorOpenGL* self = FL_COMPOSITOR_OPENGL(
      g_object_new(fl_compositor_opengl_get_type(), nullptr));

  self->task_runner = FL_TASK_RUNNER(g_object_ref(task_runner));
  self->shareable = shareable;
  self->opengl_manager = FL_OPENGL_MANAGER(g_object_ref(opengl_manager));

  return self;
}

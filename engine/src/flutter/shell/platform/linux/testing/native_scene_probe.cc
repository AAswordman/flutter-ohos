// Exercises the real GTK scene with two transparent WebKit widgets.
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include "flutter/shell/platform/linux/fl_compositor_opengl.h"
#include "flutter/shell/platform/linux/fl_compositor_software.h"
#include "flutter/shell/platform/linux/fl_opengl_manager.h"
#include "flutter/shell/platform/linux/fl_view_scene.h"

// Supplies a scheduler test double; this probe submits frames synchronously.
struct _FlTaskRunner {
  GObject parent_instance;
};
G_DEFINE_TYPE(FlTaskRunner, fl_task_runner, G_TYPE_OBJECT)
// Registers the scheduler test double.
static void fl_task_runner_class_init(FlTaskRunnerClass*) {}
// Initializes the scheduler test double.
static void fl_task_runner_init(FlTaskRunner*) {}
// Synchronous frame submission has no waiting GTK task to wake.
void fl_task_runner_stop_wait(FlTaskRunner*) {}

static GtkWidget* window;
static GtkWidget* input_box;
static const char* output_dir;
static FlViewScene* scene;
static FlCompositor* compositor;
static FlOpenGLManager* manager = nullptr;
static GdkGLContext* context = nullptr;
static bool gpu = false;
static int loaded = 0;
static int stage = 0;
static int result = 0;
static std::shared_ptr<FlCompositorFrame> retained;
static std::vector<uint32_t> base(240 * 160, 0xffff0000);
static std::vector<uint32_t> middle(240 * 160, 0);
static std::vector<uint32_t> top(240 * 160, 0x80000000);

// Counts completed HTML loads before checking the actual window pixels.
static void load_cb(WebKitWebView*, WebKitLoadEvent event, gpointer) {
  if (event == WEBKIT_LOAD_FINISHED)
    ++loaded;
}

// Creates a transparent native view with a translucent document background.
static GtkWidget* native_view(const char* color) {
  auto* widget = webkit_web_view_new();
  GdkRGBA transparent = {0, 0, 0, 0};
  webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(widget), &transparent);
  g_signal_connect(widget, "load-changed", G_CALLBACK(load_cb), nullptr);
  gchar* html = g_strdup_printf(
      "<html style='background:transparent'><body "
      "style='margin:0;background:%s;width:100vw;height:100vh'></body></html>",
      color);
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(widget), html,
                            "https://probe.invalid/");
  g_free(html);
  gtk_widget_show(widget);
  return widget;
}

// Submits a genuine embedder layer list through the modified software
// compositor.
static void submit(bool dialog, bool second = true) {
  auto render = [=]() {
    if (gpu)
      g_assert(fl_opengl_manager_make_current(manager));
    FlFramebuffer* buffers[3] = {};
    FlutterBackingStore stores[3] = {};
    const void* allocations[] = {base.data(), middle.data(), top.data()};
    for (int i = 0; i < 3; ++i) {
      stores[i].type = kFlutterBackingStoreTypeSoftware;
      stores[i].software.allocation = allocations[i];
      stores[i].software.row_bytes = 240 * 4;
      stores[i].software.height = 160;
      if (gpu) {
        buffers[i] = fl_framebuffer_new(GL_RGBA, 240, 160, FALSE);
        glBindTexture(GL_TEXTURE_2D, fl_framebuffer_get_texture_id(buffers[i]));
        std::vector<uint32_t> flipped(240 * 160);
        const auto* pixels = static_cast<const uint32_t*>(allocations[i]);
        for (int y = 0; y < 160; ++y)
          std::copy(pixels + y * 240, pixels + (y + 1) * 240,
                    flipped.begin() + (159 - y) * 240);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 160, GL_BGRA,
                        GL_UNSIGNED_BYTE, flipped.data());
        stores[i].type = kFlutterBackingStoreTypeOpenGL;
        stores[i].open_gl.framebuffer.user_data = buffers[i];
      }
    }
    FlutterPlatformView native[2] = {};
    native[0].identifier = 1;
    native[1].identifier = 2;
    FlutterLayer layers[5] = {};
    for (int i = 0; i < 5; ++i) {
      layers[i].size = {240, 160};
      layers[i].offset = {0, 0};
    }
    layers[0].type = kFlutterLayerContentTypeBackingStore;
    layers[0].backing_store = &stores[0];
    layers[1].type = kFlutterLayerContentTypePlatformView;
    layers[1].platform_view = &native[0];
    layers[1].offset = {20, 20};
    layers[1].size = {140, 120};
    layers[2].type = kFlutterLayerContentTypeBackingStore;
    layers[2].backing_store = &stores[1];
    layers[3].type = kFlutterLayerContentTypePlatformView;
    layers[3].platform_view = &native[1];
    layers[3].offset = {80, 20};
    layers[3].size = {140, 120};
    layers[4].type = kFlutterLayerContentTypeBackingStore;
    layers[4].backing_store = &stores[2];
    std::vector<const FlutterLayer*> frame = {&layers[0], &layers[1],
                                              &layers[2]};
    if (second)
      frame.push_back(&layers[3]);
    if (dialog)
      frame.push_back(&layers[4]);
    g_assert(
        fl_compositor_present_layers(compositor, frame.data(), frame.size()));
    if (gpu) {
      for (auto* buffer : buffers)
        g_object_unref(buffer);
      fl_opengl_manager_clear_current(manager);
    }
  };
  std::thread(render).join();
  fl_view_scene_set_frame(scene, compositor,
                          fl_compositor_get_frame(compositor), context);
  gtk_overlay_reorder_overlay(GTK_OVERLAY(scene), input_box, -1);
}

// Checks a screenshot pixel with rounding tolerance for native alpha blending.
static void pixel(GdkPixbuf* image, int x, int y, int r, int g, int b) {
  const auto* actual = gdk_pixbuf_get_pixels(image) +
                       y * gdk_pixbuf_get_rowstride(image) +
                       x * gdk_pixbuf_get_n_channels(image);
  std::printf("stage %d (%d,%d): %u,%u,%u expected %d,%d,%d\n", stage, x, y,
              actual[0], actual[1], actual[2], r, g, b);
  if (std::abs(actual[0] - r) > 5 || std::abs(actual[1] - g) > 5 ||
      std::abs(actual[2] - b) > 5)
    result = 1;
}

// Advances only after real WebKit loads and GTK paint cycles complete.
static gboolean advance(gpointer) {
  if (loaded != 2)
    return G_SOURCE_CONTINUE;
  GdkWindow* surface = gtk_widget_get_window(window);
  auto* image = gdk_pixbuf_get_from_window(surface, 0, 0, 240, 160);
  gchar* filename = g_strdup_printf("%s/%s-stage-%d.png", output_dir,
                                    gpu ? "opengl" : "software", stage);
  gdk_pixbuf_save(image, filename, "png", nullptr, nullptr);
  g_free(filename);
  if (stage == 0) {
    pixel(image, 10, 10, 255, 0, 0);
    pixel(image, 30, 50, 127, 128, 0);
    pixel(image, 70, 50, 191, 192, 128);
    pixel(image, 90, 50, 95, 96, 192);
    retained = fl_compositor_get_frame(compositor);
    submit(true);
  } else if (stage == 1) {
    pixel(image, 10, 10, 127, 0, 0);
    pixel(image, 30, 50, 63, 64, 0);
    pixel(image, 70, 50, 95, 96, 64);
    pixel(image, 90, 50, 47, 48, 96);
    pixel(image, 130, 85, 255, 255, 255);
    pixel(image, 200, 50, 63, 0, 64);
    g_assert(retained->layers.size() == 4);
    g_assert(retained != fl_compositor_get_frame(compositor));
    submit(false, false);
  } else {
    pixel(image, 90, 50, 191, 192, 128);
    pixel(image, 200, 50, 255, 0, 0);
    pixel(image, 130, 85, 191, 192, 128);
    fl_view_scene_unregister_native_view(scene, 2);
    g_object_unref(image);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
  }
  g_object_unref(image);
  ++stage;
  return G_SOURCE_CONTINUE;
}

// Fails instead of hanging when a native process cannot initialize.
static gboolean timeout_cb(gpointer) {
  std::fprintf(stderr, "Native scene probe timed out; loaded=%d stage=%d\n",
               loaded, stage);
  result = 1;
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}

// Runs the real GTK/WebKit scene and verifies paint order and transparency.
int main(int argc, char** argv) {
  gtk_init(&argc, &argv);
  if (argc != 3 || (std::strcmp(argv[1], "software") != 0 &&
                    std::strcmp(argv[1], "opengl") != 0)) {
    std::fprintf(
        stderr, "Usage: native_scene_probe software|opengl OUTPUT_DIRECTORY\n");
    return 2;
  }
  gpu = std::strcmp(argv[1], "opengl") == 0;
  output_dir = argv[2];
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(window), 240, 160);
  scene = fl_view_scene_new();
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(scene));
  auto* background = gtk_fixed_new();
  gtk_container_add(GTK_CONTAINER(scene), background);
  input_box = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(input_box), FALSE);
  gtk_event_box_set_above_child(GTK_EVENT_BOX(input_box), TRUE);
  GtkWidget* input_area = gtk_drawing_area_new();
  gtk_widget_set_app_paintable(input_area, TRUE);
  gtk_container_add(GTK_CONTAINER(input_box), input_area);
  gtk_overlay_add_overlay(GTK_OVERLAY(scene), input_box);
  fl_view_scene_register_native_view(scene, 1,
                                     native_view("rgba(0,255,0,0.5)"));
  fl_view_scene_register_native_view(scene, 2,
                                     native_view("rgba(0,0,255,0.5)"));
  auto* runner =
      FL_TASK_RUNNER(g_object_new(fl_task_runner_get_type(), nullptr));
  gtk_widget_show_all(window);
  if (gpu) {
    manager = fl_opengl_manager_new();
    g_assert(fl_opengl_manager_make_current(manager));
    compositor =
        FL_COMPOSITOR(fl_compositor_opengl_new(runner, manager, FALSE));
    fl_opengl_manager_clear_current(manager);
    GError* error = nullptr;
    context =
        gdk_window_create_gl_context(gtk_widget_get_window(window), &error);
    g_assert_no_error(error);
    g_assert(gdk_gl_context_realize(context, &error));
    g_assert_no_error(error);
  } else {
    compositor = FL_COMPOSITOR(fl_compositor_software_new(runner));
  }
  g_object_unref(runner);
  for (int y = 30; y < 120; ++y)
    for (int x = 60; x < 180; ++x)
      middle[y * 240 + x] = 0x80808080;
  for (int y = 70; y < 100; ++y)
    for (int x = 110; x < 150; ++x)
      top[y * 240 + x] = 0xffffffff;
  submit(false);
  gtk_widget_show_all(window);
  g_timeout_add(1000, advance, nullptr);
  g_timeout_add_seconds(30, timeout_cb, nullptr);
  gtk_main();
  retained.reset();
  fl_view_scene_clear(scene);
  gtk_widget_destroy(window);
  g_object_unref(compositor);
  g_clear_object(&context);
  g_clear_object(&manager);
  std::printf("Native scene probe: %s\n", result == 0 ? "PASS" : "FAIL");
  return result;
}

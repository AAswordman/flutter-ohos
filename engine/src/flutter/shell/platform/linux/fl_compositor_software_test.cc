// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <thread>
#include "gtest/gtest.h"

#include "flutter/common/constants.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/shell/platform/linux/fl_compositor_software.h"
#include "flutter/shell/platform/linux/fl_task_runner.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_dart_project.h"

#include <gdk/gdkwayland.h>

TEST(FlCompositorSoftwareTest, Render) {
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  g_autoptr(FlEngine) engine = fl_engine_new(project);
  g_autoptr(FlTaskRunner) task_runner = fl_task_runner_new(engine);

  g_autoptr(FlCompositorSoftware) compositor =
      fl_compositor_software_new(task_runner);

  // Present layer from a thread.
  constexpr size_t width = 100;
  constexpr size_t height = 100;
  size_t row_bytes = width * 4;
  g_autofree unsigned char* layer_data =
      static_cast<unsigned char*>(malloc(height * row_bytes));
  FlutterBackingStore backing_store = {
      .type = kFlutterBackingStoreTypeSoftware,
      .software = {
          .allocation = layer_data, .row_bytes = row_bytes, .height = height}};
  FlutterLayer layer = {.type = kFlutterLayerContentTypeBackingStore,
                        .backing_store = &backing_store,
                        .offset = {0, 0},
                        .size = {width, height}};
  const FlutterLayer* layers[1] = {&layer};
  std::thread([&]() {
    fl_compositor_present_layers(FL_COMPOSITOR(compositor), layers, 1);
  }).join();

  // Render presented layer.
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
  g_autofree unsigned char* image_data =
      static_cast<unsigned char*>(malloc(height * stride));
  cairo_surface_t* surface = cairo_image_surface_create_for_data(
      image_data, CAIRO_FORMAT_ARGB32, width, height, stride);
  cairo_t* cr = cairo_create(surface);
  fl_compositor_render(FL_COMPOSITOR(compositor), cr, nullptr);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
}

TEST(FlCompositorSoftwareTest, Resize) {
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  g_autoptr(FlEngine) engine = fl_engine_new(project);
  g_autoptr(FlTaskRunner) task_runner = fl_task_runner_new(engine);

  g_autoptr(FlCompositorSoftware) compositor =
      fl_compositor_software_new(task_runner);

  // Present a layer that is the old size.
  constexpr size_t width1 = 90;
  constexpr size_t height1 = 90;
  size_t row_bytes = width1 * 4;
  g_autofree unsigned char* layer1_data =
      static_cast<unsigned char*>(malloc(height1 * row_bytes));
  FlutterBackingStore backing_store1 = {
      .type = kFlutterBackingStoreTypeSoftware,
      .software = {.allocation = layer1_data,
                   .row_bytes = row_bytes,
                   .height = height1}};
  FlutterLayer layer1 = {.type = kFlutterLayerContentTypeBackingStore,
                         .backing_store = &backing_store1,
                         .offset = {0, 0},
                         .size = {width1, height1}};
  const FlutterLayer* layers1[1] = {&layer1};
  std::thread([&]() {
    fl_compositor_present_layers(FL_COMPOSITOR(compositor), layers1, 1);
  }).join();

  // Present layer in current size.
  constexpr size_t width2 = 100;
  constexpr size_t height2 = 100;
  row_bytes = width2 * 4;
  g_autofree unsigned char* layer2_data =
      static_cast<unsigned char*>(malloc(height2 * row_bytes));
  FlutterBackingStore backing_store2 = {
      .type = kFlutterBackingStoreTypeSoftware,
      .software = {.allocation = layer2_data,
                   .row_bytes = row_bytes,
                   .height = height2}};
  FlutterLayer layer2 = {.type = kFlutterLayerContentTypeBackingStore,
                         .backing_store = &backing_store2,
                         .offset = {0, 0},
                         .size = {width2, height2}};
  const FlutterLayer* layers2[1] = {&layer2};
  fml::AutoResetWaitableEvent latch;
  std::thread([&]() {
    fl_compositor_present_layers(FL_COMPOSITOR(compositor), layers2, 1);
    latch.Signal();
  }).detach();

  // Render, will wait for the second layer if necessary.
  int stride2 = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width2);
  g_autofree unsigned char* image_data =
      static_cast<unsigned char*>(malloc(height2 * stride2));
  cairo_surface_t* surface = cairo_image_surface_create_for_data(
      image_data, CAIRO_FORMAT_ARGB32, width2, height2, stride2);
  cairo_t* cr = cairo_create(surface);
  fl_compositor_render(FL_COMPOSITOR(compositor), cr, nullptr);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);

  latch.Wait();
}

// Verifies platform markers preserve independent stores, alpha, and source
// offsets.
TEST(FlCompositorSoftwareTest, NativeLayersRemainInterleaved) {
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  g_autoptr(FlEngine) engine = fl_engine_new(project);
  g_autoptr(FlTaskRunner) runner = fl_task_runner_new(engine);
  g_autoptr(FlCompositorSoftware) compositor =
      fl_compositor_software_new(runner);
  uint32_t background_pixels[4] = {0xffff0000, 0xffff0000, 0xffff0000,
                                   0xffff0000};
  uint32_t overlay_pixels[4] = {0x80000000, 0xffffffff, 0, 0};
  FlutterBackingStore background_store = {
      .type = kFlutterBackingStoreTypeSoftware,
      .software = {
          .allocation = background_pixels, .row_bytes = 8, .height = 2}};
  FlutterBackingStore overlay_store = {
      .type = kFlutterBackingStoreTypeSoftware,
      .software = {.allocation = overlay_pixels, .row_bytes = 8, .height = 2}};
  FlutterPlatformView native = {.identifier = 91};
  FlutterLayer background = {.type = kFlutterLayerContentTypeBackingStore,
                             .backing_store = &background_store,
                             .offset = {0, 0},
                             .size = {2, 2}};
  FlutterLayer platform = {.type = kFlutterLayerContentTypePlatformView,
                           .platform_view = &native,
                           .offset = {1, 0},
                           .size = {1, 2}};
  FlutterLayer overlay = {.type = kFlutterLayerContentTypeBackingStore,
                          .backing_store = &overlay_store,
                          .offset = {3, 4},
                          .size = {2, 2}};
  const FlutterLayer* layers[] = {&background, &platform, &overlay};
  ASSERT_TRUE(
      fl_compositor_present_layers(FL_COMPOSITOR(compositor), layers, 3));
  auto frame = fl_compositor_get_frame(FL_COMPOSITOR(compositor));
  ASSERT_EQ(frame->layers.size(), 3u);
  EXPECT_EQ(frame->layers[1].platform_view_id, 91);
  EXPECT_EQ(frame->layers[1].type, kFlutterLayerContentTypePlatformView);
  EXPECT_EQ(frame->layers[2].offset.x, 3);
  EXPECT_EQ(frame->layers[2].offset.y, 4);
  auto* lower = reinterpret_cast<uint32_t*>(
      cairo_image_surface_get_data(frame->layers[0].backing->surface));
  auto* upper = reinterpret_cast<uint32_t*>(
      cairo_image_surface_get_data(frame->layers[2].backing->surface));
  EXPECT_EQ(lower[1], 0xffff0000u);
  EXPECT_EQ(upper[0], 0x80000000u);
  EXPECT_EQ(upper[1], 0xffffffffu);
  EXPECT_EQ(upper[2], 0u);
}

// Checks producer reuse cannot mutate pixels or metadata retained by GTK.
TEST(FlCompositorSoftwareTest, FrameLeaseProtectsBackingStore) {
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  g_autoptr(FlEngine) engine = fl_engine_new(project);
  g_autoptr(FlTaskRunner) runner = fl_task_runner_new(engine);
  g_autoptr(FlCompositorSoftware) compositor =
      fl_compositor_software_new(runner);
  uint32_t pixel = 0xffff0000;
  FlutterBackingStore store = {
      .type = kFlutterBackingStoreTypeSoftware,
      .software = {.allocation = &pixel, .row_bytes = 4, .height = 1}};
  FlutterLayer layer = {.type = kFlutterLayerContentTypeBackingStore,
                        .backing_store = &store,
                        .offset = {0, 0},
                        .size = {1, 1}};
  const FlutterLayer* layers[] = {&layer};
  auto* base = FL_COMPOSITOR(compositor);
  ASSERT_TRUE(fl_compositor_present_layers(base, layers, 1));
  auto retained = fl_compositor_get_frame(base);
  const auto* first_address = retained.get();
  pixel = 0xff0000ff;
  layer.offset = {4, 5};
  ASSERT_TRUE(fl_compositor_present_layers(base, layers, 1));
  auto current = fl_compositor_get_frame(base);
  EXPECT_NE(retained, current);
  EXPECT_EQ(retained->layers[0].offset.x, 0);
  EXPECT_EQ(*reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(
                retained->layers[0].backing->surface)),
            0xffff0000u);
  EXPECT_EQ(*reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(
                current->layers[0].backing->surface)),
            0xff0000ffu);
  retained.reset();
  pixel = 0xff00ff00;
  ASSERT_TRUE(fl_compositor_present_layers(base, layers, 1));
  EXPECT_EQ(fl_compositor_get_frame(base).get(), first_address);
  ASSERT_TRUE(fl_compositor_present_layers(base, nullptr, 0));
  EXPECT_TRUE(fl_compositor_get_frame(base)->layers.empty());
}

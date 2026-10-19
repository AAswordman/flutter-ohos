#!/usr/bin/env bash
# Verifies real GTK/WebKit pixels with the engine's software and OpenGL compositors.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/engine/src"
linux="$src/flutter/shell/platform/linux"
output="$src/out/native_scene_probe"
mkdir -p "$output"
pkg-config --exists gtk+-3.0 epoxy webkit2gtk-4.1
clang++ -std=c++17 -pthread -DFLUTTER_LINUX_COMPILATION \
  -I"$src" -I"$linux/public" $(pkg-config --cflags gtk+-3.0 epoxy webkit2gtk-4.1) \
  "$linux/testing/native_scene_probe.cc" "$linux/fl_compositor.cc" \
  "$linux/fl_compositor_software.cc" "$linux/fl_compositor_opengl.cc" \
  "$linux/fl_framebuffer.cc" "$linux/fl_opengl_manager.cc" "$linux/fl_view_scene.cc" \
  $(pkg-config --libs gtk+-3.0 epoxy webkit2gtk-4.1) -o "$output/native_scene_probe"
for backend in software opengl; do
  GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 LIBGL_ALWAYS_INDIRECT=0 \
    xvfb-run -a "$output/native_scene_probe" "$backend" "$output"
done

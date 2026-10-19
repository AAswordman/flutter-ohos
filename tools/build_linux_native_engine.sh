#!/usr/bin/env bash
# Builds, tests, and packages the ordered Linux native-composition engine.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo 'Usage: build_linux_native_engine.sh debug|profile|release VERSION [JOBS]' >&2
  exit 2
fi
mode="$1"
version="$2"
jobs="${3:-4}"
case "$mode" in debug|profile|release) ;; *) echo "Invalid runtime mode: $mode" >&2; exit 2 ;; esac
case "$(uname -m)" in x86_64) arch=x64 ;; aarch64) arch=arm64 ;; *) echo 'Unsupported Linux engine architecture' >&2; exit 2 ;; esac
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
gn_args=(--runtime-mode "$mode" --no-goma)
if [[ "$arch" == arm64 ]]; then
  gn_args+=(--target-os linux --linux-cpu arm64 --target-dir "host_$mode")
fi
python3 engine/src/flutter/tools/gn "${gn_args[@]}"
out="$root/engine/src/out/host_$mode"
targets=(flutter/shell/platform/linux:flutter_gtk)
if [[ "$mode" == debug ]]; then targets+=(flutter_linux_unittests); fi
/usr/bin/ninja -C "$out" -j "$jobs" "${targets[@]}"
if [[ "$mode" == debug ]]; then
  GDK_BACKEND=x11 xvfb-run -a dbus-run-session -- "$out/flutter_linux_unittests" \
    --gtest_output="xml:$out/linux-native-unit-tests.xml"
  bash "$root/tools/test_linux_native_scene.sh"
fi
archive="$out/zip_archives/linux-$arch-$mode/linux-$arch-flutter-gtk.zip"
python3 "$root/tools/package_linux_native_engine.py" "$root" "$archive" "$mode" "$version" "$arch"

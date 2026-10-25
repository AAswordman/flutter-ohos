#!/usr/bin/env bash
# Build an iOS runtime and the shared Debug simulator slices from this checkout.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo 'Usage: build_ios_engine.sh debug|profile|release [JOBS]' >&2
  exit 2
fi
mode="$1"
jobs="${2:-8}"
case "$mode" in debug|profile|release) ;; *) echo "Invalid runtime mode: $mode" >&2; exit 2 ;; esac
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
engine_abi="$(cat bin/internal/engine.version)"
ninja_bin="${NINJA:-ninja}"
# Keep the existing Dart compiler/engine ABI; this change only replaces rendering.
common=(--ios --no-goma --no-lto --gn-args "engine_version=\"$engine_abi\"")
python3 engine/src/flutter/tools/gn "${common[@]}" --runtime-mode "$mode"
"$ninja_bin" -C "engine/src/out/ios_$mode" Flutter.xcframework -j "$jobs"
for arch in arm64 x64; do
  suffix=""
  if [[ "$arch" == arm64 ]]; then suffix="_arm64"; fi
  python3 engine/src/flutter/tools/gn "${common[@]}" --runtime-mode debug --simulator --simulator-cpu "$arch"
  "$ninja_bin" -C "engine/src/out/ios_debug_sim$suffix" Flutter.xcframework -j "$jobs"
done

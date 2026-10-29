# Flutter Project Rules

CRITICAL: You MUST follow these steps after modifying any Dart file. NEVER declare a task done without completing both steps.

After modifying any Dart file, you MUST:
1. Run `dart analyze --fatal-infos <files>` on every modified file. Fix ALL errors and warnings before proceeding.
2. Run `dart format <files>` on every modified file.

No exceptions. These steps are mandatory even for trivial changes.

CRITICAL: When adding new rules to `.agents/rules/` directory, you MUST also sync the rules to AGENTS.md.

Create a corresponding section in AGENTS.md for each rule file in `.agents/rules/` to maintain consistency between the rules directory and AGENTS.md.

## OHos ArkTS Native Public API Deletion

CRITICAL: When removing an `export` symbol that already exists in the repository from the ohos ArkTS native side, you MUST ask the user for confirmation before proceeding. Do NOT delete it without explicit user confirmation.

This rule does NOT apply to modifying an existing symbol's implementation (signature unchanged) or restructuring files not yet committed to the repository.

## Engine Changes → Operit2 Builds (local-engine workflow)

This checkout is the Flutter fork used by the Operit2 app at
`/home/harmoon/projects/Operit2` (built through the FVM SDK pinned in
`apps/flutter/app/.fvmrc`). Engine patches here are consumed in one of two ways:
`--local-engine` for development/testing, or by publishing new engine artifacts
for release builds. Background and the partial-repaint work plan:
`agent-artifacts/partial-repaint-plan.md`.

### 1. Build the engine variants Operit2 needs

Engine sources are `engine/src/flutter/**`; the GN entry point is
`engine/src/flutter/tools/gn`, and build dirs are
`engine/src/out/<os>_<mode>[_<cpu>]`.

```bash
# Android target (release example; also android_profile_arm64 / android_debug_arm64)
cd engine/src
./flutter/tools/gn --android --android-cpu arm64 --runtime-mode release
ninja -C out/android_release_arm64

# Host tooling (impellerc / font-subset / const_finder are required by the app build)
./flutter/tools/gn --unoptimized --disable-desktop-embeddings --gn-args shell_enable_gl=false
ninja -C out/host_debug_unopt impellerc font-subset const_finder
```

Expected in `out/android_release_arm64`: `flutter.jar`, `libflutter.so`,
`icudtl.dat`, `clang_x64/gen_snapshot`, `clang_x64/impellerc`,
`flutter_patched_sdk/`.

Gotchas (all hit in practice):
- `--local-engine` REQUIRES `--local-engine-host`; the tool exits otherwise. The
  host dir only needs `impellerc`, `font-subset`, `gen/const_finder`; the Dart
  SDK falls back to `engine/src/flutter/prebuilts/linux-x64/dart-sdk`.
- `flutter build apk --release` reads `flutter_patched_sdk_product` from the
  local target out dir, but GN does not emit it. Copy the same-version copy
  from the SDK cache:
  `cp -r <flutter-sdk>/bin/cache/artifacts/engine/common/flutter_patched_sdk_product engine/src/out/android_release_arm64/`
- Host GN fails unless `engine/src/flutter/third_party/swiftshader` exists (a
  test-only dep that is not synced here). When only building the host tools
  above, a stub is enough: an empty `swiftshader` source_set plus a
  `swiftshader_config` config in `third_party/swiftshader/BUILD.gn`, and an
  empty `swiftshader_libvulkan_static` static_library in
  `third_party/swiftshader/src/Vulkan/BUILD.gn`. Keep the stub untracked.
- Build dirs are large (`android_release_arm64` ≈ 1.4 GB). Never commit or copy
  them; `out/` is disposable.

### 2. Build Operit2 against the local engine

```bash
cd /home/harmoon/projects/Operit2
python3 tools/build_scripts/build_flutter_android.py \
  --build-name 2.0.0 --build-number <n> \
  --local-engine android_release_arm64 \
  --local-engine-src-path /home/harmoon/projects/flutter-ohos/engine/src \
  --local-engine-host host_release
```

The script forwards `--local-engine*` to `flutter build apk`; the Flutter tool
passes `-Plocal-engine-*` to Gradle, so no Gradle files need editing. The raw
equivalent is `fvm flutter build apk --release --split-per-abi
--target-platform android-arm64 --local-engine-src-path <engine src>
--local-engine android_release_arm64 --local-engine-host host_release`.

If `flutter pub get` fails with `Connection terminated during handshake`, the
machine's HTTP proxy is breaking pub's TLS. Run the build with the proxy
variables stripped (`env -u http_proxy -u https_proxy -u all_proxy …`); direct
access to pub.dev works while the proxy does not.

Confirm the APK actually contains the patched engine: compare
`unzip -p <apk> lib/arm64-v8a/libflutter.so` with
`out/android_release_arm64/libflutter.so` (Gradle strips symbols, so prefer a
size/hash comparison against `out/android_release_arm64/lib.stripped/` or grep a
string you added), or check the build log for the local engine path.

### 2b. Local-engine toolchain must be self-consistent (important)

`--local-engine` takes EVERYTHING from the given out dirs, and the VM refuses
mismatched snapshots. This checkout's `flutter/prebuilts` Dart is NOT the same
build as `third_party/dart` (VM hashes `78da37…` vs `a38ee8…`), so mixing
prebuilt tooling with a source-built engine crashes at startup:

- `Wrong full snapshot version` — VM vs AOT snapshot mismatch;
- `Flag dedup_instructions is false in snapshot, but … always true in product
  mode` — a develop-mode `dart` running the product `frontend_server_aot`
  snapshot;
- `Type IntPtr has no mapping for ABI … ohos` — a stock frontend without the
  fork's patched SDK.

Build both sides from source so every artifact carries the same VM hash:

```bash
cd engine/src
# Host: RELEASE mode (frontend_server_aot is a product snapshot), source Dart.
./flutter/tools/gn --runtime-mode release --no-prebuilt-dart-sdk \
  --disable-desktop-embeddings --gn-args shell_enable_gl=false
ninja -C out/host_release -j 10 zip_archives/dart-sdk-linux-x64.zip \
  gen/frontend_server_aot.dart.snapshot impellerc font-subset const_finder

# Android: source Dart as well, and build gen_snapshot from source.
./flutter/tools/gn --android --android-cpu arm64 --runtime-mode release --no-prebuilt-dart-sdk
ninja -C out/android_release_arm64 -j 10 default clang_x64/gen_snapshot

# The release build's patched SDK dills are already the product set.
cp -r out/android_release_arm64/flutter_patched_sdk \
      out/android_release_arm64/flutter_patched_sdk_product
```

Use `--local-engine-host host_release`. Consistency check — all three must print
the same 32-hex VM version:

```bash
strings out/host_release/dart-sdk/bin/dart | grep -E '^[0-9a-f]{32}$' | sort -u
strings out/android_release_arm64/clang_x64/gen_snapshot | grep -E '^[0-9a-f]{32}$'
strings out/android_release_arm64/libflutter.so | grep -E '^[0-9a-f]{32}$' | sort -u
```

Also copy `out/host_release/gen/frontend_server_aot.dart.snapshot` into
`out/host_release/dart-sdk/bin/snapshots/` if it was not already.

### 3. Fork-only runtime switches

- `--enable-partial-repaint` (engine switch) is enabled from the app through the
  manifest meta-data `io.flutter.embedding.android.EnablePartialRepaint=true`.
  Both the switch and the meta-data key exist only in this fork; stock engines
  ignore the meta-data. Remove the meta-data to A/B against the default behavior.
- `bin/internal/engine.version` in this checkout must match the one in the FVM
  SDK used by Operit2 (currently both `42d3d75a…`). After syncing the fork,
  rebuild the engine; otherwise `--local-engine` is stale.

### 4. Release path (no `--local-engine`)

For CI/release builds, either (a) build the engine on the build machine and pass
the same `--local-engine*` flags, (b) merge the patch into this fork and publish
new engine artifacts, bumping `bin/internal/engine.version` so Operit2 only
needs to move to the new SDK version, or (c) host the artifacts in a private
maven repo and point the Gradle engine repo at it. Prefer (b) for anything that
ships.

### 5. Always-vs-sometimes check

Before changing engine code, reproduce the baseline with an unpatched build and
record: per-thread CPU (`/proc/<pid>/stat` + `/proc/<pid>/task/*/stat` deltas),
produced frame rate (`dumpsys SurfaceFlinger --latency` on the
`SurfaceView[…](BLAST)` layer), and a simpleperf profile of the `1.raster` and UI
threads. `dumpsys gfxinfo` does NOT count Flutter frames.

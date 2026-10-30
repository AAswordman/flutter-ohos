# Operit.9 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.9` (2026-10-30).

## Android partial repaint

The Android engines can now repaint only the damaged portion of the
framebuffer instead of the whole surface.

- **Vulkan (Impeller)**: per-image damage accumulation, a render area limited
  to the damaged region, and an explicit `ImpellerBackend=vulkan` selection
  that bypasses the known-bad driver blacklist so affected devices can be
  tested.
- **OpenGLES (Skia and Impeller)**: partial repaint is gated on both
  `EGL_EXT_buffer_age` and swap-with-damage. Adreno drivers reject
  `eglSetDamageRegionKHR` with `EGL_BAD_ACCESS`, so `EGL_KHR_partial_update`
  is not used; without buffer age the surface falls back to full frames.
- The feature is off unless the app sets
  `io.flutter.embedding.android.EnablePartialRepaint=true`; the 0.7 damage
  ratio gate still decides when a full frame is cheaper.

## Damage granularity

`DisplayListLayer::Compare` no longer treats a display list larger than 10 KB
as changed. `kMaxBytesToCompare` is raised to 256 KB (`bytes()` is O(1),
`Equals()` is a memcmp), so a retained but re-recorded layer can be proven
unchanged instead of damaging its whole bounds. This removes the whole-sidebar
repaint caused by a 32x64 animation on devices whose display lists are large.

## Diagnostics

The `DiffStats` summary and the `PartialRepaint` damage coordinates are
compiled out of release builds (`!FLUTTER_RELEASE`); debug and profile builds
keep them.

## Distribution

This release changes framework source and the Android engine source. The
Android engine binaries are not part of the SDK archives:

- Source: `release/operit.9` (this tag).
- Android arm64 release engine: published as a static Maven repository at
  `https://h4rm00n.github.io/flutter-ohos-engine-maven`, coordinates
  `io.flutter:arm64_v8a_release:1.0.0-42d3d75a-operit.9.2` and
  `io.flutter:flutter_embedding_release:1.0.0-42d3d75a-operit.9.2`
  (release `engine-42d3d75a-operit.9.2`).

The engine is built against the Dart revision pinned by `DEPS`
(`c70f78e7d682c158c15ca0c26c729b3ccb932284`), so its VM hash
(`78da37fed6bf1489361a312568249f3f`) matches the SDK's stock
`gen_snapshot`; only `libflutter.so` and the embedding jar are replaced.
Windows and Linux desktop artifacts keep their existing pins. `engine.version`
remains `42d3d75a56efe1a2e9902f52dc8006099c45d937`.

## Verification

- `gn --check` passes for the Android arm64 release build.
- The partial-repaint build was exercised on Adreno 650 hardware (explicit
  Vulkan and GLES fallback) and on a tablet: animation damage is reported as
  `buffer=@160,416 32x64` (`@128,416 64x64` when the 32 px grid expands it)
  while the rest of the layer tree is retained; rendered output is correct.
- A release APK built from the normal SDK (without `--local-engine`) resolved
  the engine from the Maven repository above; the packaged `libflutter.so`
  contains the fork's `PartialRepaintRenderArea` marker and the stock VM hash.
  Local Android builds require JDK 17, matching CI.

## Consumer update

Select this SDK tag with FVM (`flutterUrl:
https://github.com/AAswordman/flutter-ohos.git`) and add the Maven repository
to the Android build as shown in the repository README. `--local-engine` is no
longer required to test the engine on device.

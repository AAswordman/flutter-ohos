# Operit.2 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.2` (2026-10-07).

## Source and artifacts

This release contains both release-note-bearing commits:

- `f320fdb079357216d74fe7e46605b6d61c8fbd37`: recover from genuine WebGL context loss in CanvasKit and Skwasm.
- `13a5543b0be4df18ab669da9d25d99ae723c012e`: isolate the complete desktop AOT compiler SDK from OHOS compiler artifacts.

`FlutterWebSdk` now downloads this release's `flutter-web-sdk.zip` directly.
The new release tag is its cache stamp, invalidating the old Web SDK. Windows
bootstrap resolves the actual Git revision; the old `$git` command prevented
snapshot invalidation after SDK updates. Windows GN uses `git.exe`, not the
removed depot_tools `git.bat`. The source tag bootstraps a fresh tool snapshot.

The Web SDK contains newly compiled `dart2js_platform.dill`,
`dart2wasm_platform.dill`, `ddc_outline.dill`, both DDC module formats,
rewritten engine libraries, and the rebuilt Flutter JavaScript loader.
Unchanged CanvasKit/Skwasm renderer binaries remain included; GPU rendering
stays enabled.

Each desktop compiler package contains the entire matching `dart-sdk/`,
`common/flutter_patched_sdk/` and `common/flutter_patched_sdk_product/` trees.
These match desktop engine `42d3d75a56efe1a2e9902f52dc8006099c45d937`, not OHOS
engine `ab1841593ed352873a3d26cb41e942e90b813be0`. Published matching compilers
are repackaged, not recompiled. Every package records source URLs and hashes.
Extract a compiler package directly into
`bin/cache/artifacts/engine/desktop-compiler/`; the normal precache command
also downloads the same coherent components and manages their cache stamp.

Compiler packages: Windows x64, Linux x64/ARM64, macOS x64/ARM64 (asset names
use `darwin`). The pinned upstream Windows ARM64 Dart SDK returns HTTP 404.
No Windows ARM64 compiler archive is published here, and no differently
architected SDK is substituted.

Existing Windows native-composition archives remain selected from `.operit.1`
and match this engine pin. Native engines, `gen_snapshot`, OHOS engines and
HARs are unchanged; these two fixes do not require native recompilation.

## Verification

- Modified Dart sources: `dart analyze --fatal-infos` and `dart format`.
- Web SDK cache/download regression tests: 3 passed.
- Real Chrome WebGL context-loss regression: CanvasKit Chromium 5 passed; full 5 passed.
- Desktop compiler artifact/routing tests: 24 passed.
- Windows x64 compiler probe: IntPtr and native resolver return `int64` in
  profile and release using the isolated complete compiler set.
- Archive provenance and SHA-256 manifests are included.

The `engine/src/flutter/lib/web_ui/dev/release_verify_felt.dart` launcher
sets the working directory required by the engine test runner. Consumers can
invoke it through FVM from their application directory.

## Consumer update and application gates

Pin FVM to this release with this fork's `flutterUrl`. From the application's
directory run `fvm install --skip-pub-get`, `fvm flutter precache --web` and
the explicit desktop precache target, then `fvm flutter clean` and
`fvm flutter pub get` before building.

Previously built desktop profile/release applications must be clean rebuilt
as complete bundles, including Linux `libapp.so`. Web applications must be
rebuilt and redeployed with the new platform kernels. Updating the installed
SDK does not repair old application binaries.

This SDK release does not publish Operit2 application bundles. Before publishing
new Linux/SteamOS application binaries, the hello-world and Operit2
profile/release GUI smoke tests in
[desktop AOT ABI isolation](desktop-aot-abi-isolation.md) remain mandatory.
Windows compiler probes are not Linux/SteamOS runtime verification.

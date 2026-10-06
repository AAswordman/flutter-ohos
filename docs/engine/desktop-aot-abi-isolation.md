# Desktop AOT ABI isolation — fix and release notes

## Problem and scope

The OHOS SDK inserts `ohosArm`, `ohosArm64` and `ohosX64` into the middle of
Dart's ABI table. The desktop artifacts selected by this fork still come from
`bin/internal/engine.version`, whereas the bootstrap Dart SDK, frontend and
shared patched SDK use `engine.ohos.version`. The version string alone does not
make these artifact sets compatible.

For the inspected SDK combination, the official Linux x64 AOT compiler selects
ABI index 14. In the OHOS table that index is Linux Arm (32-bit), not Linux x64.
The macOS x64 compiler has a corresponding mismatch at index 18. In particular,
`dart:ffi`'s native resolver returns an `IntPtr` function address that becomes an
`int32`, and sign extension produces garbage addresses such as
`0xfffffffff7b120b0`. A Dart-heap address in a native-transition backtrace is not
by itself proof of a WebView use-after-free.

The reproducer was a clean hello-world failing in profile/release but not debug.
The downloaded application's resolver disassembly and isolated compiler probes
also showed the truncation. The original artifact stamps were:

- Desktop engine: `42d3d75a56efe1a2e9902f52dc8006099c45d937`.
- OHOS SDK/engine: `ab1841593ed352873a3d26cb41e942e90b813be0`.

## Source fix

`DesktopCompilerArtifacts` caches a **complete** non-OHOS compiler set under
`bin/cache/artifacts/engine/desktop-compiler/`:

```text
dart-sdk/                 # frontend snapshots + matching Dart runtimes
common/flutter_patched_sdk/
common/flutter_patched_sdk_product/
```

All three archives are fetched from the configured standard Flutter storage
using the desktop engine revision. The cache stamp includes the engine revision
and host OS/architecture. Updating evicts the entire old set; incomplete downloads
cannot get a successful stamp.

For desktop **profile and release**, `CachedArtifacts` routes the SDK, frontend,
Dart/dartaotruntime and platform libraries/dill to that isolated set.
`KernelSnapshot` passes the desktop target to `KernelCompiler`, which propagates
the target and build mode when resolving the compiler executable/snapshot. Just
changing the platform dill is insufficient: mixing the old frontend with the
new platform also fails compilation.

Desktop debug/hot reload, OHOS artifacts, the bootstrap Dart SDK, and local-engine
artifact routing retain their existing behavior. This change does not renumber
any OHOS ABI or modify native engine C++ code. Its scope is desktop AOT; it is not
a general audit of Android/iOS/Web artifact compatibility.

## What needs recompilation / publishing

| Component | Action required for this fix |
| --- | --- |
| Maintained Flutter SDK/framework/tool source | Publish a new SDK patch release containing this change. |
| `flutter_tools.snapshot` | Regenerate with the new tool source, or omit it from the SDK archive so bootstrap regenerates it. An old snapshot does not contain the new cache/routing logic. |
| Desktop compiler SDK + patched SDKs | Precache/repackage all three matching archives into the isolated directory, or let the new tool download them. **No recompilation of these already published matching archives is required.** Do not copy just a single dill. |
| Previously built Linux desktop profile/release apps | **Clean rebuild and republish the entire application bundle**, including the new `libapp.so`. Updating the installed SDK or replacing `libflutter_linux_gtk.so` does not repair already miscompiled AOT code. |
| Previously built macOS/Windows desktop AOT apps | Rebuild/republish with the coherent compiler set too; shifted ABI tables can corrupt other ABI-specific native types even where `IntPtr` happens to remain 64-bit. |
| Existing matching desktop native engines / `gen_snapshot` | **No native rebuild required solely for this fix.** Keep them matched to `engine.version`. |
| OHOS native engine, HAR, Dart SDK, patched SDK / AOT generators | **No rebuild required solely for this fix.** OHOS selection and numbering are unchanged. |
| Android/iOS/Web releases | No source or artifact-selection change for those targets in this patch. Validate separately when producing a broader release. |

If publishing a separately changed native desktop engine/Dart revision, publish
its matching frontend/runtime/platform set as well and update the engine pin.
This patch does not make arbitrary custom native engines compatible with old
compiler artifacts. Likewise, any future OHOS ABI-renumbering fix would require
rebuilding and publishing the entire OHOS Dart/compiler/engine set together;
that is **not** the approach taken here.

## Rebuild instructions (Linux / Steam Deck / Linux CI)

Use the updated maintained SDK, not a different FVM installation:

```sh
cd /path/to/flutter-ohos
# After updating to the SDK release/commit containing this fix:
rm -f bin/cache/flutter_tools.snapshot bin/cache/flutter_tools.stamp
bin/flutter precache --linux
python3 tools/check_desktop_aot_abi.py --output /tmp/desktop-aot-abi-check

cd /path/to/application
/path/to/flutter-ohos/bin/flutter clean
/path/to/flutter-ohos/bin/flutter pub get
/path/to/flutter-ohos/bin/flutter build linux --profile
# Smoke-test the profile bundle, then:
/path/to/flutter-ohos/bin/flutter build linux --release
# Smoke-test and publish the rebuilt release bundle.
```

Do not delete the OHOS SDK/common cache or hand-edit installed `dart:ffi` source.
The new cache can coexist with the original one. The additional host Dart SDK
is a universal artifact, so a first update also downloads it for an OHOS-only
installation; it remains separate and does not replace its OHOS compiler.

## Validation and remaining release gate

- All modified Dart files: `dart analyze --fatal-infos` clean and `dart format` run.
- Targeted compiler/build-system regression suite: **58 tests passed**, including
  **24 new tests** for six host download variants, five desktop targets in both
  AOT modes, required-file/stamp checks, unchanged OHOS/debug paths and actual
  `KernelSnapshot` command propagation.
- A real macOS-arm64-host precache fetched the matched desktop SDK/platform
  archives and completed successfully.
- `tools/check_desktop_aot_abi.py` compiles a native IntPtr probe and the native
  resolver closure using the isolated frontend/platform and the existing macOS
  x64 `gen_snapshot`. Both functions return **int64 in profile and release**.
  The deliberately nonexistent symbol is never executed; this is a compiler
  test, not a GUI/runtime smoke test.
- A wider existing cache/artifact suite had five failures. The same five failures
  were reproduced on the unchanged `f320fdb0793` baseline (one Linux local-engine
  tester path, two cache tests' global-context access and two Web SDK URL tests).
  They are not silently counted as passing or changed in this patch.

**Before distributing the fixed Linux application, run a newly rebuilt
hello-world and Operit2 profile/release bundle on SteamOS/Linux.** The development
host is macOS; Linux GUI runtime verification remains a release gate.

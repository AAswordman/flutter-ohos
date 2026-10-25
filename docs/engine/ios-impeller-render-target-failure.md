# iOS Impeller render target failure

The Operit TestFlight build 26 crash reports from 2026-10-25 at 09:19:46 and
10:18:21 Asia/Shanghai terminate at `Canvas::GetLocalCoverageLimit +712` on
iPhone10,3 / iOS 16.7.16. Disassembly of the matching release binary
(UUID `4C4C44A2-5555-3144-A15F-431DD0AC6446`) identifies the fatal
`back_texture` assertion, rather than the neighboring render-pass-stack check.
The reports establish an invalid target reaching coverage calculation; they do
not establish why allocation failed or prove a system OOM event.

## Ownership and failure handling

- A private `LazyRenderingConfig` constructor and validating `Make` factory
  ensure only complete targets enter the canvas render-pass stack. Root,
  readback, SaveLayer and onscreen-switch targets use the same factory.
- Failed SaveLayer allocations preserve the parent pass and balance nested
  Save/Restore through the existing skip mechanism.
- Full-coverage clear optimization retains the validated target and resets only
  its command recording context. The direct rendering-config reconstruction path is removed.
- Coverage lookup returns no coverage for an unavailable texture.
- Frame failure propagates through `EndReplay`, `FinishRecording`,
  `RenderToTarget` and `DisplayListToTexture`. Failed frames do not present.
- Failed subpass command creation or encoding unwinds layer state, reports frame
  failure and disables destructor encoding retries. Backdrop pass-end failure
  also disables retries while retaining valid stack ownership.
- Final copy and mipmap creation/encoding are checked. A later frame can allocate
  fresh resources and recover.

This handles invalid render targets and rendering failures. It does not reduce
application memory use or address separate Rust JSON allocation aborts.

## Validation

Six regression cases pass using the Metal backend, with real GPU allocations
and injected failures. They cover failed root readback, nested SaveLayer,
invalid root targets, missing attachments, full-coverage pass restart, and
subpass/backdrop command creation/encoding failure without destructor retry.
A failed backdrop SaveLayer still creates the matching skipped Save/Restore
entry; failed layer composition also unwinds its clip/transform state.

The related Metal filter selects 69 cases: 68 pass in default mode; the one
window-dependent advanced-blend case passes separately with the native window
open. The SDK cache and precache suites pass: 84 tests, 2 pre-existing skips.
Both changed Dart files pass `dart analyze --fatal-infos` and `dart format`.
The rebuilt Release engine and existing release App snapshots use format
signature `78da37fed6bf1489361a312568249f3f`; this confirms snapshot format
compatibility and is not a physical-device execution test.

A broader run with every interactive window enabled exposes the unchanged
`DisplayListTest.CanDrawTextWithSaveLayer` fixture, which constructs
`DlTextSkia` objects whose `GetTextFrame` is always null. Impeller's existing
text-frame assertion rejects it. The fixture and assertion are unchanged by
this fix; this broader interactive run is not reported as passing.

Device Debug, Profile and Release engines build successfully for arm64; Debug
simulator engines also build for arm64 and x86_64.
The final arm64 iOS 27 simulator engine builds and launches the application.
The welcome page renders normally and the app process remains running. The
installed framework UUID matches the final engine
(`EB500DE9-A849-35E1-84AC-7923255BC38D`). This is a startup and first-page smoke
test, not a complete chat-session test. No iOS 16 physical-device verification
has been performed.

## Release packaging

The intended SDK release is `3.41.10-ohos-0.0.2-beta.operit.5`, published from
this fork with `ios-debug.zip`, `ios-profile.zip` and `ios-release.zip` plus
SHA-256 sidecars. `IOSEngineArtifacts` pins these release assets; Windows,
Linux, Web and the Dart engine ABI pin retain their existing versions.

`tools/build_ios_engine.sh` builds a selected arm64 device runtime and both
Debug simulator architectures. The simulator Debug engine is shared across
runtime archives, matching the existing SDK structure. The current iOS GN
framework target always links with application-extension safety; packaging
verifies its Mach-O flag and includes the framework under `extension_safe`.

`tools/package_ios_engine.py` refuses uncommitted tracked changes, verifies
platforms/architectures, rejects the old coverage assertion, generates matching
device dSYMs, and includes a manifest with source revision, binary UUIDs and
file hashes. It retains the unchanged, signed AOT compiler from the official
cache matching `bin/internal/engine.version`; the rendering-only engine change
does not change the compiler or Dart ABI.

Commit and push the final source before tagging or publishing artifacts. The
GitHub release is a regular release. App SDK selection and TestFlight
publication are separate integration steps.

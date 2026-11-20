# Operit.8 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.8` (2026-10-29).

## Windows accessibility channel

The Windows accessibility plugin accepts the framework's `tap` and `tooltip`
messages without logging an unsupported-message warning. These events did not
have Windows-specific handling before this change and still do not trigger
accessibility announcements. Message structure is validated before accepting
either event. Unknown event types retain their warning, malformed messages retain
their errors, and `announce` handling is unchanged.

This release does not change snapshot logging, SkSL logging, GPU cache directory
handling, or Operit application synchronization.

## Distribution

Windows x64 Debug, Profile and Release engines must be rebuilt from this source.
The Windows artifact cache selects `.operit.8`, ensuring SDK consumers download
the updated DLLs instead of retaining `.operit.3` binaries. Each archive includes
its source revision and per-file SHA-256 hashes in `engine-release.json`.

The build explicitly retains engine ABI pin
`42d3d75a56efe1a2e9902f52dc8006099c45d937` to match the existing Dart compiler set.
Web, Linux, iOS, Android and OHOS artifact pins remain unchanged.

## Verification

The tool cache and desktop compiler suites pass 88 tests, with two existing
skips. Both modified Dart files pass `dart analyze --fatal-infos` and
`dart format`.

The Windows release build must pass the shared C++ tests and focused Windows
accessibility and native composition tests. Five new plugin regressions cover
silent tap and tooltip handling, retained unknown-event warnings, and retained
validation errors for malformed tap and tooltip data. The engine-level tooltip
regression is also included in the release test filter. Test reports are retained
as CI artifacts; completed native build results belong in the published notes.

## Consumer update

Select this SDK tag with FVM using
`flutterUrl: https://github.com/AAswordman/flutter-ohos.git`, run
`fvm flutter precache --windows`, and rebuild the Windows application bundle.
Hot reload does not replace the running engine DLL.

# Operit.12 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.12` (2026-11-24).

## Linux fontconfig glyph fallback

Enable fontconfig for the Linux engine builds. Every shipped Linux engine since
Operit.4 was configured without `--enable-fontconfig`, which compiled out the
`fontmgr_fontconfig` port and left `txt` with the directory-scanning font
manager whose `onMatchFamilyStyleCharacter` always returns null. Text set in a
font family that lacks a glyph — most visibly Chinese inside monospace code
blocks — rendered as tofu on Linux, and fontconfig generic aliases such as
`monospace` never resolved. The fontconfig port matches fonts by character set
and language, restoring per-character fallback on desktop Linux; Android,
Windows, macOS, iOS and OHOS font managers already implement it.

The Linux artifact stamp in `flutter_tools` moves from Operit.4 to Operit.12 so
SDK consumers select the rebuilt binaries. The packaged artifact verifier now
rejects ELF outputs that do not link fontconfig, preventing a silent regression
of the build configuration.

## Distribution and verification

The SDK publishes newly built Linux x64 and arm64 Debug, Profile and Release
archives from this release. Before publishing, the release verifier checks each
archive's source identity and every file's SHA256 and requires the native test
reports to have executed successfully. Windows, Web, iOS, Android and OHOS
artifact pins retain their existing versions: none of those engines changed in
this release, and the Android engine remains distributed through the
`flutter-ohos-engine-maven` repository.

## Consumer update

Select this SDK tag using FVM with
`flutterUrl: https://github.com/AAswordman/flutter-ohos.git`, download the Linux
artifacts and rebuild the Linux application bundle. The installed application
must include the new `libflutter_linux_gtk.so`; hot reload cannot replace it.
The first Linux artifact download requires this release's assets to exist.

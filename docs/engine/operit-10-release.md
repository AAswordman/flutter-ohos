# Operit.10 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.10` (2026-11-21).

## Windows floating-window rendering

Fix the Windows engine's empty-frame resize path to create its render surface
with width and height in the correct order. Previously a resize completed by an
empty frame transposed the EGL surface dimensions, allowing subsequent content
to appear clipped, scaled or displaced inside an otherwise correctly sized
native window. Asynchronously loaded floating-window content can expose this
path during startup and resizing.

Regression coverage checks square (`500 × 500`), landscape (`800 × 600`) and
portrait (`600 × 800`) surfaces, including window metrics and actual EGL surface
creation dimensions. Windows engine builds run these regressions alongside the
existing window resize, accessibility and native composition suites.

## Distribution and verification

The SDK selects newly built Windows x64 Debug, Profile and Release archives
from this release. Before publishing, the release verifier checks each
archive's source identity and every file's SHA256, checks the native test
reports, and requires all three empty-frame regressions and the adjacent resize
tests to have executed successfully. The release includes `SHA256SUMS`, a
source/ABI manifest and the native test reports.

The engine ABI remains `42d3d75a56efe1a2e9902f52dc8006099c45d937`.
Web, Linux, iOS, Android and OHOS artifact pins retain their existing versions.
Native engine regression results do not establish an interactive replay of the
reported Operit application incident.

## Consumer update

Select this SDK tag using FVM with
`flutterUrl: https://github.com/AAswordman/flutter-ohos.git`, download the Windows
artifacts and rebuild the Windows application bundle. The installed application
must include the new `flutter_windows.dll`; hot reload cannot replace it.

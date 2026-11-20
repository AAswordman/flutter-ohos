# Operit.7 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.7` (2026-10-28).

## Wait for selection container layout

An obscured `OverlayEntry` with `maintainState: true` can build its text and
selection containers without laying them out. The selection delegates previously
flushed their pending children anyway. Sorting then read `paintBounds`, `size`,
or text layout from an unlaid-out render object and failed with `hasSize` or
`debugNeedsLayout` assertions.

`MultiSelectableSelectionContainerDelegate` now retains its pending update until
its container has a size. It retries after a subsequent frame, without requesting
frames or repeatedly scheduling microtasks while layout is unavailable. Once the
container is laid out, it processes the original pending additions normally.
Nested additions that propagate after a frame request a visual update so that
ancestor delegates also process them and selection works after revealing the
entry.

`SelectionContainerDelegate.hasSize` returns false when it has no associated
render object, so a pending callback also tolerates delegate replacement and
container removal. Geometry getters retain their existing contracts; this fix
prevents premature geometry consumption instead of returning placeholder bounds.

The shared delegate handles both ordinary selection containers and the text
container comparator used with `WidgetSpan`. The fix applies to all platforms.
Related upstream report: flutter/flutter#151536.

## Verification

- All four new layout regression tests fail on the unmodified Operit.6 framework.
- The regressions verify hidden containers awaiting their first layout,
  selection and copied content after revealing the entry, nested `WidgetSpan`
  text, removal of a pending child, disposal while waiting, and no continuously
  requested frames while hidden.
- 244 widget tests pass, with 10 existing platform-specific skips: the four new
  regressions, six existing selection container tests, five synchronous-
  unregistration regressions, and the full `selectable_region_test.dart` suite.
- The existing suite now includes OHOS in two previously non-exhaustive switches
  and drains the OHOS frame-rate velocity timer in its haptic-feedback test.
  The timer failure was independently reproduced on Operit.6.
- All modified Dart files pass `dart analyze --fatal-infos` and `dart format`.

## Distribution

This release changes framework Dart source and tests only. Publish the SDK Git
tag and these notes; no native engine or Web engine archives need rebuilding.

Artifact pins remain Web Operit.2, Windows Operit.3, Linux Operit.4 and iOS
Operit.5, with the existing Android and OHOS artifact revisions.
`bin/internal/engine.version` is
`42d3d75a56efe1a2e9902f52dc8006099c45d937`.

Consumers select this tag using FVM, regenerate their dependency configuration,
and rebuild the application to include the framework fix.

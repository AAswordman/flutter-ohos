# Operit.6 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.6` (2026-10-27).

## Selection event dispatch

`MultiSelectableSelectionContainerDelegate.handleClearSelection` and
`handleSelectAll` previously iterated the live selectable list. Dispatching an
event can synchronously unregister a child through `SelectionRegistrant`, which
mutates that list and throws `ConcurrentModificationError`. This also affects
selection propagated through nested scrollables.

Both handlers now iterate a snapshot and check that each child is still
registered before dispatch. A child removed by an earlier callback receives no
further event. Selection indices continue to use the current registered list.
The fix applies to the shared Flutter framework on all platforms.

Related upstream issue: flutter/flutter#192081. Related upstream fix:
flutter/flutter#192529.

## Verification

- All five new regression tests reproduce `ConcurrentModificationError` on the
  unmodified Operit.5 framework.
- All five pass with this fix: self-unregistration and removal of a later child
  during clear and select-all, plus select-all through a nested scrollable.
- The six existing `selection_container_test.dart` tests also pass: 11 passing
  widget tests in total.
- Both modified Dart files pass `dart analyze --fatal-infos` and `dart format`.
- The existing `selectable_region_test.dart` suite is not included in this run:
  two pre-existing exhaustive switches omit this fork's `TargetPlatform.ohos`.
  The new regression file exercises the production dispatch and registration
  paths without introducing platform-specific branches.

## Distribution

This release changes framework Dart source only. Publish the SDK Git tag and
release notes; no new native engine or Web engine archives are required.

Keep the existing artifact pins in `flutter_cache.dart`:

- Web: Operit.2.
- Windows: Operit.3.
- Linux: Operit.4.
- iOS: Operit.5.
- Android and OHOS: their existing artifact revisions.

`bin/internal/engine.version` remains
`42d3d75a56efe1a2e9902f52dc8006099c45d937`. Dart SDK and AOT compiler artifacts
are unchanged.

Consumers select this tag using FVM and rebuild their Flutter application to
include the corrected framework. Existing installed application binaries do not
receive framework source changes automatically.

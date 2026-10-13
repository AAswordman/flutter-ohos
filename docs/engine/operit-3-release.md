# Operit.3 SDK release

Release: `3.41.10-ohos-0.0.2-beta.operit.3` (2026-10-13).

## Accessibility behavior

Reparenting an accessibility node deletes its entire old AXTree subtree. Flutter
semantics batches contain only changed nodes, so the bridge previously omitted
unchanged descendants when recreating that subtree. An update of a leaf such as
`452`, below an unchanged intermediate node, could fail with
`452 will not be in the tree and is not the new root`.

The shared bridge now preserves the old subtree before detachment, applies the
current batch's data, and serializes nodes in the final tree's parent-before-child
order. Removed descendants stay removed. Simultaneous ancestor and descendant
moves no longer update already-deleted parents during the detachment phase.
Focus and selection updates are derived only from submitted nodes.

Traversal is linear in the final tree's node count. Preserved subtree data is
allocated only for moves; each old node is preserved once per batch.

## Artifacts and tooling

Windows x64 Debug, Profile and Release engines are rebuilt from this release's
source. Each archive includes the DLL, import/export libraries, symbols, public
headers, client wrapper, ICU data, license, `gen_snapshot`, and a manifest with
source revision and per-file SHA-256 hashes.

The Windows cache version and download URLs select `.operit.3`, invalidating the
previous native engine cache. Web artifacts still select the verified `.operit.2`
Web SDK. Desktop compiler artifacts continue to use the coherent compiler set
matching engine ABI pin `42d3d75a56efe1a2e9902f52dc8006099c45d937`.
OHOS artifacts and other platforms' native engine distributions are unchanged.

The tool cache no longer imports a global `cache` getter that shadowed the
inherited cache field. OHOS version resolution occurs only when an OHOS-provided
archive is used. Existing cache download tests and the new `.operit.3` Windows
download test validate this behavior.

The native build script includes shared accessibility regression tests and
Windows accessibility tests. The CI upload step now points at the actual ZIP.
The Windows `tooltip` channel warning is independent of the AXTree defect.

## Verification

- Shared C++ suite: 234 tests passed, including all 17 accessibility bridge tests
  and the three new subtree reparenting regressions.
- Windows accessibility and native composition suite: 43 tests passed.
- Tool cache and desktop compiler suites: 83 tests passed; two existing lock tests
  remain skipped by their upstream test definitions.
- Modified Dart sources pass `dart analyze --fatal-infos` and `dart format`.
- Release archives carry per-file provenance and hashes; the release manifest
  and `SHA256SUMS` cover the published archives and test results.

## Consumer update

Pin FVM to this version with
`flutterUrl: https://github.com/AAswordman/flutter-ohos.git`, install it, and run
`fvm flutter precache --windows` from the application directory. Rebuild the
Windows application bundle to use the new engine. Hot reload and source-only
SDK updates do not replace an already-running native DLL.

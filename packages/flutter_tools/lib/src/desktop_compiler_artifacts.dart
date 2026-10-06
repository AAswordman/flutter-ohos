// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'base/common.dart';
import 'base/error_handling_io.dart';
import 'base/file_system.dart';
import 'base/os.dart';
import 'base/platform.dart';
import 'cache.dart';

/// A complete compiler SDK matching the non-OHOS desktop engine revision.
///
/// The bootstrap SDK and common patched SDK in this fork use the OHOS ABI
/// numbering. Feeding their kernel output to a desktop gen_snapshot truncates
/// IntPtr values on x64. Keep the frontend, its runtime and both platform kernels
/// together, separate from the OHOS cache. Do not replace just one platform dill.
class DesktopCompilerArtifacts extends CachedArtifact {
  DesktopCompilerArtifacts(Cache cache, {required Platform platform})
    : _platform = platform,
      super('desktop_compiler', cache, DevelopmentArtifact.universal);

  final Platform _platform;

  @override
  Directory get location => cache.getArtifactDirectory('engine').childDirectory('desktop-compiler');

  @override
  String get version => '${cache.engineRevision}-$_sdkHost-${cache.getHostPlatformArchName()}';

  String get _sdkHost => switch (_platform.operatingSystem) {
    'macos' => 'darwin',
    'linux' => 'linux',
    'windows' => 'windows',
    _ => throwToolExit(
      'Desktop compiler artifacts are not available on ${_platform.operatingSystem}.',
    ),
  };

  String get _executableSuffix => _platform.isWindows ? '.exe' : '';

  @override
  bool isUpToDateInner(FileSystem fileSystem) {
    return <String>[
      'dart-sdk/bin/dart$_executableSuffix',
      'dart-sdk/bin/dartaotruntime$_executableSuffix',
      'dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot',
      'common/flutter_patched_sdk/platform_strong.dill',
      'common/flutter_patched_sdk_product/platform_strong.dill',
    ].every((String path) => location.childFile(path).existsSync());
  }

  @override
  Future<void> updateInner(
    ArtifactUpdater artifactUpdater,
    FileSystem fileSystem,
    OperatingSystemUtils operatingSystemUtils,
  ) async {
    final baseUrl =
        '${cache.storageBaseUrl}/flutter_infra_release/flutter/${cache.engineRevision}/';
    // Evict the entire old compiler set before changing revision. A failed
    // download must never leave a mixture of platform kernels and frontends.
    ErrorHandlingFileSystem.deleteIfExists(location, recursive: true);
    location.createSync(recursive: true);
    await artifactUpdater.downloadZipArchive(
      'Downloading desktop Dart SDK...',
      Uri.parse('${baseUrl}dart-sdk-$_sdkHost-${cache.getHostPlatformArchName()}.zip'),
      location,
    );
    for (final sdk in <String>['flutter_patched_sdk', 'flutter_patched_sdk_product']) {
      await artifactUpdater.downloadZipArchive(
        'Downloading desktop $sdk...',
        Uri.parse('$baseUrl$sdk.zip'),
        location.childDirectory('common'),
      );
    }
    if (!_platform.isWindows) {
      final Directory bin = location.childDirectory('dart-sdk').childDirectory('bin');
      for (final File file in bin.listSync(recursive: true).whereType<File>()) {
        final isUserExecutable = ((file.statSync().mode >> 6) & 0x1) == 1;
        if (isUserExecutable || file.basename == 'dart' || file.basename == 'dartaotruntime') {
          operatingSystemUtils.chmod(file, 'a+r,a+x');
        }
      }
    }
    if (!isUpToDateInner(fileSystem)) {
      throwToolExit(
        'The downloaded desktop compiler SDK is incomplete. Run flutter precache again.',
      );
    }
  }
}

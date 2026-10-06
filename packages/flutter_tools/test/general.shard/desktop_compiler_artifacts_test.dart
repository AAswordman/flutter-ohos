// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:file/memory.dart';
import 'package:flutter_tools/src/artifacts.dart';
import 'package:flutter_tools/src/base/file_system.dart';
import 'package:flutter_tools/src/base/logger.dart';
import 'package:flutter_tools/src/base/os.dart';
import 'package:flutter_tools/src/base/platform.dart';
import 'package:flutter_tools/src/build_info.dart';
import 'package:flutter_tools/src/build_system/build_system.dart';
import 'package:flutter_tools/src/build_system/targets/common.dart';
import 'package:flutter_tools/src/cache.dart';
import 'package:flutter_tools/src/compile.dart';
import 'package:flutter_tools/src/desktop_compiler_artifacts.dart';
import 'package:package_config/package_config.dart';
import 'package:test/fake.dart';

import '../src/common.dart';
import '../src/context.dart';
import '../src/fake_process_manager.dart';
import '../src/fakes.dart';

void main() {
  late FileSystem fileSystem;
  late Cache cache;
  late FakeOperatingSystemUtils osUtils;
  late Platform platform;

  setUp(() {
    fileSystem = MemoryFileSystem.test();
    platform = FakePlatform(environment: <String, String>{});
    osUtils = FakeOperatingSystemUtils();
    cache = Cache(
      rootOverride: fileSystem.directory('/flutter'),
      fileSystem: fileSystem,
      platform: platform,
      logger: BufferLogger.test(),
      osUtils: osUtils,
      artifacts: <ArtifactSet>[],
    );
    cache.getRoot().createSync(recursive: true);
    cache.setStampFor('engine', 'desktop-revision');
    cache.setStampFor('engine-dart-sdk', 'ohos-revision');
    fileSystem.file('/flutter/bin/internal/engine.ohos.version')
      ..createSync(recursive: true)
      ..writeAsStringSync('ohos-revision');
  });

  CachedArtifacts createArtifacts() => CachedArtifacts(
    fileSystem: fileSystem,
    cache: cache,
    platform: platform,
    operatingSystemUtils: osUtils,
  );

  for (final host in <(String, HostPlatform, String)>[
    ('linux', HostPlatform.linux_x64, 'linux-x64'),
    ('linux', HostPlatform.linux_arm64, 'linux-arm64'),
    ('macos', HostPlatform.darwin_x64, 'darwin-x64'),
    ('macos', HostPlatform.darwin_arm64, 'darwin-arm64'),
    ('windows', HostPlatform.windows_x64, 'windows-x64'),
    ('windows', HostPlatform.windows_arm64, 'windows-arm64'),
  ]) {
    testWithoutContext('downloads a coherent desktop compiler set on ${host.$3}', () async {
      platform = FakePlatform(operatingSystem: host.$1);
      osUtils.hostPlatform = host.$2;
      final compiler = DesktopCompilerArtifacts(cache, platform: platform);
      final updater = RecordingArtifactUpdater(windows: host.$1 == 'windows');
      compiler.location.childFile('old-kernel').createSync(recursive: true);
      await compiler.updateInner(updater, fileSystem, osUtils);

      expect(updater.urls, <Uri>[
        Uri.parse(
          'https://storage.googleapis.com/flutter_infra_release/flutter/desktop-revision/dart-sdk-${host.$3}.zip',
        ),
        Uri.parse(
          'https://storage.googleapis.com/flutter_infra_release/flutter/desktop-revision/flutter_patched_sdk.zip',
        ),
        Uri.parse(
          'https://storage.googleapis.com/flutter_infra_release/flutter/desktop-revision/flutter_patched_sdk_product.zip',
        ),
      ]);
      expect(compiler.isUpToDateInner(fileSystem), isTrue);
      expect(compiler.location.childFile('old-kernel').existsSync(), isFalse);
      expect(compiler.version, 'desktop-revision-${host.$3}');
      expect(cache.getStampFor('engine-dart-sdk'), 'ohos-revision');
      expect(compiler.location.path, '/flutter/bin/cache/artifacts/engine/desktop-compiler');
    });
  }

  testWithoutContext('an incomplete set cannot be stamped as up to date', () async {
    final compiler = DesktopCompilerArtifacts(cache, platform: platform);
    final updater = RecordingArtifactUpdater()..skipFrontend = true;
    await expectLater(
      compiler.update(updater, BufferLogger.test(), fileSystem, osUtils),
      throwsToolExit(message: 'The downloaded desktop compiler SDK is incomplete'),
    );
    expect(await compiler.isUpToDate(fileSystem), isFalse);
    expect(cache.getStampFor(compiler.stampName), isNull);
  });

  testWithoutContext('stamp and required files invalidate a previously cached set', () async {
    final compiler = DesktopCompilerArtifacts(cache, platform: platform);
    await compiler.update(RecordingArtifactUpdater(), BufferLogger.test(), fileSystem, osUtils);
    expect(await compiler.isUpToDate(fileSystem), isTrue);
    compiler.location
        .childFile('dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot')
        .deleteSync();
    expect(await compiler.isUpToDate(fileSystem), isFalse);
    cache.setStampFor(compiler.stampName, 'old-revision');
    expect(await compiler.isUpToDate(fileSystem), isFalse);
  });

  for (final target in <TargetPlatform>[
    TargetPlatform.linux_x64,
    TargetPlatform.linux_arm64,
    TargetPlatform.darwin,
    TargetPlatform.windows_x64,
    TargetPlatform.windows_arm64,
  ]) {
    for (final mode in <BuildMode>[BuildMode.profile, BuildMode.release]) {
      testWithoutContext('$target $mode routes the frontend, runtime and platform together', () {
        final CachedArtifacts artifacts = createArtifacts();
        const root = '/flutter/bin/cache/artifacts/engine/desktop-compiler';
        final sdkName = mode == BuildMode.release
            ? 'flutter_patched_sdk_product'
            : 'flutter_patched_sdk';
        final expected = <Artifact, String>{
          Artifact.engineDartSdkPath: '$root/dart-sdk',
          Artifact.engineDartBinary: '$root/dart-sdk/bin/dart',
          Artifact.engineDartAotRuntime: '$root/dart-sdk/bin/dartaotruntime',
          Artifact.frontendServerSnapshotForEngineDartSdk:
              '$root/dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot',
          Artifact.flutterPatchedSdkPath: '$root/common/$sdkName',
          Artifact.platformKernelDill: '$root/common/$sdkName/platform_strong.dill',
          Artifact.platformLibrariesJson: '$root/common/$sdkName/lib/libraries.json',
        };
        for (final MapEntry<Artifact, String> entry in expected.entries) {
          expect(artifacts.getArtifactPath(entry.key, platform: target, mode: mode), entry.value);
        }
        expect(
          artifacts.getArtifactPath(Artifact.genSnapshot, platform: target, mode: mode),
          isNot(contains('desktop-compiler')),
        );
      });
    }
  }

  testWithoutContext('Windows hosts use executable suffixes for the matching runtime', () {
    platform = FakePlatform(operatingSystem: 'windows');
    final CachedArtifacts artifacts = createArtifacts();
    expect(
      artifacts.getArtifactPath(
        Artifact.engineDartAotRuntime,
        platform: TargetPlatform.windows_x64,
        mode: BuildMode.release,
      ),
      endsWith('bin/dartaotruntime.exe'),
    );
    expect(
      artifacts.getArtifactPath(
        Artifact.engineDartBinary,
        platform: TargetPlatform.windows_x64,
        mode: BuildMode.release,
      ),
      endsWith('bin/dart.exe'),
    );
  });

  testWithoutContext('OHOS, desktop debug, and unspecified host keep their original SDK', () {
    final CachedArtifacts artifacts = createArtifacts();
    for (final target in <TargetPlatform?>[
      null,
      TargetPlatform.ohos_arm64,
      TargetPlatform.linux_x64,
    ]) {
      final BuildMode mode = target == TargetPlatform.linux_x64
          ? BuildMode.debug
          : BuildMode.release;
      expect(
        artifacts.getArtifactPath(Artifact.engineDartAotRuntime, platform: target, mode: mode),
        '/flutter/bin/cache/dart-sdk/bin/dartaotruntime',
      );
      final sdkName = mode == BuildMode.release
          ? 'flutter_patched_sdk_product'
          : 'flutter_patched_sdk';
      expect(
        artifacts.getArtifactPath(Artifact.flutterPatchedSdkPath, platform: target, mode: mode),
        '/flutter/bin/cache/artifacts/engine/common/$sdkName',
      );
    }
  });

  for (final mode in <BuildMode>[BuildMode.profile, BuildMode.release]) {
    testWithoutContext(
      'KernelCompiler propagates desktop platform and $mode to its runtime',
      () async {
        final CachedArtifacts artifacts = createArtifacts();
        final manager = FakeProcessManager.any();
        final logger = BufferLogger.test();
        final stdoutHandler = StdoutHandler(logger: logger, fileSystem: fileSystem);
        final compiler = KernelCompiler(
          fileSystem: fileSystem,
          logger: logger,
          processManager: manager,
          artifacts: artifacts,
          fileSystemRoots: <String>[],
          stdoutHandler: stdoutHandler,
        );
        final Future<CompilerOutput?> output = compiler.compile(
          sdkRoot: artifacts.getArtifactPath(
            Artifact.flutterPatchedSdkPath,
            platform: TargetPlatform.linux_x64,
            mode: mode,
          ),
          mainPath: '/main.dart',
          packagesPath: '/.dart_tool/package_config.json',
          packageConfig: PackageConfig.empty,
          buildMode: mode,
          targetPlatform: TargetPlatform.linux_x64,
          aot: true,
          targetOS: 'linux',
          trackWidgetCreation: false,
          dartDefines: <String>[],
        );
        stdoutHandler.compilerOutput!.complete(const CompilerOutput('/app.dill', 0, <Uri>[]));
        expect((await output)!.errorCount, 0);
        expect(
          logger.traceText,
          contains(
            '/flutter/bin/cache/artifacts/engine/desktop-compiler/dart-sdk/bin/dartaotruntime '
            '/flutter/bin/cache/artifacts/engine/desktop-compiler/dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot --sdk-root',
          ),
        );
        expect(logger.traceText, contains('--aot'));
      },
    );
  }

  for (final mode in <BuildMode>[BuildMode.profile, BuildMode.release]) {
    testUsingContext('KernelSnapshot uses the complete desktop $mode compiler set', () async {
      final CachedArtifacts artifacts = createArtifacts();
      final logger = BufferLogger.test();
      final manager = FakeProcessManager.empty();
      final Directory project = fileSystem.directory('/app');
      project.childFile('.dart_tool/package_config.json')
        ..createSync(recursive: true)
        ..writeAsStringSync('{"configVersion":2,"packages":[]}');
      final environment = Environment.test(
        project,
        defines: <String, String>{kBuildMode: mode.cliName, kTargetPlatform: 'linux-x64'},
        inputs: <String, String>{},
        artifacts: artifacts,
        processManager: manager,
        fileSystem: fileSystem,
        logger: logger,
      );
      environment.buildDir.createSync(recursive: true);
      final String build = environment.buildDir.path;
      const root = '/flutter/bin/cache/artifacts/engine/desktop-compiler';
      final sdk = mode == BuildMode.release ? 'flutter_patched_sdk_product' : 'flutter_patched_sdk';
      manager.addCommand(
        FakeCommand(
          command: <String>[
            '$root/dart-sdk/bin/dartaotruntime',
            '$root/dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot',
            '--sdk-root',
            '$root/common/$sdk/',
            '--target=flutter',
            '--no-print-incremental-dependencies',
            ...buildModeOptions(mode, <String>[]),
            if (mode != BuildMode.release) '--track-widget-creation',
            '--aot',
            '--tfa',
            '--target-os',
            'linux',
            '--packages',
            '/app/.dart_tool/package_config.json',
            '--output-dill',
            '$build/app.dill',
            '--depfile',
            '$build/kernel_snapshot_program.d',
            '--verbosity=error',
            'file:///lib/main.dart',
          ],
          stdout: 'result boundary\nboundary\nboundary $build/app.dill 0\n',
        ),
      );
      await const KernelSnapshot().build(environment);
      expect(manager, hasNoRemainingExpectations);
    });
  }
}

class RecordingArtifactUpdater extends Fake implements ArtifactUpdater {
  RecordingArtifactUpdater({this.windows = false});

  final bool windows;
  final urls = <Uri>[];
  bool skipFrontend = false;

  @override
  Future<void> downloadZipArchive(String message, Uri url, Directory location) async {
    urls.add(url);
    final String archive = url.pathSegments.last;
    if (archive.startsWith('dart-sdk-')) {
      final suffix = windows ? '.exe' : '';
      for (final path in <String>[
        'dart$suffix',
        'dartaotruntime$suffix',
        if (!skipFrontend) 'snapshots/frontend_server_aot.dart.snapshot',
      ]) {
        location.childFile('dart-sdk/bin/$path').createSync(recursive: true);
      }
    } else {
      final String sdk = archive.replaceAll('.zip', '');
      location.childFile('$sdk/platform_strong.dill').createSync(recursive: true);
    }
  }

  @override
  void removeDownloadedFiles() {}
}

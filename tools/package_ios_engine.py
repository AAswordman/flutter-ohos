#!/usr/bin/env python3
"""Package rebuilt iOS frameworks with the unchanged pinned AOT compiler."""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def validate_framework(framework, platform, architectures):
    binary = framework / 'Flutter'
    actual = set(run('lipo', '-archs', str(binary)).split())
    if actual != set(architectures):
        raise RuntimeError(f'Unexpected architectures in {binary}: {actual}')
    info = plistlib.loads((framework / 'Info.plist').read_bytes())
    if info['CFBundleExecutable'] != 'Flutter':
        raise RuntimeError(f'Invalid framework bundle: {framework}')
    # Check the Mach-O platform for every architecture, not just the plist.
    for arch in architectures:
        load = run('otool', '-arch', arch, '-l', str(binary))
        platform_id = '2' if platform == 'ios' else '7'
        if f'platform {platform_id}\n' not in load and f'platform {"IOS" if platform == "ios" else "IOSSIMULATOR"}\n' not in load:
            raise RuntimeError(f'Unexpected Mach-O platform: {binary} ({arch})')
    if b'back_texture\0' in binary.read_bytes():
        raise RuntimeError(f'The old fatal coverage assertion is still present: {binary}')
    return [line.split(')')[0] + ')' for line in run('dwarfdump', '--uuid', str(binary)).splitlines()]


def package(root, mode, version, baseline, destination):
    if run('git', '-C', str(root), 'status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('Commit all tracked source changes before packaging a release')
    revision = run('git', '-C', str(root), 'rev-parse', 'HEAD')
    engine_abi = (root / 'bin/internal/engine.version').read_text().strip()
    expected_baseline = 'ios' if mode == 'debug' else f'ios-{mode}'
    if baseline.name != expected_baseline:
        raise RuntimeError(f'Expected the official {expected_baseline} compiler cache')
    baseline_stamp = baseline.parents[2] / 'ios-sdk.stamp'
    if not baseline_stamp.is_file() or baseline_stamp.read_text().strip() != engine_abi:
        raise RuntimeError('The baseline compiler must come from the pinned official iOS engine cache')
    source = root / 'engine/src/out'
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ios-engine-package-') as directory:
        staging = Path(directory)
        simulator = staging / 'simulator/Flutter.framework'
        shutil.copytree(source / 'ios_debug_sim_arm64/Flutter.framework', simulator)
        subprocess.run([
            'lipo', '-create',
            str(source / 'ios_debug_sim_arm64/Flutter.framework/Flutter'),
            str(source / 'ios_debug_sim/Flutter.framework/Flutter'),
            '-output', str(simulator / 'Flutter'),
        ], check=True)
        validate_framework(simulator, 'simulator', ['arm64', 'x86_64'])
        device = source / f'ios_{mode}/Flutter.framework'
        validate_framework(device, 'ios', ['arm64'])
        contents = staging / 'contents'
        contents.mkdir()
        symbols = staging / 'Flutter.framework.dSYM'
        subprocess.run(['dsymutil', str(device / 'Flutter'), '-o', str(symbols)], check=True)
        device_uuid = run('dwarfdump', '--uuid', str(device / 'Flutter')).split()[1]
        symbols_uuid = run('dwarfdump', '--uuid', str(symbols)).split()[1]
        if device_uuid != symbols_uuid:
            raise RuntimeError('The device symbols do not match the engine UUID')
        subprocess.run([
            'xcodebuild', '-create-xcframework', '-framework', str(device),
            '-debug-symbols', str(symbols), '-framework', str(simulator),
            '-output', str(contents / 'Flutter.xcframework'),
        ], check=True)
        # Flutter's current iOS GN framework is always linked application-extension
        # safe. The obsolete --darwin-extension-safe switch is no longer consumed.
        binary_load = run('otool', '-hv', str(device / 'Flutter'))
        if 'APP_EXTENSION_SAFE' not in binary_load:
            raise RuntimeError('The engine is not safe for application extensions')
        shutil.copytree(contents / 'Flutter.xcframework', contents / 'extension_safe/Flutter.xcframework')
        compiler = baseline / 'gen_snapshot_arm64'
        dart_version = (source / f'ios_{mode}/gen/flutter/third_party/dart/runtime/version.cc').read_text()
        snapshot_hash = re.search(r'snapshot_hash_ = "([0-9a-f]{32})"', dart_version).group(1)
        if snapshot_hash.encode() not in compiler.read_bytes() or snapshot_hash.encode() not in (device / 'Flutter').read_bytes():
            raise RuntimeError('The AOT compiler and engine snapshot formats do not match')
        shutil.copy2(compiler, contents / compiler.name)
        shutil.copy2(baseline / 'LICENSE', contents / 'LICENSE')
        (contents / compiler.name).chmod(0o755)
        subprocess.run([str(contents / compiler.name), '--version'], check=True)
        uuids = {}
        for framework in sorted(contents.rglob('Flutter.framework')):
            if not (framework / 'Flutter').is_file():
                continue
            uuids[str(framework.relative_to(contents))] = [
                line.split(')')[0] + ')'
                for line in run('dwarfdump', '--uuid', str(framework / 'Flutter')).splitlines()
            ]
        manifest = {
            'frameworkVersion': version,
            'frameworkRevision': revision,
            'engineSourceRevision': revision,
            'engineRevision': engine_abi,
            'runtimeMode': mode,
            'simulatorRuntimeMode': 'debug',
            'targets': ['ios-arm64', 'ios-arm64_x86_64-simulator'],
            'compilerEngineRevision': engine_abi,
            'dartSnapshotHash': snapshot_hash,
            'compilerSha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
            'binaryUuids': uuids,
            'files': {
                str(p.relative_to(contents)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(contents.rglob('*')) if p.is_file()
            },
        }
        (contents / 'engine-release.json').write_text(json.dumps(manifest, indent=2) + '\n')
        archive = destination / f'ios-{mode}.zip'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as target:
            for path in sorted(contents.rglob('*')):
                if path.is_file():
                    target.write(path, str(path.relative_to(contents)))
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n')
        print(f'Packaged {archive} from {revision}')
        return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['debug', 'profile', 'release'])
    parser.add_argument('version')
    parser.add_argument('--baseline', type=Path, required=True,
                        help='Official pinned bin/cache/artifacts/engine/ios[-profile|-release]')
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    package(Path(__file__).resolve().parent.parent, args.mode, args.version,
            args.baseline.resolve(), args.destination.resolve())


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Packages a GN-built Linux engine with its exact source and file hashes."""

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile


# Validates the native contract before creating a release archive.
def package(root: Path, archive: Path, mode: str, version: str, arch: str) -> Path:
    revision = subprocess.check_output(
        ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True
    ).strip()
    with zipfile.ZipFile(archive) as source:
        files = {name: source.read(name) for name in source.namelist() if not name.endswith('/')}
    for required in ['libflutter_linux_gtk.so', 'gen_snapshot', 'flutter_linux/fl_view.h']:
        if required not in files:
            raise RuntimeError(f'Missing GN artifact: {required}')
    for name in ['libflutter_linux_gtk.so', 'gen_snapshot']:
        binary = files[name]
        if binary[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', binary, 18)[0] != {'x64': 62, 'arm64': 183}[arch]:
            raise RuntimeError(f'Incorrect ELF architecture for {name}: expected {arch}')
    files['LICENSE'] = (root / 'LICENSE').read_bytes()
    header = files['flutter_linux/fl_view.h'].decode()
    if '#define FL_NATIVE_VIEW_COMPOSITION_API_VERSION 1' not in header:
        raise RuntimeError('The native-composition public contract is missing')
    library = root / f'engine/src/out/host_{mode}/libflutter_linux_gtk.so'
    symbols = subprocess.check_output(['nm', '-D', '--defined-only', str(library)], text=True)
    exports = {line.split()[-1] for line in symbols.splitlines()}
    for symbol in ['fl_view_get_native_view_overlay', 'fl_view_register_native_view', 'fl_view_unregister_native_view']:
        if symbol not in exports:
            raise RuntimeError(f'Missing native-composition symbol: {symbol}')
    manifest = {
        'frameworkVersion': version,
        'frameworkRevision': revision,
        'engineSourceRevision': revision,
        'engineRevision': (root / 'bin/internal/engine.version').read_text().strip(),
        'target': f'linux-{arch}',
        'runtimeMode': mode,
        'nativeViewCompositionApiVersion': 1,
        'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())},
    }
    files['engine-release.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    output = root / f'engine/src/out/host_{mode}/linux-{arch}-{mode}.zip'
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = (0o100755 if name in {'gen_snapshot', 'libflutter_linux_gtk.so'} else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            target.writestr(info, data)
    checksum = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.zip.sha256').write_text(f'{checksum}  {output.name}\n')
    print(f'Engine artifact: {output}')
    return output


# Requires explicit build metadata for every publishable engine artifact.
def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit('Usage: package_linux_native_engine.py ROOT GN_ARCHIVE MODE VERSION ARCH')
    package(Path(sys.argv[1]), Path(sys.argv[2]), *sys.argv[3:])


if __name__ == '__main__':
    main()

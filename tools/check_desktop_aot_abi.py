#!/usr/bin/env python3
# Copyright 2026 The Flutter Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Compile an IntPtr/native-resolver probe with the isolated desktop compiler.

Run flutter precache for your desktop first. This is a compiler check, NOT a
GUI/runtime smoke test. The deliberately nonexistent native symbol is never
executed. Full logs and generated artifacts are retained in --output.
"""

import argparse
import hashlib
import json
import platform
import re
import subprocess
import tempfile
from pathlib import Path

PROBE = """import 'dart:ffi';

@Native<IntPtr Function()>(symbol: 'operit_abi_probe_unused_symbol')
external int readNativeAddress();

void main() {
  print(sizeOf<IntPtr>());
  print(sizeOf<Pointer<Void>>());
  print(readNativeAddress());
}
"""


def run(command, log):
    with log.open("w") as stream:
        subprocess.run([str(arg) for arg in command], stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    output = args.output or Path(tempfile.mkdtemp(prefix="desktop-aot-abi-"))
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    system = platform.system()
    host, target_os = {
        "Darwin": ("darwin", "macos"),
        "Linux": ("linux", "linux"),
        "Windows": ("windows", "windows"),
    }[system]
    # Published macOS gen_snapshot binaries run as x64, including on arm64
    # via Rosetta. The frontend/runtime themselves remain host-native.
    arch = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    gen_arch = "x64" if system == "Darwin" else arch
    suffix = ".exe" if system == "Windows" else ""
    engine = sdk / "bin/cache/artifacts/engine"
    compiler = engine / "desktop-compiler"
    dart_sdk = compiler / "dart-sdk"
    dart = dart_sdk / ("bin/dart" + suffix)
    runtime = dart_sdk / ("bin/dartaotruntime" + suffix)
    frontend = dart_sdk / "bin/snapshots/gen_kernel_aot.dart.snapshot"
    probe = output / "probe.dart"
    probe.write_text(PROBE)
    run([dart, "analyze", "--fatal-infos", probe], output / "analyze.log")
    run([dart, "format", probe], output / "format.log")

    results = []
    for mode in ("profile", "release"):
        patched_name = "flutter_patched_sdk_product" if mode == "release" else "flutter_patched_sdk"
        patched = compiler / "common" / patched_name / "platform_strong.dill"
        kernel = output / (mode + ".dill")
        gen_snapshot = engine / f"{host}-{gen_arch}-{mode}" / ("gen_snapshot" + suffix)
        run([runtime, frontend, "--platform", patched, "--target=flutter",
             "--target-os", target_os, "--aot", "--output", kernel, probe],
            output / (mode + "-kernel.log"))
        for name, function in (("intptr", "readNativeAddress"), ("resolver", "ffiClosure0")):
            log = output / f"{mode}-{name}.log"
            run([gen_snapshot, "--snapshot_kind=app-aot-elf",
                 "--elf=" + str(output / f"{mode}-{name}.so"),
                 "--print-flow-graph-filter=" + function,
                 "--print-flow-graph-optimized", "--disassemble",
                 "--disassemble-relative", kernel], log)
            calls = [line.strip() for line in log.read_text().splitlines()
                     if "FfiCall:" in line]
            if not calls or not all(re.search(r"\bint64\s*$", call) for call in calls):
                raise RuntimeError(f"{mode} {name}: expected IntPtr/native return int64; see {log}")
            results.append({"mode": mode, "function": name, "ffiCalls": calls,
                            "genSnapshotSha256": hashlib.sha256(gen_snapshot.read_bytes()).hexdigest()})
            print(f"PASS {mode} {name}: int64")
    (output / "result.json").write_text(json.dumps({
        "sdk": str(sdk), "host": system, "targetOS": target_os,
        "engineRevision": (sdk / "bin/cache/engine.stamp").read_text().strip(),
        "compilerStamp": (sdk / "bin/cache/desktop_compiler.stamp").read_text().strip(),
        "runtimeSmokeTest": False, "results": results,
    }, indent=2))
    print(f"Evidence: {output}")


if __name__ == "__main__":
    main()

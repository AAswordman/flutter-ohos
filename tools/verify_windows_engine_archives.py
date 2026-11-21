"""Validate Windows engine source identity, archive integrity and resize tests."""

import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile


def digest(stream):
    """Compute a streaming SHA256 without loading engine symbols into memory."""
    result = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        result.update(chunk)
    return result.hexdigest()


def require(condition, message):
    """Reject invalid release artifacts even when Python assertions are disabled."""
    if not condition:
        raise ValueError(message)


def verify(root, revision, version, engine_revision):
    """Verify all three archives and executed regressions before writing checksums."""
    required = {
        "flutter_windows.dll", "flutter_windows.dll.lib", "flutter_windows.dll.exp",
        "flutter_windows.dll.pdb", "flutter_windows.h", "flutter_export.h",
        "flutter_macros.h", "flutter_messenger.h", "flutter_plugin_registrar.h",
        "flutter_texture_registrar.h", "icudtl.dat", "gen_snapshot.exe", "LICENSE",
    }
    archives = []
    for mode in ("debug", "profile", "release"):
        path = root / f"windows-x64-{mode}.zip"
        with zipfile.ZipFile(path) as archive:
            names = [item.filename for item in archive.infolist() if not item.is_dir()]
            require(len(names) == len(set(names)), f"Duplicate entries: {path}")
            require(all("\\" not in name and not name.startswith("/")
                        and ".." not in Path(name).parts for name in names),
                    f"Unsafe archive paths: {path}")
            manifest = json.loads(archive.read("engine-release.json").decode("utf-8-sig"))
            expected = {
                "frameworkVersion": version,
                "frameworkRevision": revision,
                "engineSourceRevision": revision,
                "engineRevision": engine_revision,
                "target": "windows-x64",
                "runtimeMode": mode,
            }
            for key, value in expected.items():
                require(manifest[key] == value, f"Unexpected {key}: {path}")
            require(required.issubset(manifest["files"]), f"Missing engine files: {path}")
            require(any(name.startswith("cpp_client_wrapper/") for name in names),
                    f"Missing C++ wrapper: {path}")
            require(set(names) == set(manifest["files"]) | {"engine-release.json"},
                    f"Archive/manifest file mismatch: {path}")
            for name, expected_hash in manifest["files"].items():
                with archive.open(name) as stream:
                    require(digest(stream) == expected_hash, f"Hash mismatch: {mode}/{name}")
            with archive.open("flutter_windows.dll") as stream:
                require(stream.read(2) == b"MZ", f"Invalid Windows DLL: {path}")
        with path.open("rb") as stream:
            sha256 = digest(stream)
        archives.append({"name": path.name, "sha256": sha256, "size": path.stat().st_size})
        print(f"Verified {path.name}")

    reports = []
    for name in ("common-unit-tests.xml", "windows-unit-tests.xml"):
        suite = ET.parse(root / name).getroot()
        require(int(suite.attrib["tests"]) > 0, f"Empty test report: {name}")
        require(int(suite.attrib.get("failures", 0)) == 0
                and int(suite.attrib.get("errors", 0)) == 0,
                f"Failed test report: {name}")
        if name == "windows-unit-tests.xml":
            expected_cases = {
                ("SurfaceShapes/EmptyFrameResizeTest", f"PreservesSurfaceDimensions/{index}")
                for index in range(3)
            } | {
                ("FlutterWindowsViewTest", "WindowResizeTests"),
                ("FlutterWindowsViewTest", "WindowResizeRace"),
            }
            executed = {
                (case.attrib["classname"], case.attrib["name"])
                for case in suite.findall(".//testcase")
                if case.attrib.get("status") == "run"
                and case.attrib.get("result") == "completed"
                and case.find("skipped") is None and case.find("failure") is None
            }
            require(expected_cases.issubset(executed),
                    f"Resize regressions did not execute: {expected_cases - executed}")
        reports.append({"name": name, **suite.attrib})
        print(f"Verified {name}: {suite.attrib['tests']} tests")

    release = {
        "version": version, "sourceRevision": revision, "engineRevision": engine_revision,
        "archives": archives, "testReports": reports,
    }
    (root / "release-manifest.json").write_text(
        json.dumps(release, indent=2) + "\n", encoding="utf-8")
    (root / "SHA256SUMS").write_text(
        "".join(f"{item['sha256']}  {item['name']}\n" for item in archives), encoding="ascii")


def main():
    """Read the expected release identity from explicit command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--engine-revision", required=True)
    args = parser.parse_args()
    verify(args.root, args.revision, args.version, args.engine_revision)


if __name__ == "__main__":
    main()

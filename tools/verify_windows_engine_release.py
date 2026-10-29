"""Verify downloaded Windows engine archives before publishing a release."""

import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
import zipfile


def digest_stream(stream):
    """Hash a file without loading its symbols into memory."""
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def main():
    """Validate source provenance, all archived files, and native test reports."""
    root = Path(sys.argv[1]).resolve()
    source_revision = sys.argv[2]
    version = sys.argv[3]
    engine_revision = "42d3d75a56efe1a2e9902f52dc8006099c45d937"
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
            assert len(names) == len(set(names)), f"Duplicate ZIP entries: {path}"
            assert all("\\" not in name and not name.startswith("/")
                       and ".." not in Path(name).parts for name in names), path
            manifest = json.loads(archive.read("engine-release.json").decode("utf-8-sig"))
            assert manifest["frameworkVersion"] == version, manifest
            assert manifest["frameworkRevision"] == source_revision, manifest
            assert manifest["engineSourceRevision"] == source_revision, manifest
            assert manifest["engineRevision"] == engine_revision, manifest
            assert manifest["runtimeMode"] == mode, manifest
            assert manifest["target"] == "windows-x64", manifest
            assert required.issubset(manifest["files"]), path
            assert any(name.startswith("cpp_client_wrapper/") for name in names), path
            assert set(names) == set(manifest["files"]) | {"engine-release.json"}, path
            for name, expected in manifest["files"].items():
                with archive.open(name) as stream:
                    assert digest_stream(stream) == expected, f"Hash mismatch: {mode}/{name}"
            with archive.open("flutter_windows.dll") as stream:
                assert stream.read(2) == b"MZ", path
            print(f"Verified {path.name}: {len(manifest['files'])} file hashes")
        with path.open("rb") as stream:
            digest = digest_stream(stream)
        archives.append({"name": path.name, "sha256": digest, "size": path.stat().st_size})

    reports = []
    for name in ("common-unit-tests.xml", "windows-unit-tests.xml"):
        path = root / name
        tree = ET.parse(path)
        suite = tree.getroot()
        assert int(suite.attrib["tests"]) > 0, name
        assert int(suite.attrib.get("failures", 0)) == 0, name
        assert int(suite.attrib.get("errors", 0)) == 0, name
        cases = tree.findall(".//testcase")
        if name == "windows-unit-tests.xml":
            expected = {
                "TapDoesNotLog", "TooltipDoesNotLog", "UnsupportedMessageStillLogs",
                "MalformedTapDataStillLogs", "MalformedTooltipDataStillLogs",
            }
            for case in cases:
                if case.attrib.get("classname") == "AccessibilityPluginTest":
                    assert case.attrib.get("status") == "run", case.attrib
                    assert case.attrib.get("result") == "completed", case.attrib
            actual = {case.attrib["name"] for case in cases
                      if case.attrib.get("classname") == "AccessibilityPluginTest"}
            assert expected.issubset(actual), actual
            assert any(case.attrib.get("classname") == "FlutterWindowsEngineTest"
                       and case.attrib["name"] == "AccessibilityTooltip"
                       and case.attrib.get("result") == "completed" for case in cases)
        summary = {"name": name, **suite.attrib}
        reports.append(summary)
        print(f"Verified {name}: {suite.attrib}")

    manifest = {
        "version": version, "sourceRevision": source_revision,
        "engineRevision": engine_revision, "archives": archives, "testReports": reports,
    }
    (root / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (root / "SHA256SUMS").write_text(
        "".join(f"{item['sha256']}  {item['name']}\n" for item in archives), encoding="ascii")


if __name__ == "__main__":
    main()

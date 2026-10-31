"""Package hash-checked Bayer Drift source, module and validation evidence."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.audit_module import inspect_module


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--release", action="store_true", help="Package a stable public release")
    parser.add_argument("--hardware-report", type=Path)
    parser.add_argument("--runtime-comparison", type=Path)
    args = parser.parse_args()
    build = ROOT / "build/modules/bayer"
    manifest = json.loads((build / "build.json").read_text())
    evidence = args.evidence.resolve()
    report = json.loads(evidence.read_text())
    module = (build / "bayer.mo").read_bytes()
    if inspect_module(module)["module_name"] != "bayer" or manifest["module"] != "bayer.mo":
        raise ValueError("Wrong module identity")
    if sha(module) != manifest["files"]["bayer.mo"] or report["module_sha256"] != sha(module) or report["hardware_test"]:
        raise ValueError("Automated evidence does not identify this module")
    required = {"raw-host-sanitizers", "raw-full-eosm-sanitizers", "arm946-eos", "arm946-eos5",
                "capture-host-sanitizers", "capture-shutdown-sanitizers", "menu-settings-sanitizers",
                "module-arm946-eos", "module-arm946-eos5", "module-corruption-rejected", "capture-only-low-memory-sanitizers"}
    if {run["name"] for run in report["runs"]} != required:
        raise ValueError("Incomplete fixture evidence")
    if not report.get("capture_only") or report.get("display_filter_registered") or report.get("workspace_bytes") != 39600:
        raise ValueError("Expected capture-only evidence")
    for name, expected in {**manifest["source_files"], **report["source_files"]}.items():
        if sha((ROOT / name).read_bytes()) != expected:
            raise ValueError("Source changed after validation: " + name)
    files = {"ML/MODULES/bayer.mo": module, "README.md": (ROOT / "README.md").read_bytes(),
             "COPYING": (ROOT / "COPYING").read_bytes(), "evidence/tests.json": evidence.read_bytes()}
    if args.release:
        if not re.fullmatch(r"\d+\.\d+\.\d+", manifest["version"]) or not args.hardware_report or not args.runtime_comparison:
            raise ValueError("A public release requires a stable version, hardware report and runtime comparison")
        hardware = json.loads(args.hardware_report.read_text())
        comparison = json.loads(args.runtime_comparison.read_text())
        if hardware.get("basis") != "owner_report" or hardware.get("camera") != "EOSM" or hardware.get("canon_firmware") != "2.0.2" or not hardware.get("operation_confirmed"):
            raise ValueError("Missing owner confirmation for the release target")
        if comparison.get("release_module_sha256") != sha(module) or not comparison.get("runtime_source_unchanged_except_version"):
            raise ValueError("Release runtime differs from confirmed development implementation")
        for name, expected in comparison["release_source_files"].items():
            if sha((ROOT / name).read_bytes()) != expected:
                raise ValueError("Runtime comparison source changed: " + name)
        files["evidence/hardware-report.json"] = args.hardware_report.read_bytes()
        files["evidence/runtime-comparison.json"] = args.runtime_comparison.read_bytes()
    elif args.hardware_report or args.runtime_comparison:
        raise ValueError("Hardware/release reports require --release")
    for name, expected in manifest["files"].items():
        data = (build / name).read_bytes()
        if sha(data) != expected:
            raise ValueError("Build artifact changed: " + name)
        if name != "bayer.mo":
            files["evidence/build/" + name] = data
    files["evidence/build/build.json"] = (build / "build.json").read_bytes()
    for run in report["runs"]:
        data = (evidence.parent / run["log"]).read_bytes()
        if sha(data) != run["sha256"]:
            raise ValueError("Test log changed: " + run["name"])
        files["evidence/" + run["log"]] = data
    sources = set(manifest["source_files"]) | set(report["source_files"])
    sources.update({"tools/build_module.py", "tools/audit_module.py", "tools/linux_runtime.py", "tools/package_bayer.py",
                    "tools/ml-builder.Dockerfile", "tests/fx_sdk.h", "tests/mirror_camera_sdk.h", "tests/mirror_trace_sdk.h",
                    "tests/mirror_ui_sdk.h", "tests/mirror_settings_sdk.h", "targets/EOSM.202/EOSM_202.sym",
                    "targets/EOSM.202/PROVENANCE.md", "docs/bayer-drift.md", "COPYING",
                    "README.md", "CHANGELOG.md", "docs/building.md", "docs/validation.md", ".gitignore", ".gitattributes", ".github/workflows/test.yml"})
    for name in sorted(sources):
        files["source/" + name] = (ROOT / name).read_bytes()
    payload = {"module": "Bayer Drift", "version": manifest["version"], "camera_test_required": not args.release,
               "release": args.release, "automated_tests_are_hardware_tests": False,
               "hardware_confirmation": "owner_report" if args.release else None,
               "target": manifest["target"], "module_sha256": sha(module),
               "files": {name: sha(data) for name, data in sorted(files.items())}}
    files["manifest.json"] = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode()
    output = args.output or ROOT / ("build/releases" if args.release else "build/candidates") / ("BAYER-DRIFT-" + manifest["version"] + ("-EOSM202.zip" if args.release else "-EOSM-CANDIDATE.zip"))
    output = output.resolve()
    if output.exists():
        raise FileExistsError("Refusing to replace package: " + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    epoch = datetime.fromtimestamp(manifest["source_date_epoch"], timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            item = zipfile.ZipInfo(name, epoch)
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = 0o100644 << 16
            archive.writestr(item, data)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() or len(archive.namelist()) != len(files):
            raise ValueError("Candidate ZIP verification failed")
        for name, expected in payload["files"].items():
            if sha(archive.read(name)) != expected:
                raise ValueError("Candidate member mismatch: " + name)
        if archive.read("ML/MODULES/bayer.mo") != module:
            raise ValueError("Packaged module changed")
    print(output)
    print("SHA256", sha(output.read_bytes()), "members", len(files))


if __name__ == "__main__":
    main()

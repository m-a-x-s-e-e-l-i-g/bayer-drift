"""Bayer Drift sample, menu, and guarded capture fixtures; no hardware claim."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.linux_runtime import LinuxRuntime
from tools.audit_module import inspect_module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    out = ROOT / "build/bayer-tests" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out.mkdir(parents=True)
    base = out.relative_to(ROOT).as_posix()
    rt = LinuxRuntime(ROOT, "wsl" if os.name == "nt" else "native")
    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
             "-DFX_PREFIX=bayer", "-include", "modules/fx_common/fx_namespace.h"]
    flags += ["-I" + p for p in ("tests", "modules/bayer", "modules/fx_common", "modules/glock", "core")]
    san = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie"]
    sources = ["tests/bayer_raw.c", "modules/bayer/fx_bayer.c", "modules/bayer/bayer_preview.c"]
    runs = []

    def run(name, command):
        text = rt.output(*command)
        if name.startswith("raw-") or name in ("arm946-eos", "arm946-eos5"):
            markers = re.findall(r"^BAYER_RAW=(.+)$", text, re.M)
            expected = 1 if name == "raw-full-eosm-sanitizers" else 864
            if len(markers) != 1 or json.loads(markers[0])["cases"] != expected or json.loads(markers[0])["mismatches"]:
                raise ValueError("Incomplete RAW evidence: " + name)
        (out / (name + ".log")).write_text(text, encoding="utf-8")
        print(name, text.strip(), flush=True)
        runs.append({"name": name, "log": name + ".log"})
        return text

    exe = base + "/raw-host"
    reference = ROOT / "tests/bayer_dev1_reference"
    provenance = json.loads((reference / "provenance.json").read_text())
    for name, expected in provenance["files"].items():
        if hashlib.sha256((reference / name).read_bytes()).hexdigest() != expected:
            raise ValueError("Changed dev1 reference: " + name)
    ref_flags = ["-DFX_PREFIX=reference" if f == "-DFX_PREFIX=bayer" else f for f in flags]
    ref_objects = []
    for name in ("fx_bayer", "bayer_preview"):
        obj = base + "/dev1-" + name + ".o"
        rt.run("gcc", *ref_flags, *san, "-c", "tests/bayer_dev1_reference/" + name + ".c", "-o", obj)
        ref_objects.append(obj)
    rt.run("gcc", *flags, *san, "-DBAYER_DEV1_REFERENCE", *sources, *ref_objects, "-o", exe)
    raw_log = run("raw-host-sanitizers", [exe])
    preview_hash = int(re.search(r"^BAYER_PREVIEW_FNV=(\d+)$", raw_log, re.M)[1])
    if args.full:
        run("raw-full-eosm-sanitizers", [exe, "full"])
    if args.arm:
        elf = base + "/raw-arm.elf"
        rt.run("arm-none-eabi-gcc", *flags, "-march=armv5te", "-marm", "--specs=rdimon.specs", "-Ttext=0x10000", *sources, "-o", elf)
        for cpu in ("arm946-eos", "arm946-eos5"):
            run(cpu, ["env", "QEMU_AUDIO_DRV=none", "timeout", "180", "build/qemu-eos/qemu-system-arm",
                      "-M", "versatilepb", "-cpu", cpu, "-m", "128", "-display", "none", "-monitor", "none",
                      "-serial", "none", "-semihosting-config", "enable=on,target=native", "-kernel", elf, "-no-reboot"])
    common = ["tests/preview_sdk.c", "modules/bayer/bayer_capture_runtime.c", "modules/bayer/fx_bayer.c",
              "modules/bayer/bayer_camera.c", "modules/bayer/bayer_raw_trace.c", "modules/bayer/bayer_capture_gate.c",
              "modules/glock/gl_format.c"]
    capture = base + "/capture-host"
    rt.run("gcc", *flags, *san, "-DFX_HOST_TEST", "-DBAYER_CAMERA_HOST_TEST", "-DFX_GATE_HOST_TEST",
           "-DFX_TRACE_HOST_TEST", "-DFX_BAYER_TEST_HOOK", "-DBAYER_RELEASE", "tests/bayer_capture_only.c", *common, "-o", capture)
    run("capture-host-sanitizers", [capture])
    run("capture-shutdown-sanitizers", [capture, "held-shutdown"])
    menu = base + "/menu-host"
    rt.run("gcc", *flags, *san, "-DFX_BAYER_UI_HOST_TEST", "-DFX_SETTINGS_HOST_TEST", "-DFX_HOST_TEST",
           "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "tests/bayer_menu.c",
           "modules/bayer/bayer_settings.c", "modules/bayer/fx_bayer.c", "modules/glock/gl_format.c", "-o", menu)
    run("menu-settings-sanitizers", [menu])
    staging = base + "/capture-low-memory-host"
    rt.run("gcc", *flags, *san, "-DFX_HOST_TEST", "-DBAYER_CAMERA_HOST_TEST",
           "-DFX_GATE_HOST_TEST", "-DFX_TRACE_HOST_TEST", "-DFX_BAYER_TEST_HOOK", "-DBAYER_RELEASE",
           "-DFX_TEST_FREE_BYTES=1835008", "-DFX_TEST_MAX_REGION_BYTES=65536",
           "tests/bayer_capture_only.c", *common, "-o", staging)
    run("capture-only-low-memory-sanitizers", [staging])
    if args.arm:
        # Execute the built .mo itself, not a recompilation of its source.
        build_manifest = json.loads((ROOT / "build/modules/bayer/build.json").read_text())
        inspected = inspect_module((ROOT / "build/modules/bayer/bayer.mo").read_bytes())
        forbidden = {"get_yuv422_vram", "sync_caches", "zoom_overlay_disable", "zebra_should_run", "should_draw_zoom_overlay"}
        if forbidden.intersection(inspected["imports"]) or "bayer_bayer_preview" in inspected["defined_symbols"]:
            raise ValueError("Capture-only module still links display processing")
        for name, expected in build_manifest["source_files"].items():
            if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
                raise ValueError("Built module source changed: " + name)
        (out / "build").mkdir()
        (out / "build/module_strings.h").write_text("#define MODULE_STRINGS()\n")
        binary_flags = ["-Os", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                        "-march=armv5te", "-marm", "-D__ARM__", "-DMODULE", "-DMODULE_NAME=fixture", "-DCONFIG_MAGICLANTERN", "-DFX_PREFIX=bayer",
                        "-DBAYER_PREVIEW_FNV=" + str(preview_hash) + "u"]
        binary_flags += ["-I" + p for p in (base, ".cache/magiclantern/src", ".cache/magiclantern/include", ".cache/magiclantern/platform/all",
                                          "modules/bayer", "modules/fx_common", "modules/glock", "core")]
        binary = base + "/module-arm.elf"
        rt.run("arm-none-eabi-gcc", *binary_flags, "--specs=rdimon.specs", "-Ttext=0x10000", "tests/bayer_binary.c",
               "build/modules/bayer/bayer.mo", "-o", binary)
        for cpu in ("arm946-eos", "arm946-eos5"):
            log = run("module-" + cpu, ["env", "QEMU_AUDIO_DRV=none", "timeout", "180", "build/qemu-eos/qemu-system-arm",
                      "-M", "versatilepb", "-cpu", cpu, "-m", "128", "-display", "none", "-monitor", "none",
                      "-serial", "none", "-semihosting-config", "enable=on,target=native", "-kernel", binary, "-no-reboot"])
            if "BINARY_MODULE_RESULT api=8 profiles=1 unsupported=1 sensor_reads=0 PASS" not in log:
                raise ValueError("Incomplete built module execution")
        negative = base + "/module-corrupted.elf"
        rt.run("arm-none-eabi-gcc", *binary_flags, "-DBINARY_CORRUPT_OUTPUT", "--specs=rdimon.specs", "-Ttext=0x10000",
               "tests/bayer_binary.c", "build/modules/bayer/bayer.mo", "-o", negative)
        result = subprocess.run(rt.prefix() + ["env", "QEMU_AUDIO_DRV=none", "timeout", "180", "build/qemu-eos/qemu-system-arm",
                      "-M", "versatilepb", "-cpu", "arm946-eos5", "-m", "128", "-display", "none", "-monitor", "none",
                      "-serial", "none", "-semihosting-config", "enable=on,target=native", "-kernel", negative, "-no-reboot"],
                      capture_output=True, text=True)
        if result.returncode != 1 or "BINARY FIXTURE FAIL" not in result.stdout or "BINARY_MODULE_RESULT" in result.stdout:
            raise ValueError("Deliberate corruption was not rejected")
        (out / "module-corruption-rejected.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        runs.append({"name": "module-corruption-rejected", "log": "module-corruption-rejected.log"})
        print("module-corruption-rejected PASS", flush=True)
    paths = list((ROOT / "modules/bayer").rglob("*")) + [ROOT / p for p in sources + common] + [Path(__file__), ROOT / "tests/bayer_menu.c", ROOT / "tests/bayer_capture.c", ROOT / "tests/bayer_binary.c"]
    paths += list(reference.iterdir())
    paths.append(ROOT / "tests/bayer_preview_staging.c")
    paths.append(ROOT / "tests/bayer_capture_only.c")
    hashes = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}
    for sdk in ("tests/fx_sdk.h", "tests/mirror_camera_sdk.h", "tests/mirror_trace_sdk.h", "tests/mirror_ui_sdk.h", "tests/mirror_settings_sdk.h"):
        hashes[sdk] = hashlib.sha256((ROOT / sdk).read_bytes()).hexdigest()
    for run_result in runs:
        run_result["sha256"] = hashlib.sha256((out / run_result["log"]).read_bytes()).hexdigest()
    report = {"kind": "bayer-drift-host-arm-fixtures", "hardware_test": False, "runs": runs, "source_files": hashes,
              "module_sha256": hashlib.sha256((ROOT / "build/modules/bayer/bayer.mo").read_bytes()).hexdigest() if args.arm else None,
              "capture_only": True, "display_filter_registered": False, "workspace_bytes": 39600}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Evidence:", out / "results.json")


if __name__ == "__main__":
    main()

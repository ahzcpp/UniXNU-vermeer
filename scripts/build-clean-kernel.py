#!/usr/bin/env python3
"""Build a fresh vermeer diagnostic kernel; never flash a device.

The output is a baseline, not a complete iPadOS installation. The native
storage, display and input integration still require device observations.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

CORE = "c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--core", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True,
                    help="new directory, outside the source checkout")
    args = ap.parse_args()
    source = Path(__file__).resolve().parents[1]
    core, work = args.core.resolve(), args.work.resolve()
    revision = subprocess.check_output(["git", "-C", str(core), "rev-parse", "HEAD"], text=True).strip()
    if revision != CORE:
        ap.error(f"expected UniXNU {CORE}, got {revision}")
    if sys.platform.startswith("linux"):
        clang = Path(os.environ.get("UNIXNU_TOOLCHAIN", "/opt/unixnu-linux")) / "llvm/bin/clang"
        probe = subprocess.run([str(clang), "-E", "-x", "c", "-"], text=True,
            input="#if __has_builtin(__builtin_xnu_type_summary)\nxnu_builtins_ready\n#endif\n",
            capture_output=True)
        if probe.returncode or "xnu_builtins_ready" not in probe.stdout.splitlines():
            ap.error(f"{clang} lacks the required XNU builtins; install the official toolchain")
    if work == source or source in work.parents:
        ap.error("--work must be outside the device checkout")
    work.mkdir(parents=True, exist_ok=False)
    report = {"core_commit": revision, "device_commit": subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip(),
        "hardware_tested": False, "complete_os_image": False, "steps": []}
    logdir = work / "logs"
    logdir.mkdir()
    device, out = work / "device", work / "out"

    def run(name, command):
        command = [str(x) for x in command]
        print(f"== {name}: {' '.join(command)}", flush=True)
        start = time.time()
        with (logdir / f"{name}.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        report["steps"].append({"name": name, "command": command,
                                "exit_code": result.returncode, "seconds": round(time.time()-start, 1)})
        (work / "build-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
        if result.returncode:
            print((logdir / f"{name}.log").read_text(errors="replace")[-12000:], file=sys.stderr)
            raise SystemExit(result.returncode)

    def build(name, *stages):
        run(name, [sys.executable, core / "unixnu", "build", *stages,
                   "-D", device, "--out", out])

    run("clone-device", ["git", "clone", "--no-hardlinks", source, device])
    profile = device / "scripts/ci-build-profile.py"
    run("first-pass-profile", [sys.executable, profile, "--drop-pmgr", "--drop-gpu"])
    run("configure-first", [sys.executable, core / "unixnu", "defconfig", "vermeer_defconfig",
                             "-D", device, "--out", out])
    build("kernel-first", "kernel")
    target = out / "vermeer/ipados-17.7.11-21H461/vermeer"
    kernel = target / "kernel/kernel.macho"
    candidate = target / "tree/build/native-prelink/native-owner-ufs-cpu-desktop-bootstrap0-candidate.macho"
    candidate.parent.mkdir(parents=True, exist_ok=True)
    candidate.write_bytes(kernel.read_bytes())
    run("restore-stage-profile", ["git", "-C", device, "checkout", "--",
                                   "board.toml", "configs/vermeer_defconfig"])
    run("second-pass-profile", [sys.executable, profile, "--drop-gpu"])
    run("configure-second", [sys.executable, core / "unixnu", "defconfig", "vermeer_defconfig",
                              "-D", device, "--out", out])
    build("kernel-second", "kernel")
    report["kernel"] = {"path": str(kernel), "bytes": kernel.stat().st_size,
                         "sha256": hashlib.sha256(kernel.read_bytes()).hexdigest()}
    report["limitations"] = ["native root storage is disabled in the inherited bring-up defconfig",
                              "GPU is excluded from this diagnostic build",
                              "no Apple OS/SystemOS or Data image is included",
                              "no physical K70 test has been performed"]
    (work / "build-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["kernel"], indent=2))


if __name__ == "__main__":
    main()

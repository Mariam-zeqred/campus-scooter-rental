#!/usr/bin/env python3
"""Run every check that needs no hardware and no AWS account.

    python tools/check.py            # firmware host tests, lint, cloud tests, synth, cost
    python tools/check.py --firmware # also compile the ESP32 firmware with PlatformIO

`make check` calls this script; it exists so the checks also run on Windows,
where `make` is usually missing. Run it with the Python that has
cloud/requirements-dev.txt installed.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def run(title: str, cmd: list[str], cwd: Path = ROOT) -> None:
    print(f"\n== {title}\n   {' '.join(str(c) for c in cmd)}", flush=True)
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"FAILED: {title}")


def host_compiler() -> list[str]:
    """A C++17 compiler: $CXX, g++, clang++, or the one in the `ziglang` pip package."""
    if os.environ.get("CXX"):
        return os.environ["CXX"].split()
    for name in ("g++", "clang++"):
        if shutil.which(name):
            return [name]
    try:
        import ziglang  # noqa: F401
    except ImportError:
        sys.exit("No C++ compiler found. Install g++ or clang++, or run: pip install ziglang")
    return [sys.executable, "-m", "ziglang", "c++"]


def firmware_host_tests() -> None:
    core = ROOT / "firmware" / "lib" / "scooter_core" / "src"
    BUILD.mkdir(exist_ok=True)
    exe = BUILD / ("test_core.exe" if os.name == "nt" else "test_core")
    sources = sorted(str(p) for p in core.glob("*.cpp")) + [str(ROOT / "firmware/test/test_core/test_main.cpp")]
    run("firmware core: compile host tests", host_compiler() + [
        "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{ROOT / 'firmware' / 'include'}", f"-I{core}", f"-I{ROOT / 'tools' / 'unity_shim'}",
        *sources, "-o", str(exe)])
    run("firmware core: run host tests", [str(exe)])


def cloud_checks() -> None:
    cloud = ROOT / "cloud"
    py = sys.executable
    run("cloud: ruff", [py, "-m", "ruff", "check", "."], cloud)
    run("cloud: pytest", [py, "-m", "pytest", "-q"], cloud)
    run("cloud: synthesise the stack (no AWS access)", [py, str(ROOT / "tools" / "plan.py")])
    run("cloud: cost model", [py, "cost.py", "--scooters", "100"], cloud)


def firmware_build() -> None:
    pio = shutil.which("pio") or str(Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe")
    run("firmware: PlatformIO build for esp32dev", [pio, "run", "-e", "esp32dev"], ROOT / "firmware")


if __name__ == "__main__":
    firmware_host_tests()
    cloud_checks()
    if "--firmware" in sys.argv:
        firmware_build()
    print("\nAll checks passed.")

"""Builds webui/panel.wasm: the firmware's display code (firmware/src/app) plus tools/wasm/panel.c, for
the panel preview in the web UI. Needs clang with the wasm32 target and wasm-ld (LLVM).

    python tools/wasm/build.py [--clang PATH] [--out FILE]
"""
import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
APP = ROOT / "firmware" / "src" / "app"
# the same files the PC simulator runs (tools/sim), nothing hardware specific
SOURCES = ["gfx", "effects", "fx_more", "fx_demo", "fx_sim", "fx_finals", "emblem", "modes", "clockface", "clockface2", "clockface3", "tables", "font5x7",
           "settings", "textfx", "anim", "screens"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clang", default=shutil.which("clang") or "clang")
    ap.add_argument("--out", default=str(ROOT / "webui" / "panel.wasm"))
    a = ap.parse_args()
    cmd = [a.clang, "--target=wasm32", "-O2", "-std=gnu11", "-ffreestanding", "-nostdlib",
           "-Wall", "-Wextra", "-Wno-unused-parameter", f"-I{APP}",
           "-Wl,--no-entry", "-Wl,--strip-all", "-Wl,--export-memory", "-Wl,-z,stack-size=65536",
           "-o", a.out, str(ROOT / "tools" / "wasm" / "panel.c"), *[str(APP / f"{s}.c") for s in SOURCES]]
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(r.returncode)
    print(f"{a.out}: {pathlib.Path(a.out).stat().st_size} bytes")


if __name__ == "__main__":
    main()

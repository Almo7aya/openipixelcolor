#!/bin/bash
# Build on Linux / WSL. Needs nds32le-elf-gcc + binutils (tested: gcc 15.2, binutils 2.45.1 from Arch Linux)
# on PATH, or in $NDS32_TOOLCHAIN/usr/bin (default ~/x-tools/nds32le-elf), and Python 3.
set -euo pipefail
cd "$(dirname "$0")"
export PATH="${NDS32_TOOLCHAIN:-$HOME/x-tools/nds32le-elf}/usr/bin:$PATH"
PY=python3
command -v python3 >/dev/null || PY="python.exe"   # fall back to Windows Python via WSL interop
exec make PYTHON="$PY" "$@"

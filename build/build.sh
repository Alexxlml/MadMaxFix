#!/usr/bin/env bash
# Builds MadMaxFix (XInput9_1_0.dll) with Zig as a cross compiler (it ships the MinGW headers).
# Requirements: Python 3 and `pip install ziglang`. Works on Linux, WSL and Git Bash on Windows.
# Usage (from the repository root): ./build/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}; command -v "$PY" >/dev/null || PY=python
MH="third_party/minhook/src/buffer.c third_party/minhook/src/hook.c third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c"
mkdir -p out
# The source includes "minhook/include/MinHook.h"; -Ithird_party resolves it.
$PY -m ziglang cc -target x86_64-windows-gnu -shared -O2 -s -Ithird_party -Ithird_party/minhook/include \
    -o out/XInput9_1_0.dll src/fix.c $MH
rm -f out/*.lib
echo "Done: out/XInput9_1_0.dll"

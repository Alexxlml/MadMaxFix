# Builds MadMaxFix (XInput9_1_0.dll) with Zig on Windows (PowerShell).
# Requirements: Python 3 and `pip install ziglang`.
# Usage (from the repository root): powershell -ExecutionPolicy Bypass -File .\build\build.ps1
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$mh = @("third_party/minhook/src/buffer.c","third_party/minhook/src/hook.c","third_party/minhook/src/trampoline.c","third_party/minhook/src/hde/hde64.c")
New-Item -ItemType Directory -Force -Path out | Out-Null
python -m ziglang cc -target x86_64-windows-gnu -shared -O2 -s -Ithird_party -Ithird_party/minhook/include -o out/XInput9_1_0.dll src/fix.c @mh
Remove-Item out/*.lib -ErrorAction SilentlyContinue
Write-Host "Done: out\XInput9_1_0.dll"

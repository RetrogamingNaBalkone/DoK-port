param(
    [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
    [switch]$CrossChecks,
    [switch]$OriginalEvidence,
    [switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if(-not $RegressionOnly) {
        & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
        if($LASTEXITCODE -ne 0) {throw 'Native checks failed'}
        if($OriginalEvidence) {
            & "$PSScriptRoot/analyze-collision.ps1"
            if($LASTEXITCODE -ne 0) {throw 'Collision Ghidra analysis failed'}
            & $PythonExe scripts/verify_collision.py
            if($LASTEXITCODE -ne 0) {throw 'Original collision instructions differ'}
        }
        foreach($taskScript in @('analyze_runtime.py','pack_gba_assets.py','verify_packing.py')) {
            & $PythonExe "$PSScriptRoot/$taskScript"
            if($LASTEXITCODE -ne 0) {throw "Asset preparation failed: $taskScript"}
        }
        & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g08
        if($LASTEXITCODE -ne 0) {throw 'G08 ROM build failed'}
    }
    & $PythonExe scripts/verify_collision_gba.py --core $CorePath
    if($LASTEXITCODE -ne 0) {throw 'Collision ARM replay failed'}
    & $PythonExe scripts/verify_renderer_gba.py --core $CorePath --build-dir build/g08 --report analysis/g08-renderer-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Renderer regression failed'}
    & $PythonExe scripts/verify_renderer_host.py --arm-dir build/g08 --half-columns --capacity-tool build/native/dok-render-one-face.exe
    if($LASTEXITCODE -ne 0) {throw 'Startup pixel oracle failed'}
    Copy-Item -LiteralPath build/gba/renderer-host-check/verification.json -Destination analysis/g08-host-verification.json
    & $PythonExe scripts/verify_time_gba.py --core $CorePath --build-dir build/g08 --report analysis/g08-time-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Time regression failed'}
    & $PythonExe scripts/verify_render_columns.py --build-dir build/g08 --report analysis/g08-column-verification.json
    if($LASTEXITCODE -ne 0) {throw 'Column regression failed'}
    & $PythonExe scripts/verify_renderer_early.py --core $CorePath --build-dir build/g08 --half-columns --report analysis/g08-camera-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Camera regression failed'}
    & $PythonExe scripts/profile_renderer.py --core $CorePath --build-dir build/g08 --report analysis/g08-startup-profile.json
    if($LASTEXITCODE -ne 0) {throw 'Frame profile failed'}
    & $PythonExe scripts/report_collision.py
    if($LASTEXITCODE -ne 0) {throw 'G08 report failed'}
} finally {Pop-Location}

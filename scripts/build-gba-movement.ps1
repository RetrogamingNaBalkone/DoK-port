param(
    [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
    [switch]$CrossChecks,
    [switch]$OriginalEvidence
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
    & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
    if($LASTEXITCODE -ne 0) {throw 'Native checks failed'}
    if($OriginalEvidence) {
        & "$PSScriptRoot/analyze-movement.ps1"
        if($LASTEXITCODE -ne 0) {throw 'Ghidra movement evidence failed'}
        & $PythonExe scripts/verify_movement.py
        if($LASTEXITCODE -ne 0) {throw 'Original-instruction comparison failed'}
    }
    foreach($taskScript in @('analyze_runtime.py','pack_gba_assets.py','verify_packing.py')) {
        & $PythonExe "$PSScriptRoot/$taskScript"
        if($LASTEXITCODE -ne 0) {throw "Asset preparation failed: $taskScript"}
    }
    & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g07
    if($LASTEXITCODE -ne 0) {throw 'G07 ROM build failed'}
    & $PythonExe scripts/verify_movement_gba.py --core $CorePath
    if($LASTEXITCODE -ne 0) {throw 'ARM movement verification failed'}
    & $PythonExe scripts/verify_renderer_gba.py --core $CorePath --build-dir build/g07 --report analysis/g07-renderer-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Renderer regression failed'}
    & $PythonExe scripts/verify_renderer_host.py --arm-dir build/g07 --half-columns --capacity-tool build/native/dok-render-one-face.exe
    if($LASTEXITCODE -ne 0) {throw 'Startup pixel oracle failed'}
    Copy-Item -LiteralPath build/gba/renderer-host-check/verification.json -Destination analysis/g07-host-verification.json
    & $PythonExe scripts/verify_time_gba.py --core $CorePath --build-dir build/g07 --report analysis/g07-time-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Time regression failed'}
    & $PythonExe scripts/verify_render_columns.py --build-dir build/g07 --report analysis/g07-column-verification.json
    if($LASTEXITCODE -ne 0) {throw 'Linked asset/column verification failed'}
    & $PythonExe scripts/verify_renderer_early.py --core $CorePath --build-dir build/g07 --half-columns --report analysis/g07-camera-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Camera regression failed'}
    & $PythonExe scripts/profile_renderer.py --core $CorePath --build-dir build/g07 --report analysis/g07-startup-profile.json
    if($LASTEXITCODE -ne 0) {throw 'Frame profile failed'}
    & $PythonExe scripts/report_movement.py
    if($LASTEXITCODE -ne 0) {throw 'G07 provenance publication failed'}
} finally {Pop-Location}

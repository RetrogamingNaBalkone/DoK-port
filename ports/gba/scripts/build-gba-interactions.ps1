param(
    [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
    [switch]$CrossChecks,[switch]$OriginalEvidence,[switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../"))
Push-Location (([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../"))))
try {
    if(-not $RegressionOnly) {
        & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
        if($LASTEXITCODE -ne 0) {throw 'Native checks failed'}
        if($OriginalEvidence) {
            & "$taskRoot/ports/gba/tools/ghidra/analyze-interactions.ps1"
            if($LASTEXITCODE -ne 0) {throw 'G09 Ghidra failed'}
            & $PythonExe ports/gba/scripts/verify_interactions.py
            if($LASTEXITCODE -ne 0) {throw 'Original interaction instructions differ'}
        }
        foreach($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
            & $PythonExe "$taskRoot/$taskScript"
            if($LASTEXITCODE -ne 0) {throw "Asset preparation failed: $taskScript"}
        }
        & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g09
        if($LASTEXITCODE -ne 0) {throw 'G09 ROM build failed'}
    }
    & build/gba/host/dok-interactions-scenarios.exe build/gba/assets/assets.bin | Set-Content -Encoding utf8 analysis/gba/g09-level-scenarios.json
    if($LASTEXITCODE -ne 0) {throw 'Real-level interaction scenarios failed'}
    & $PythonExe tools/originals/inventory_interactions.py
    if($LASTEXITCODE -ne 0) {throw 'Original LEVEL inventories failed'}
    & $PythonExe ports/gba/scripts/verify_interactions_gba.py --core $CorePath
    if($LASTEXITCODE -ne 0) {throw 'Interaction ARM scenarios failed'}
    & $PythonExe ports/gba/scripts/verify_renderer_gba.py --core $CorePath --build-dir build/gba/g09 --report analysis/gba/g09-renderer-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Renderer regression failed'}
    & $PythonExe ports/gba/scripts/verify_renderer_host.py --arm-dir build/gba/g09 --half-columns --capacity-tool build/gba/host/dok-render-one-face.exe
    if($LASTEXITCODE -ne 0) {throw 'Startup pixel oracle failed'}
    Copy-Item -LiteralPath build/gba/renderer-host-check/verification.json -Destination analysis/gba/g09-host-verification.json
    & $PythonExe ports/gba/scripts/verify_time_gba.py --core $CorePath --build-dir build/gba/g09 --report analysis/gba/g09-time-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Time regression failed'}
    & $PythonExe ports/gba/scripts/verify_render_columns.py --build-dir build/gba/g09 --report analysis/gba/g09-column-verification.json
    if($LASTEXITCODE -ne 0) {throw 'Column regression failed'}
    & $PythonExe ports/gba/scripts/verify_renderer_early.py --core $CorePath --build-dir build/gba/g09 --half-columns --report analysis/gba/g09-camera-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Camera regression failed'}
    & $PythonExe ports/gba/scripts/profile_renderer.py --core $CorePath --build-dir build/gba/g09 --report analysis/gba/g09-startup-profile.json
    if($LASTEXITCODE -ne 0) {throw 'Frame profile failed'}
    & $PythonExe ports/gba/scripts/report_interactions.py
    if($LASTEXITCODE -ne 0) {throw 'G09 report failed'}
} finally {Pop-Location}

param(
    [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
    [switch]$CrossChecks,[switch]$OriginalEvidence,[switch]$RegressionOnly,
    [int]$CameraStep=1
)
$ErrorActionPreference='Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../"))
Push-Location (([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../"))))
try {
    if(-not $RegressionOnly) {
        & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
        if($LASTEXITCODE -ne 0) {throw 'Native checks failed'}
        foreach($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
            & $PythonExe "$taskRoot/$taskScript"
            if($LASTEXITCODE -ne 0) {throw "Asset preparation failed: $taskScript"}
        }
        & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g10
        if($LASTEXITCODE -ne 0) {throw 'G10 ROM build failed'}
    }
    if($OriginalEvidence) {
        & "$taskRoot/ports/gba/tools/ghidra/analyze-surfaces.ps1"
        if($LASTEXITCODE -ne 0) {throw 'G10 Ghidra failed'}
        & $PythonExe ports/gba/scripts/verify_surfaces.py
        if($LASTEXITCODE -ne 0) {throw 'Original surface instructions differ'}
    }
    & $PythonExe ports/gba/scripts/verify_surfaces_gba.py --core $CorePath
    if($LASTEXITCODE -ne 0) {throw 'Surface ARM scenarios failed'}
    & $PythonExe ports/gba/scripts/verify_interactions_gba.py --core $CorePath --build-dir build/gba/g10 --report analysis/gba/g10-interactions-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Interaction regression failed'}
    & $PythonExe ports/gba/scripts/verify_renderer_gba.py --core $CorePath --build-dir build/gba/g10 --report analysis/gba/g10-renderer-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Renderer regression failed'}
    & $PythonExe ports/gba/scripts/verify_renderer_host.py --arm-dir build/gba/g10 --half-columns --capacity-tool build/gba/host/dok-render-one-face.exe
    if($LASTEXITCODE -ne 0) {throw 'Startup pixel oracle failed'}
    Copy-Item -LiteralPath build/gba/renderer-host-check/verification.json -Destination analysis/gba/g10-host-verification.json
    & $PythonExe ports/gba/scripts/verify_time_gba.py --core $CorePath --build-dir build/gba/g10 --report analysis/gba/g10-time-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Time regression failed'}
    & $PythonExe ports/gba/scripts/verify_render_columns.py --build-dir build/gba/g10 --report analysis/gba/g10-column-verification.json
    if($LASTEXITCODE -ne 0) {throw 'Column regression failed'}
    & $PythonExe ports/gba/scripts/verify_renderer_early.py --core $CorePath --build-dir build/gba/g10 --half-columns --step $CameraStep --report analysis/gba/g10-camera-regression.json
    if($LASTEXITCODE -ne 0) {throw 'Camera regression failed'}
    & $PythonExe ports/gba/scripts/profile_renderer.py --core $CorePath --build-dir build/gba/g10 --report analysis/gba/g10-startup-profile.json
    if($LASTEXITCODE -ne 0) {throw 'Frame profile failed'}
    if($CameraStep -eq 1) {
        & $PythonExe ports/gba/scripts/report_surfaces.py
        if($LASTEXITCODE -ne 0) {throw 'G10 report failed'}
    } else {
        Write-Warning 'Reduced camera sweep passed; final G10 acceptance requires CameraStep 1 (default)'
    }
} finally {Pop-Location}

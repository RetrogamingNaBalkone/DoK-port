param(
    [string]$ZigExe = "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe = 'C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$CorePath = "$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
    [switch]$CrossChecks
)
$ErrorActionPreference = 'Stop'
$taskRoot = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../")))
Push-Location $taskRoot
try {
    & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
    if ($LASTEXITCODE -ne 0) { throw 'Native checks failed' }
    foreach ($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
        & $PythonExe "$taskRoot/$taskScript"
        if ($LASTEXITCODE -ne 0) { throw "Asset preparation failed: $taskScript" }
    }
    & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g06a-fast
    if ($LASTEXITCODE -ne 0) { throw 'GBA build failed' }
    & $PythonExe "$PSScriptRoot/verify_render_columns.py"
    if ($LASTEXITCODE -ne 0) { throw 'Derived columns/linked payload check failed' }
    & $PythonExe "$PSScriptRoot/verify_renderer_gba.py" --core $CorePath --build-dir build/gba/g06a-fast --report analysis/gba/g06a-regression.json
    if ($LASTEXITCODE -ne 0) { throw 'Language/reset/guards check failed' }
    & $PythonExe "$PSScriptRoot/verify_renderer_host.py" --arm-dir build/gba/g06a-fast --half-columns --capacity-tool build/gba/host/dok-render-one-face.exe
    if ($LASTEXITCODE -ne 0) { throw 'Independent startup oracle failed' }
    Copy-Item -LiteralPath build/gba/renderer-host-check/verification.json -Destination analysis/gba/g06a-host-verification.json
    & $PythonExe "$PSScriptRoot/verify_time_gba.py" --core $CorePath --build-dir build/gba/g06a-fast --report analysis/gba/g06a-time-regression.json
    if ($LASTEXITCODE -ne 0) { throw 'Clock/scheduler regression failed' }
    & $PythonExe "$PSScriptRoot/verify_renderer_early.py" --core $CorePath --half-columns
    if ($LASTEXITCODE -ne 0) { throw 'Camera regression failed' }
    & $PythonExe "$PSScriptRoot/profile_renderer.py" --core $CorePath --build-dir build/gba/g06a-fast --report analysis/gba/render-early-startup.json
    if ($LASTEXITCODE -ne 0) { throw 'Startup profile failed' }
    & $PythonExe "$PSScriptRoot/report_renderer_early.py"
    if ($LASTEXITCODE -ne 0) { throw 'G06A evidence publication failed' }
    $taskReport = Get-Content -Raw -Encoding UTF8 analysis/gba/render-early-benchmark.json | ConvertFrom-Json
    if ($taskReport.status -ne 'passed') { throw 'G06A functional acceptance failed' }
    if (-not $taskReport.budget_met) {
        Write-Warning "G06A early target remains unmet: worst $($taskReport.worst.cycles) cycles vs 400000; deferred by user decision 2026-10-25"
    }
} finally { Pop-Location }

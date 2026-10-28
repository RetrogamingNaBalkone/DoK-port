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
    foreach ($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
        & $PythonExe "$taskRoot/$taskScript"
        if ($LASTEXITCODE -ne 0) { throw "Asset preparation/check failed: $taskScript" }
    }
    & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -CorePath $CorePath -AssetsProbe -OutDir build/gba/gba-assets -Verify
} finally { Pop-Location }

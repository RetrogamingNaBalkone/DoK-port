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
  if($LASTEXITCODE -ne 0){throw 'G15 native failed'}
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g15
  if($LASTEXITCODE -ne 0){throw 'G15 GBA build failed'}
 }
 & $PythonExe ports/gba/scripts/verify_ai_combat_host.py
 if($LASTEXITCODE -ne 0){throw 'G15 host failed'}
 if($OriginalEvidence){
  & "$taskRoot/ports/gba/tools/ghidra/analyze-ai.ps1"
  if($LASTEXITCODE -ne 0){throw 'G15 Ghidra failed'}
  & $PythonExe ports/gba/scripts/verify_ai_combat_original.py
  if($LASTEXITCODE -ne 0){throw 'G15 original failed'}
 }
 & $PythonExe ports/gba/scripts/verify_ai_combat_gba.py --core $CorePath --build-dir build/gba/g15
 if($LASTEXITCODE -ne 0){throw 'G15 ARM failed'}
 foreach($taskCheck in @(@('ports/gba/scripts/verify_renderer_gba.py','analysis/gba/g15-renderer-regression.json'),@('ports/gba/scripts/verify_interactions_gba.py','analysis/gba/g15-interactions-regression.json'),@('ports/gba/scripts/verify_time_gba.py','analysis/gba/g15-time-regression.json'))){
  & $PythonExe "$taskRoot/$($taskCheck[0])" --core $CorePath --build-dir build/gba/g15 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0){throw "G15 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe ports/gba/scripts/report_ai_combat.py
 if($LASTEXITCODE -ne 0){throw 'G15 acceptance failed'}
}finally{Pop-Location}

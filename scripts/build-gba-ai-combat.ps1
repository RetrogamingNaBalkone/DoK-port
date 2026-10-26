param(
 [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
 [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
 [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
 [switch]$CrossChecks,[switch]$OriginalEvidence,[switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 if(-not $RegressionOnly) {
  & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
  if($LASTEXITCODE -ne 0){throw 'G15 native failed'}
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g15
  if($LASTEXITCODE -ne 0){throw 'G15 GBA build failed'}
 }
 & $PythonExe scripts/verify_ai_combat_host.py
 if($LASTEXITCODE -ne 0){throw 'G15 host failed'}
 if($OriginalEvidence){
  & "$PSScriptRoot/analyze-ai.ps1"
  if($LASTEXITCODE -ne 0){throw 'G15 Ghidra failed'}
  & $PythonExe scripts/verify_ai_combat_original.py
  if($LASTEXITCODE -ne 0){throw 'G15 original failed'}
 }
 & $PythonExe scripts/verify_ai_combat_gba.py --core $CorePath --build-dir build/g15
 if($LASTEXITCODE -ne 0){throw 'G15 ARM failed'}
 foreach($taskCheck in @(@('verify_renderer_gba.py','analysis/g15-renderer-regression.json'),@('verify_interactions_gba.py','analysis/g15-interactions-regression.json'),@('verify_time_gba.py','analysis/g15-time-regression.json'))){
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g15 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0){throw "G15 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe scripts/report_ai_combat.py
 if($LASTEXITCODE -ne 0){throw 'G15 acceptance failed'}
}finally{Pop-Location}

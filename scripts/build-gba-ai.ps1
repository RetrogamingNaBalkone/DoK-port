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
  if($LASTEXITCODE -ne 0){throw 'G14 native failed'}
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g14
  if($LASTEXITCODE -ne 0){throw 'G14 GBA build failed'}
 }
 & $PythonExe scripts/verify_ai_host.py
 if($LASTEXITCODE -ne 0){throw 'G14 host failed'}
 if($OriginalEvidence){
  & "$PSScriptRoot/analyze-ai.ps1"
  if($LASTEXITCODE -ne 0){throw 'G14 Ghidra failed'}
  & $PythonExe scripts/verify_ai_original.py
  if($LASTEXITCODE -ne 0){throw 'G14 original failed'}
 }
 & $PythonExe scripts/verify_ai_gba.py --core $CorePath --build-dir build/g14
 if($LASTEXITCODE -ne 0){throw 'G14 ARM failed'}
 foreach($taskCheck in @(@('verify_renderer_gba.py','analysis/g14-renderer-regression.json'),@('verify_interactions_gba.py','analysis/g14-interactions-regression.json'),@('verify_time_gba.py','analysis/g14-time-regression.json'))){
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g14 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0){throw "G14 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe scripts/report_ai.py
 if($LASTEXITCODE -ne 0){throw 'G14 acceptance failed'}
}finally{Pop-Location}

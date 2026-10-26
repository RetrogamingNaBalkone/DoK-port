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
  if($LASTEXITCODE -ne 0) {throw 'G11 native build failed'}
  foreach($taskScript in @('analyze_runtime.py','pack_gba_assets.py','verify_packing.py')) {
   & $PythonExe "$PSScriptRoot/$taskScript"
   if($LASTEXITCODE -ne 0) {throw "Asset preparation failed: $taskScript"}
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g11
  if($LASTEXITCODE -ne 0) {throw 'G11 ROM build failed'}
 }
 & ./build/native/dok-visual-probe.exe build/gba/assets/assets.bin | Set-Content -Encoding utf8 analysis/g11-playback-host.json
 if($LASTEXITCODE -ne 0) {throw 'G11 real-resource playback failed'}
 if($OriginalEvidence) {
  & "$PSScriptRoot/analyze-sprites.ps1"
  if($LASTEXITCODE -ne 0) {throw 'G11 Ghidra failed'}
  & $PythonExe scripts/verify_sprites_original.py
  if($LASTEXITCODE -ne 0) {throw 'Original billboard instructions differ'}
 }
 & $PythonExe scripts/verify_sprites_host.py
 if($LASTEXITCODE -ne 0) {throw 'G11 host scene oracle failed'}
 & $PythonExe scripts/verify_sprites_gba.py --core $CorePath
 if($LASTEXITCODE -ne 0) {throw 'G11 ARM sprites/playback failed'}
 foreach($taskCheck in @(
  @('verify_renderer_gba.py','--report','analysis/g11-renderer-regression.json'),
  @('verify_interactions_gba.py','--report','analysis/g11-interactions-regression.json'),
  @('verify_time_gba.py','--report','analysis/g11-time-regression.json'),
  @('verify_renderer_early.py','--half-columns','--report','analysis/g11-camera-regression.json')
 )) {
  $taskScript=$taskCheck[0];$taskArgs=$taskCheck[1..($taskCheck.Length-1)]
  & $PythonExe "scripts/$taskScript" --core $CorePath --build-dir build/g11 @taskArgs
  if($LASTEXITCODE -ne 0) {throw "G11 regression failed: $taskScript"}
 }
 & $PythonExe scripts/report_sprites.py
 if($LASTEXITCODE -ne 0) {throw 'G11 aggregate verification failed'}
} finally {Pop-Location}

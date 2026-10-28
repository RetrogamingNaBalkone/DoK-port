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
  if($LASTEXITCODE -ne 0) {throw 'G11 native build failed'}
  foreach($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
   & $PythonExe "$taskRoot/$taskScript"
   if($LASTEXITCODE -ne 0) {throw "Asset preparation failed: $taskScript"}
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g11
  if($LASTEXITCODE -ne 0) {throw 'G11 ROM build failed'}
 }
 & ./build/gba/host/dok-visual-probe.exe build/gba/assets/assets.bin | Set-Content -Encoding utf8 analysis/gba/g11-playback-host.json
 if($LASTEXITCODE -ne 0) {throw 'G11 real-resource playback failed'}
 if($OriginalEvidence) {
  & "$taskRoot/ports/gba/tools/ghidra/analyze-sprites.ps1"
  if($LASTEXITCODE -ne 0) {throw 'G11 Ghidra failed'}
  & $PythonExe ports/gba/scripts/verify_sprites_original.py
  if($LASTEXITCODE -ne 0) {throw 'Original billboard instructions differ'}
 }
 & $PythonExe ports/gba/scripts/verify_sprites_host.py
 if($LASTEXITCODE -ne 0) {throw 'G11 host scene oracle failed'}
 & $PythonExe ports/gba/scripts/verify_sprites_gba.py --core $CorePath
 if($LASTEXITCODE -ne 0) {throw 'G11 ARM sprites/playback failed'}
 foreach($taskCheck in @(
  @('ports/gba/scripts/verify_renderer_gba.py','--report','analysis/gba/g11-renderer-regression.json'),
  @('ports/gba/scripts/verify_interactions_gba.py','--report','analysis/gba/g11-interactions-regression.json'),
  @('ports/gba/scripts/verify_time_gba.py','--report','analysis/gba/g11-time-regression.json'),
  @('ports/gba/scripts/verify_renderer_early.py','--half-columns','--report','analysis/gba/g11-camera-regression.json')
 )) {
  $taskScript=$taskCheck[0];$taskArgs=$taskCheck[1..($taskCheck.Length-1)]
  & $PythonExe "$taskRoot/$taskScript" --core $CorePath --build-dir build/gba/g11 @taskArgs
  if($LASTEXITCODE -ne 0) {throw "G11 regression failed: $taskScript"}
 }
 & $PythonExe ports/gba/scripts/report_sprites.py
 if($LASTEXITCODE -ne 0) {throw 'G11 aggregate verification failed'}
} finally {Pop-Location}

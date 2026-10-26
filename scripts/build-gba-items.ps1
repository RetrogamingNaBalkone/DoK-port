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
  if($LASTEXITCODE -ne 0) {throw 'G12 native build failed'}
  foreach($taskScript in @('analyze_runtime.py','pack_gba_assets.py','verify_packing.py')) {
   & $PythonExe "$PSScriptRoot/$taskScript"
   if($LASTEXITCODE -ne 0) {throw "G12 asset preparation failed: $taskScript"}
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g12
  if($LASTEXITCODE -ne 0) {throw 'G12 ROM build failed'}
 }
 & $PythonExe scripts/inventory_items.py
 if($LASTEXITCODE -ne 0) {throw 'Original item inventory failed'}
 if($OriginalEvidence) {
  & "$PSScriptRoot/analyze-items.ps1"
  if($LASTEXITCODE -ne 0) {throw 'G12 Ghidra failed'}
  & $PythonExe scripts/verify_items_original.py
  if($LASTEXITCODE -ne 0) {throw 'Original item instructions differ'}
 }
 & $PythonExe scripts/verify_items_gba.py --core $CorePath --build-dir build/g12
 if($LASTEXITCODE -ne 0) {throw 'G12 ARM items failed'}
 foreach($taskCheck in @(
  @('verify_renderer_gba.py','analysis/g12-renderer-regression.json'),
  @('verify_interactions_gba.py','analysis/g12-interactions-regression.json'),
  @('verify_time_gba.py','analysis/g12-time-regression.json')
 )) {
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g12 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0) {throw "G12 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe scripts/report_items.py
 if($LASTEXITCODE -ne 0) {throw 'G12 aggregate verification failed'}
} finally {Pop-Location}

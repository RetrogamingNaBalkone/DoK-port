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
  if($LASTEXITCODE -ne 0) {throw 'G12 native build failed'}
  foreach($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
   & $PythonExe "$taskRoot/$taskScript"
   if($LASTEXITCODE -ne 0) {throw "G12 asset preparation failed: $taskScript"}
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g12
  if($LASTEXITCODE -ne 0) {throw 'G12 ROM build failed'}
 }
 & $PythonExe tools/originals/inventory_items.py
 if($LASTEXITCODE -ne 0) {throw 'Original item inventory failed'}
 if($OriginalEvidence) {
  & "$taskRoot/ports/gba/tools/ghidra/analyze-items.ps1"
  if($LASTEXITCODE -ne 0) {throw 'G12 Ghidra failed'}
  & $PythonExe ports/gba/scripts/verify_items_original.py
  if($LASTEXITCODE -ne 0) {throw 'Original item instructions differ'}
 }
 & $PythonExe ports/gba/scripts/verify_items_gba.py --core $CorePath --build-dir build/gba/g12
 if($LASTEXITCODE -ne 0) {throw 'G12 ARM items failed'}
 foreach($taskCheck in @(
  @('ports/gba/scripts/verify_renderer_gba.py','analysis/gba/g12-renderer-regression.json'),
  @('ports/gba/scripts/verify_interactions_gba.py','analysis/gba/g12-interactions-regression.json'),
  @('ports/gba/scripts/verify_time_gba.py','analysis/gba/g12-time-regression.json')
 )) {
  & $PythonExe "$taskRoot/$($taskCheck[0])" --core $CorePath --build-dir build/gba/g12 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0) {throw "G12 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe ports/gba/scripts/report_items.py
 if($LASTEXITCODE -ne 0) {throw 'G12 aggregate verification failed'}
} finally {Pop-Location}

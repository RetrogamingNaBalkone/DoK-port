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
  if($LASTEXITCODE -ne 0) {throw 'G13 native build failed'}
  foreach($taskScript in @('tools/originals/analyze_runtime.py','ports/gba/scripts/pack_gba_assets.py','ports/gba/scripts/verify_packing.py')) {
   & $PythonExe "$taskRoot/$taskScript"
   if($LASTEXITCODE -ne 0) {throw "G13 asset preparation failed: $taskScript"}
  }
  & ./build/gba/host/dok-combat-tests.exe
  if($LASTEXITCODE -ne 0) {throw 'G13 host combat suite failed'}
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g13
  if($LASTEXITCODE -ne 0) {throw 'G13 ROM build failed'}
 }
 if($CrossChecks) {& $PythonExe ports/gba/scripts/verify_combat_host.py --cross-checks}
 else {& $PythonExe ports/gba/scripts/verify_combat_host.py}
 if($LASTEXITCODE -ne 0) {throw 'G13 host verification failed'}
 if($OriginalEvidence) {
  & "$taskRoot/ports/gba/tools/ghidra/analyze-combat.ps1"
  if($LASTEXITCODE -ne 0) {throw 'G13 Ghidra failed'}
  & $PythonExe ports/gba/scripts/verify_combat_original.py
  if($LASTEXITCODE -ne 0) {throw 'G13 original x86 verification failed'}
 }
 & $PythonExe ports/gba/scripts/verify_combat_gba.py --core $CorePath --build-dir build/gba/g13
 if($LASTEXITCODE -ne 0) {throw 'G13 ARM combat failed'}
 foreach($taskCheck in @(
  @('ports/gba/scripts/verify_renderer_gba.py','analysis/gba/g13-renderer-regression.json'),
  @('ports/gba/scripts/verify_interactions_gba.py','analysis/gba/g13-interactions-regression.json'),
  @('ports/gba/scripts/verify_time_gba.py','analysis/gba/g13-time-regression.json')
 )) {
  & $PythonExe "$taskRoot/$($taskCheck[0])" --core $CorePath --build-dir build/gba/g13 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0) {throw "G13 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe ports/gba/scripts/report_combat.py
 if($LASTEXITCODE -ne 0) {throw 'G13 acceptance failed'}
} finally {Pop-Location}

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
  if($LASTEXITCODE -ne 0) {throw 'G13 native build failed'}
  foreach($taskScript in @('analyze_runtime.py','pack_gba_assets.py','verify_packing.py')) {
   & $PythonExe "$PSScriptRoot/$taskScript"
   if($LASTEXITCODE -ne 0) {throw "G13 asset preparation failed: $taskScript"}
  }
  & ./build/native/dok-combat-tests.exe
  if($LASTEXITCODE -ne 0) {throw 'G13 host combat suite failed'}
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g13
  if($LASTEXITCODE -ne 0) {throw 'G13 ROM build failed'}
 }
 if($CrossChecks) {& $PythonExe scripts/verify_combat_host.py --cross-checks}
 else {& $PythonExe scripts/verify_combat_host.py}
 if($LASTEXITCODE -ne 0) {throw 'G13 host verification failed'}
 if($OriginalEvidence) {
  & "$PSScriptRoot/analyze-combat.ps1"
  if($LASTEXITCODE -ne 0) {throw 'G13 Ghidra failed'}
  & $PythonExe scripts/verify_combat_original.py
  if($LASTEXITCODE -ne 0) {throw 'G13 original x86 verification failed'}
 }
 & $PythonExe scripts/verify_combat_gba.py --core $CorePath --build-dir build/g13
 if($LASTEXITCODE -ne 0) {throw 'G13 ARM combat failed'}
 foreach($taskCheck in @(
  @('verify_renderer_gba.py','analysis/g13-renderer-regression.json'),
  @('verify_interactions_gba.py','analysis/g13-interactions-regression.json'),
  @('verify_time_gba.py','analysis/g13-time-regression.json')
 )) {
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g13 --report $taskCheck[1]
  if($LASTEXITCODE -ne 0) {throw "G13 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe scripts/report_combat.py
 if($LASTEXITCODE -ne 0) {throw 'G13 acceptance failed'}
} finally {Pop-Location}

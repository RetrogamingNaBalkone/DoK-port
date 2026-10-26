param(
 [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
 [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
 [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
 [switch]$CrossChecks,[switch]$OriginalEvidence,[switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 if(-not $RegressionOnly){
  & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks
  if($LASTEXITCODE -ne 0){throw 'G16 native failed'}
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g16
  if($LASTEXITCODE -ne 0){throw 'G16 ROM build failed'}
 }
 & $PythonExe scripts/verify_audio_host.py
 if($LASTEXITCODE -ne 0){throw 'G16 host failed'}
 if($OriginalEvidence){
  & "$PSScriptRoot/analyze-audio.ps1"
  if($LASTEXITCODE -ne 0){throw 'G16 Ghidra failed'}
  & $PythonExe scripts/verify_audio_original.py
  if($LASTEXITCODE -ne 0){throw 'G16 original failed'}
 }
 & $PythonExe scripts/verify_audio_gba.py --core $CorePath --build-dir build/g16 --report analysis/g16-audio-gba.json
 if($LASTEXITCODE -ne 0){throw 'G16 audio capture failed'}
 foreach($taskCheck in @(@('verify_renderer_gba.py','renderer'),@('verify_interactions_gba.py','interactions'),@('verify_time_gba.py','time'),@('verify_ai_combat_gba.py','ai-combat'))){
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g16 --report "analysis/g16-$($taskCheck[1])-regression.json"
  if($LASTEXITCODE -ne 0){throw "G16 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe scripts/report_audio.py
 if($LASTEXITCODE -ne 0){throw 'G16 aggregate failed'}
}finally{Pop-Location}

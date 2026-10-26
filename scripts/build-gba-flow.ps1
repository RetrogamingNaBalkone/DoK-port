param(
 [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
 [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
 [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
 [switch]$CrossChecks,[switch]$OriginalEvidence,[switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
$taskPreviousCache=$env:ZIG_GLOBAL_CACHE_DIR
$taskPreviousFast=$env:DOK_PROBE_FAST_VIDEO
try {
 $env:ZIG_GLOBAL_CACHE_DIR=(Join-Path (Get-Location) 'build/zig-cache')
 $env:DOK_PROBE_FAST_VIDEO='1'
 if(-not $RegressionOnly){
  New-Item -ItemType Directory -Force build/native | Out-Null
  $taskArgs=@('cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','include')
  foreach($taskModule in @('flow','ui')){
   & $ZigExe @taskArgs "src/$taskModule.c" "tests/test_$taskModule.c" -o "build/native/dok-$taskModule-tests.exe"
   if($LASTEXITCODE -ne 0){throw "G18 host compile failed: $taskModule"}
   if($CrossChecks){
    & $ZigExe @taskArgs -target x86_64-linux-musl -static "src/$taskModule.c" "tests/test_$taskModule.c" -o "build/native/dok-$taskModule-tests-linux"
    if($LASTEXITCODE -ne 0){throw "G18 Linux compile failed: $taskModule"}
    & $ZigExe @taskArgs -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -c "src/$taskModule.c" -o "build/native/$taskModule-arm7tdmi.o"
    if($LASTEXITCODE -ne 0){throw "G18 Thumb compile failed: $taskModule"}
   }
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g18
  if($LASTEXITCODE -ne 0){throw 'G18 ROM build failed'}
 }
 $taskHostArgs=@('scripts/verify_flow_host.py');if($CrossChecks){$taskHostArgs+='--cross-checks'}
 & $PythonExe @taskHostArgs
 if($LASTEXITCODE -ne 0){throw 'G18 host failed'}
 if($OriginalEvidence){
  & "$PSScriptRoot/analyze-flow.ps1"
  if($LASTEXITCODE -ne 0){throw 'G18 Ghidra failed'}
  & $PythonExe scripts/verify_flow_original.py
  if($LASTEXITCODE -ne 0){throw 'G18 original x86 failed'}
 }
 & $PythonExe scripts/audit_flow_exits.py
 if($LASTEXITCODE -ne 0){throw 'G18 original RES audit failed'}
 & $PythonExe scripts/verify_flow_gba.py --core $CorePath --build-dir build/g18 --report analysis/g18-flow-gba.json
 if($LASTEXITCODE -ne 0){throw 'G18 ARM flow failed'}
 foreach($taskCheck in @(@('verify_ui_gba.py','ui'),@('verify_renderer_gba.py','renderer'),@('verify_interactions_gba.py','interactions'),@('verify_time_gba.py','time'),@('verify_ai_combat_gba.py','ai-combat'),@('verify_audio_gba.py','audio'))){
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g18 --report "analysis/g18-$($taskCheck[1])-regression.json"
  if($LASTEXITCODE -ne 0){throw "G18 regression failed: $($taskCheck[0])"}
 }
 # Requires root's visual inspection record for the exact final PNG hashes.
 & $PythonExe scripts/report_flow.py
 if($LASTEXITCODE -ne 0){throw 'G18 aggregate failed (check fresh visual review)'}
}finally{
 $env:ZIG_GLOBAL_CACHE_DIR=$taskPreviousCache
 $env:DOK_PROBE_FAST_VIDEO=$taskPreviousFast
 Pop-Location
}

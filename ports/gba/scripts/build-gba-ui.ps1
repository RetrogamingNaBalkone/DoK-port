param(
 [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
 [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
 [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
 [switch]$CrossChecks,[switch]$OriginalEvidence,[switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../"))
Push-Location (([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../"))))
$taskPreviousCache=$env:ZIG_GLOBAL_CACHE_DIR
$taskPreviousFast=$env:DOK_PROBE_FAST_VIDEO
try {
 $env:ZIG_GLOBAL_CACHE_DIR=(Join-Path (Get-Location) 'build/gba/zig-cache')
 $env:DOK_PROBE_FAST_VIDEO='1'
 if(-not $RegressionOnly){
  New-Item -ItemType Directory -Force build/gba/host | Out-Null
  $taskArgs=@('cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','ports/gba/include')
  & $ZigExe @taskArgs ports/gba/src/ui.c ports/gba/tests/test_ui.c -o build/gba/host/dok-ui-tests.exe
  if($LASTEXITCODE -ne 0){throw 'G17 host compile failed'}
  if($CrossChecks){
   & $ZigExe @taskArgs -target x86_64-linux-musl -static ports/gba/src/ui.c ports/gba/tests/test_ui.c -o build/gba/host/dok-ui-tests-linux
   if($LASTEXITCODE -ne 0){throw 'G17 Linux compile failed'}
   & $ZigExe @taskArgs -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -c ports/gba/src/ui.c -o build/gba/host/ui-arm7tdmi.o
   if($LASTEXITCODE -ne 0){throw 'G17 Thumb compile failed'}
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/gba/g17
  if($LASTEXITCODE -ne 0){throw 'G17 ROM build failed'}
 }
 $taskHostArgs=@('ports/gba/scripts/verify_ui_host.py');if($CrossChecks){$taskHostArgs+='--cross-checks'}
 & $PythonExe @taskHostArgs
 if($LASTEXITCODE -ne 0){throw 'G17 host failed'}
 if($OriginalEvidence){
  & "$taskRoot/ports/gba/tools/ghidra/analyze-ui.ps1"
  if($LASTEXITCODE -ne 0){throw 'G17 Ghidra failed'}
  & $PythonExe ports/gba/scripts/verify_ui_original.py
  if($LASTEXITCODE -ne 0){throw 'G17 original x86 failed'}
 }
 & $PythonExe ports/gba/scripts/verify_ui_gba.py --core $CorePath --build-dir build/gba/g17 --report analysis/gba/g17-ui-gba.json
 if($LASTEXITCODE -ne 0){throw 'G17 ARM UI failed'}
 foreach($taskCheck in @(@('ports/gba/scripts/verify_renderer_gba.py','renderer'),@('ports/gba/scripts/verify_interactions_gba.py','interactions'),@('ports/gba/scripts/verify_time_gba.py','time'),@('ports/gba/scripts/verify_ai_combat_gba.py','ai-combat'),@('ports/gba/scripts/verify_audio_gba.py','audio'))){
  & $PythonExe "$taskRoot/$($taskCheck[0])" --core $CorePath --build-dir build/gba/g17 --report "analysis/gba/g17-$($taskCheck[1])-regression.json"
  if($LASTEXITCODE -ne 0){throw "G17 regression failed: $($taskCheck[0])"}
 }
 # The visual review is a recorded human inspection of the exact PNG hashes;
 # aggregation refuses stale review after a ROM or screenshot change.
 & $PythonExe ports/gba/scripts/report_ui.py
 if($LASTEXITCODE -ne 0){throw 'G17 aggregate failed'}
}finally{
 $env:ZIG_GLOBAL_CACHE_DIR=$taskPreviousCache
 $env:DOK_PROBE_FAST_VIDEO=$taskPreviousFast
 Pop-Location
}

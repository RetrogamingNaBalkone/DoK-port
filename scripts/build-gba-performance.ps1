param(
 [string]$ZigExe="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
 [string]$PythonExe='C:/Source/wasdify/.venv/Scripts/python.exe',
 [string]$CorePath="$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll",
 [switch]$CrossChecks,[switch]$RegressionOnly
)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
$taskPreviousCache=$env:ZIG_GLOBAL_CACHE_DIR
$taskPreviousFast=$env:DOK_PROBE_FAST_VIDEO
try {
 $env:ZIG_GLOBAL_CACHE_DIR=(Join-Path (Get-Location) 'build/zig-cache')
 $env:DOK_PROBE_FAST_VIDEO='1'
 New-Item -ItemType Directory -Force build/g20 | Out-Null
 & $PythonExe -c "import json,hashlib;from pathlib import Path;d={p.as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(Path('originals').rglob('*')) if p.is_file()};Path('build/g20/originals-before.json').write_text(json.dumps(d,indent=2)+'\n',encoding='utf-8')"
 if($LASTEXITCODE -ne 0){throw 'G20 originals snapshot failed'}
 if(-not $RegressionOnly){
  & "$PSScriptRoot/build-native.ps1" -ZigExe $ZigExe -CrossChecks:$CrossChecks *> build/g20/host-regression.log
  if($LASTEXITCODE -ne 0){throw 'G20 native regression failed; see build/g20/host-regression.log'}
  $taskArgs=@('cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','include')
  foreach($taskModule in @('flow','ui')){
   & $ZigExe @taskArgs "src/$taskModule.c" "tests/test_$taskModule.c" -o "build/native/dok-$taskModule-tests.exe"
   if($LASTEXITCODE -ne 0){throw "G20 host compile failed: $taskModule"}
   if($CrossChecks){
    & $ZigExe @taskArgs -target x86_64-linux-musl -static "src/$taskModule.c" "tests/test_$taskModule.c" -o "build/native/dok-$taskModule-tests-linux"
    if($LASTEXITCODE -ne 0){throw "G20 Linux compile failed: $taskModule"}
    & $ZigExe @taskArgs -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -c "src/$taskModule.c" -o "build/native/$taskModule-arm7tdmi.o"
    if($LASTEXITCODE -ne 0){throw "G20 Thumb compile failed: $taskModule"}
   }
  }
  & "$PSScriptRoot/build-gba.ps1" -ZigExe $ZigExe -PythonExe $PythonExe -RendererProbe -HalfColumns -OutDir build/g20
  if($LASTEXITCODE -ne 0){throw 'G20 ROM build failed'}
 }
 & build/native/dok-ui-tests.exe > build/g20/host-ui-flow.log
 if($LASTEXITCODE -ne 0){throw 'G20 host UI failed'}
 & build/native/dok-flow-tests.exe >> build/g20/host-ui-flow.log
 if($LASTEXITCODE -ne 0){throw 'G20 host flow failed'}
 & $PythonExe scripts/verify_performance_arm.py
 if($LASTEXITCODE -ne 0){throw 'G20 linked ARM vectors failed'}
 & $PythonExe scripts/verify_performance_gba.py --core $CorePath --step 5
 if($LASTEXITCODE -ne 0){throw 'G20 paired cameras failed (requires frozen G18 reference)'}
 & $PythonExe scripts/profile_game_gba.py --core $CorePath
 if($LASTEXITCODE -ne 0){throw 'G20 integrated scenarios failed'}
 foreach($taskCheck in @(@('verify_flow_gba.py','flow'),@('verify_ui_gba.py','ui'),@('verify_renderer_gba.py','renderer'),@('verify_sprites_gba.py','sprites'),@('verify_interactions_gba.py','interactions'),@('verify_time_gba.py','time'),@('verify_ai_combat_gba.py','ai-combat'),@('verify_audio_gba.py','audio'))){
  & $PythonExe "scripts/$($taskCheck[0])" --core $CorePath --build-dir build/g20 --report "analysis/g20-$($taskCheck[1])-regression.json"
  if($LASTEXITCODE -ne 0){throw "G20 regression failed: $($taskCheck[0])"}
 }
 & $PythonExe scripts/profile_renderer.py --build-dir build/g20 --report analysis/g20-startup-profile.json
 if($LASTEXITCODE -ne 0){throw 'G20 startup stage profile failed'}
 & $PythonExe scripts/report_performance.py
 if($LASTEXITCODE -ne 0){throw 'G20 aggregate failed'}
}finally{
 $env:ZIG_GLOBAL_CACHE_DIR=$taskPreviousCache
 $env:DOK_PROBE_FAST_VIDEO=$taskPreviousFast
 Pop-Location
}

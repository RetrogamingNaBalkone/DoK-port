param(
    [ValidateSet('zig','msvc')][string]$Compiler = 'zig',
    [switch]$CrossChecks,
    [string]$ZigExe = "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$VsDevCmd = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force "$taskRoot/build/native" | Out-Null
if ($Compiler -eq 'zig') {
    if (-not (Test-Path -LiteralPath $ZigExe)) { throw "Shared C compiler not found: $ZigExe" }
    Push-Location $taskRoot
    try {
        $taskCommon = @('cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','include')
        & $ZigExe @taskCommon src/res.c tools/res_tool.c -o build/native/dok-res.exe
        if ($LASTEXITCODE -ne 0) { throw 'Resource tool build failed' }
        & $ZigExe @taskCommon src/res.c tests/test_res.c -o build/native/dok-res-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Resource tests build failed' }
        & $ZigExe @taskCommon src/native_state.c tests/test_native_state.c -o build/native/dok-native-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native state tests build failed' }
        & $ZigExe @taskCommon src/image.c src/animation.c tools/animation_tool.c -o build/native/dok-anim.exe
        if ($LASTEXITCODE -ne 0) { throw 'Animation tool build failed' }
        & $ZigExe @taskCommon src/image.c src/animation.c tests/test_animation.c -o build/native/dok-animation-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Animation tests build failed' }
        & $ZigExe @taskCommon src/sound.c tools/sound_tool.c -o build/native/dok-sound.exe
        if ($LASTEXITCODE -ne 0) { throw 'Sound tool build failed' }
        & $ZigExe @taskCommon src/sound.c tests/test_sound.c -o build/native/dok-sound-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Sound tests build failed' }
        & $ZigExe @taskCommon src/level.c tools/level_tool.c -o build/native/dok-level.exe
        if ($LASTEXITCODE -ne 0) { throw 'Level tool build failed' }
        & $ZigExe @taskCommon src/level.c tests/test_level.c -o build/native/dok-level-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Level tests build failed' }
        & $ZigExe @taskCommon src/res.c src/image.c src/animation.c src/level.c src/world.c tools/world_tool.c -o build/native/dok-world.exe
        if ($LASTEXITCODE -ne 0) { throw 'World tool build failed' }
        & $ZigExe @taskCommon src/res.c src/image.c src/animation.c src/level.c src/world.c src/native_state.c tests/test_world.c -o build/native/dok-world-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'World tests build failed' }
        & ./build/native/dok-world-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'World tests failed' }
        & ./build/native/dok-level-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Level tests failed' }
        & ./build/native/dok-res-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Resource tests failed' }
        & ./build/native/dok-native-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native state tests failed' }
        & ./build/native/dok-animation-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Animation tests failed' }
        & ./build/native/dok-sound-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Sound tests failed' }
        if ($CrossChecks) {
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/res.c tools/res_tool.c -o build/native/dok-res-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux resource tool build failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/native_state.c tests/test_native_state.c -o build/native/dok-native-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux state tests build failed' }
            & $ZigExe cc -target arm-freestanding-eabi -mcpu=arm7tdmi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/native_state.c -o build/native/native-state-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM7TDMI compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/image.c src/animation.c tools/animation_tool.c -o build/native/dok-anim-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux animation tool build failed' }
            & $ZigExe cc -target arm-freestanding-eabi -mcpu=arm7tdmi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/image.c -o build/native/image-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM image compile failed' }
            & $ZigExe cc -target arm-freestanding-eabi -mcpu=arm7tdmi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/animation.c -o build/native/animation-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM animation compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/sound.c tools/sound_tool.c -o build/native/dok-sound-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux sound tool build failed' }
            & $ZigExe cc -target arm-freestanding-eabi -mcpu=arm7tdmi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/sound.c -o build/native/sound-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM sound compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/level.c tools/level_tool.c -o build/native/dok-level-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux level tool build failed' }
            & $ZigExe cc -target arm-freestanding-eabi -mcpu=arm7tdmi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/level.c -o build/native/level-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM level compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/res.c src/image.c src/animation.c src/level.c src/world.c tools/world_tool.c -o build/native/dok-world-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux world tool build failed' }
            & $ZigExe cc -target arm-freestanding-eabi -mcpu=arm7tdmi -mthumb -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/world.c -o build/native/world-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM world compile failed' }
            Write-Output 'Linux static binaries and ARM7TDMI Thumb objects compiled; cross-target binaries were not executed.'
        }
    } finally { Pop-Location }
    return
}
$taskBatch = @'
@echo off
call "%~1" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~2"
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/res.c tools/res_tool.c /Fo:build/native/ /Fe:build/native/dok-res.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/res.c tests/test_res.c /Fo:build/native/ /Fe:build/native/dok-res-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/native_state.c tests/test_native_state.c /Fo:build/native/ /Fe:build/native/dok-native-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/image.c src/animation.c tools/animation_tool.c /Fo:build/native/ /Fe:build/native/dok-anim.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/image.c src/animation.c tests/test_animation.c /Fo:build/native/ /Fe:build/native/dok-animation-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/sound.c tools/sound_tool.c /Fo:build/native/ /Fe:build/native/dok-sound.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/sound.c tests/test_sound.c /Fo:build/native/ /Fe:build/native/dok-sound-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/level.c tools/level_tool.c /Fo:build/native/ /Fe:build/native/dok-level.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/level.c tests/test_level.c /Fo:build/native/ /Fe:build/native/dok-level-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/res.c src/image.c src/animation.c src/level.c src/world.c tools/world_tool.c /Fo:build/native/ /Fe:build/native/dok-world.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/res.c src/image.c src/animation.c src/level.c src/world.c src/native_state.c tests/test_world.c /Fo:build/native/ /Fe:build/native/dok-world-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-world-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-level-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-res-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-native-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-animation-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-sound-tests.exe
exit /b %errorlevel%
'@
Set-Content -LiteralPath "$taskRoot/build/native/build.cmd" -Value $taskBatch -Encoding ASCII
Push-Location $taskRoot
try { & $env:ComSpec /d /c build\native\build.cmd $VsDevCmd $taskRoot }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "Native build or tests failed: $LASTEXITCODE" }

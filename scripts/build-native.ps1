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
    $taskPreviousCache = $env:ZIG_GLOBAL_CACHE_DIR
    try {
        $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskRoot 'build/zig-cache'
        $taskCommon = @('cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','include')
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c src/image.c src/animation.c src/scheduler.c src/visual.c tools/visual_probe.c -o build/native/dok-visual-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Visual probe build failed' }
        & $ZigExe @taskCommon src/interactions.c src/scheduler.c src/movement.c src/collision.c tests/interactions_cases.c tests/test_interactions.c -o build/native/dok-interactions-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions suite build failed' }
        & ./build/native/dok-interactions-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions suite failed' }
        & $ZigExe @taskCommon src/interactions.c src/scheduler.c src/movement.c src/collision.c tools/interactions_probe.c -o build/native/dok-interactions-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c src/interactions.c src/scheduler.c src/movement.c src/collision.c tools/interactions_scenarios.c -o build/native/dok-interactions-scenarios.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions scenarios build failed' }
        & $ZigExe @taskCommon src/movement.c src/collision.c tests/collision_cases.c tests/test_collision.c -o build/native/dok-collision-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision suite build failed' }
        & ./build/native/dok-collision-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision suite failed' }
        & $ZigExe @taskCommon src/movement.c src/collision.c tools/collision_probe.c -o build/native/dok-collision-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c src/movement.c src/collision.c tools/collision_replay.c -o build/native/dok-collision-replay.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision replay build failed' }
        & $ZigExe @taskCommon src/movement.c tests/movement_cases.c tests/test_movement.c -o build/native/dok-movement-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Movement tests build failed' }
        & ./build/native/dok-movement-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Movement tests failed' }
        & $ZigExe @taskCommon src/movement.c tools/movement_probe.c -o build/native/dok-movement-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Movement probe build failed' }
        & $ZigExe @taskCommon src/scheduler.c src/clock.c tests/time_cases.c tests/test_time.c -o build/native/dok-time-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Time/scheduler build failed' }
        & ./build/native/dok-time-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Time/scheduler tests failed' }
        & $ZigExe @taskCommon src/projection.c tools/projection_probe.c -o build/native/dok-projection-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Projection probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c src/image.c src/animation.c src/projection.c src/renderer.c tools/render_tool.c -o build/native/dok-render.exe
        if ($LASTEXITCODE -ne 0) { throw 'Static renderer tool build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY -DDOK_RENDER_REFERENCE src/runtime.c src/rom_assets.c src/image.c src/animation.c src/projection.c src/renderer.c tools/render_tool.c -o build/native/dok-render-reference.exe
        if ($LASTEXITCODE -ne 0) { throw 'Reference renderer build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY -DDOK_RENDER_FACE_CAPACITY=1 src/runtime.c src/rom_assets.c src/image.c src/animation.c src/projection.c src/renderer.c tools/render_tool.c -o build/native/dok-render-one-face.exe
        if ($LASTEXITCODE -ne 0) { throw 'Renderer capacity probe build failed' }
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
        & $ZigExe @taskCommon src/res.c src/image.c src/animation.c src/level.c src/world.c src/runtime.c tools/runtime_tool.c -o build/native/dok-runtime.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime tool build failed' }
        & $ZigExe @taskCommon src/res.c src/image.c src/animation.c src/level.c src/world.c src/runtime.c tests/test_runtime.c -o build/native/dok-runtime-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime tests build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c tools/rom_assets_tool.c -o build/native/dok-rom-assets.exe
        if ($LASTEXITCODE -ne 0) { throw 'ROM assets tool build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c tests/test_rom_assets.c -o build/native/dok-rom-assets-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'ROM assets tests build failed' }
        & $ZigExe @taskCommon gba/memory.c tests/test_gba_helpers.c -o build/native/dok-gba-helper-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'GBA helper tests build failed' }
        & ./build/native/dok-rom-assets-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'ROM assets tests failed' }
        & ./build/native/dok-gba-helper-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'GBA helper tests failed' }
        & ./build/native/dok-runtime-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime tests failed' }
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
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/interactions.c src/scheduler.c src/movement.c src/collision.c tests/interactions_cases.c tests/test_interactions.c -o build/native/dok-interactions-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux interactions build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/interactions.c -o build/native/interactions-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb interactions compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/movement.c src/collision.c tests/collision_cases.c tests/test_collision.c -o build/native/dok-collision-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux collision suite build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/collision.c -o build/native/collision-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb collision compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/movement.c tests/movement_cases.c tests/test_movement.c -o build/native/dok-movement-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux movement tests build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/movement.c -o build/native/movement-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM movement compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/scheduler.c src/clock.c tests/time_cases.c tests/test_time.c -o build/native/dok-time-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux time tests build failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/res.c tools/res_tool.c -o build/native/dok-res-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux resource tool build failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/native_state.c tests/test_native_state.c -o build/native/dok-native-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux state tests build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/native_state.c -o build/native/native-state-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM7TDMI compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/image.c src/animation.c tools/animation_tool.c -o build/native/dok-anim-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux animation tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/image.c -o build/native/image-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM image compile failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/animation.c -o build/native/animation-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM animation compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/sound.c tools/sound_tool.c -o build/native/dok-sound-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux sound tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/sound.c -o build/native/sound-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM sound compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/level.c tools/level_tool.c -o build/native/dok-level-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux level tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/level.c -o build/native/level-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM level compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/res.c src/image.c src/animation.c src/level.c src/world.c tools/world_tool.c -o build/native/dok-world-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux world tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/world.c -o build/native/world-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM world compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include src/res.c src/image.c src/animation.c src/level.c src/world.c src/runtime.c tools/runtime_tool.c -o build/native/dok-runtime-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux runtime tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/runtime.c -o build/native/runtime-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM runtime compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c tools/rom_assets_tool.c -o build/native/dok-rom-assets-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux ROM assets tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/rom_assets.c -o build/native/rom-assets-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM ROM reader compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include -DDOK_RUNTIME_ROM_ONLY src/runtime.c src/rom_assets.c src/image.c src/animation.c src/projection.c src/renderer.c tools/render_tool.c -o build/native/dok-render-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux static renderer build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c src/renderer.c -o build/native/renderer-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM renderer compile failed' }
            Write-Output 'Linux static binaries and ARM7TDMI Thumb objects compiled; cross-target binaries were not executed.'
        }
    } finally {
        $env:ZIG_GLOBAL_CACHE_DIR = $taskPreviousCache
        Pop-Location
    }
    return
}
$taskBatch = @'
@echo off
call "%~1" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~2"
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/interactions.c src/scheduler.c src/movement.c src/collision.c tests/interactions_cases.c tests/test_interactions.c /Fo:build/native/ /Fe:build/native/dok-interactions-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-interactions-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/interactions.c src/scheduler.c src/movement.c src/collision.c tools/interactions_probe.c /Fo:build/native/ /Fe:build/native/dok-interactions-probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /I include src/runtime.c src/rom_assets.c src/interactions.c src/scheduler.c src/movement.c src/collision.c tools/interactions_scenarios.c /Fo:build/native/ /Fe:build/native/dok-interactions-scenarios.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/movement.c tests/movement_cases.c tests/test_movement.c /Fo:build/native/ /Fe:build/native/dok-movement-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-movement-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/movement.c tools/movement_probe.c /Fo:build/native/ /Fe:build/native/dok-movement-probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /I include src/runtime.c src/rom_assets.c src/image.c src/animation.c src/projection.c src/renderer.c tools/render_tool.c /Fo:build/native/ /Fe:build/native/dok-render.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /DDOK_RENDER_FACE_CAPACITY=1 /I include src/runtime.c src/rom_assets.c src/image.c src/animation.c src/projection.c src/renderer.c tools/render_tool.c /Fo:build/native/ /Fe:build/native/dok-render-one-face.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/projection.c tools/projection_probe.c /Fo:build/native/ /Fe:build/native/dok-projection-probe.exe
if errorlevel 1 exit /b 1
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
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include src/res.c src/image.c src/animation.c src/level.c src/world.c src/runtime.c tools/runtime_tool.c /Fo:build/native/ /Fe:build/native/dok-runtime.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include src/res.c src/image.c src/animation.c src/level.c src/world.c src/runtime.c tests/test_runtime.c /Fo:build/native/ /Fe:build/native/dok-runtime-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /I include src/runtime.c src/rom_assets.c tools/rom_assets_tool.c /Fo:build/native/ /Fe:build/native/dok-rom-assets.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /DDOK_RUNTIME_ROM_ONLY /I include src/runtime.c src/rom_assets.c tests/test_rom_assets.c /Fo:build/native/ /Fe:build/native/dok-rom-assets-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include gba/memory.c tests/test_gba_helpers.c /Fo:build/native/ /Fe:build/native/dok-gba-helper-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-rom-assets-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-gba-helper-tests.exe
if errorlevel 1 exit /b 1
build\native\dok-runtime-tests.exe
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

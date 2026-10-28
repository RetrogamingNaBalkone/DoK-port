param(
    [ValidateSet('zig','msvc')][string]$Compiler = 'zig',
    [switch]$CrossChecks,
    [string]$ZigExe = "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$VsDevCmd = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
$taskRoot = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../")))
New-Item -ItemType Directory -Force "$taskRoot/build/gba/host" | Out-Null
if ($Compiler -eq 'zig') {
    if (-not (Test-Path -LiteralPath $ZigExe)) { throw "Shared C compiler not found: $ZigExe" }
    Push-Location $taskRoot
    $taskPreviousCache = $env:ZIG_GLOBAL_CACHE_DIR
    try {
        $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskRoot 'build/gba/zig-cache'
        $taskCommon = @('cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','ports/gba/include')
        & $ZigExe @taskCommon ports/gba/src/audio.c ports/gba/tests/test_audio.c -o build/gba/host/dok-audio-tests.exe
        if($LASTEXITCODE -ne 0){throw 'G16 host build failed'}
        & ./build/gba/host/dok-audio-tests.exe
        if($LASTEXITCODE -ne 0){throw 'G16 host failed'}
        & $ZigExe @taskCommon ports/gba/src/ai.c ports/gba/src/combat.c ports/gba/src/items.c ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/src/scheduler.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/res.c ports/gba/tests/test_ai.c -o build/gba/host/dok-ai-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'AI suite build failed' }
        & $ZigExe @taskCommon ports/gba/src/ai.c ports/gba/src/combat.c ports/gba/src/items.c ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/src/scheduler.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/res.c ports/gba/tests/test_ai_combat.c -o build/gba/host/dok-ai-combat-tests.exe
        if($LASTEXITCODE -ne 0){throw 'G15 host build failed'}
        & ./build/gba/host/dok-ai-combat-tests.exe
        if($LASTEXITCODE -ne 0){throw 'G15 host failed'}
        & ./build/gba/host/dok-ai-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'AI suite failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/combat.c ports/gba/src/items.c ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/test_combat.c -o build/gba/host/dok-combat-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Combat suite build failed' }
        if(Test-Path 'build/gba/assets/assets.bin') {
            & ./build/gba/host/dok-combat-tests.exe
            if ($LASTEXITCODE -ne 0) { throw 'Combat suite failed' }
        }
        & $ZigExe @taskCommon ports/gba/src/items.c ports/gba/tests/test_items.c -o build/gba/host/dok-items-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Items tests build failed' }
        & ./build/gba/host/dok-items-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Items tests failed' }
        & $ZigExe @taskCommon ports/gba/src/items.c ports/gba/tools/items_probe.c -o build/gba/host/dok-items-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Items probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/scheduler.c ports/gba/src/visual.c ports/gba/tools/visual_probe.c -o build/gba/host/dok-visual-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Visual probe build failed' }
        & $ZigExe @taskCommon ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/interactions_cases.c ports/gba/tests/test_interactions.c -o build/gba/host/dok-interactions-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions suite build failed' }
        & ./build/gba/host/dok-interactions-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions suite failed' }
        & $ZigExe @taskCommon ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tools/interactions_probe.c -o build/gba/host/dok-interactions-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tools/interactions_scenarios.c -o build/gba/host/dok-interactions-scenarios.exe
        if ($LASTEXITCODE -ne 0) { throw 'Interactions scenarios build failed' }
        & $ZigExe @taskCommon ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/collision_cases.c ports/gba/tests/test_collision.c -o build/gba/host/dok-collision-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision suite build failed' }
        & ./build/gba/host/dok-collision-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision suite failed' }
        & $ZigExe @taskCommon ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tools/collision_probe.c -o build/gba/host/dok-collision-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tools/collision_replay.c -o build/gba/host/dok-collision-replay.exe
        if ($LASTEXITCODE -ne 0) { throw 'Collision replay build failed' }
        & $ZigExe @taskCommon ports/gba/src/movement.c ports/gba/tests/movement_cases.c ports/gba/tests/test_movement.c -o build/gba/host/dok-movement-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Movement tests build failed' }
        & ./build/gba/host/dok-movement-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Movement tests failed' }
        & $ZigExe @taskCommon ports/gba/src/movement.c ports/gba/tools/movement_probe.c -o build/gba/host/dok-movement-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Movement probe build failed' }
        & $ZigExe @taskCommon ports/gba/src/scheduler.c ports/gba/src/clock.c ports/gba/tests/time_cases.c ports/gba/tests/test_time.c -o build/gba/host/dok-time-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Time/scheduler build failed' }
        & ./build/gba/host/dok-time-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Time/scheduler tests failed' }
        & $ZigExe @taskCommon ports/gba/src/projection.c ports/gba/tools/projection_probe.c -o build/gba/host/dok-projection-probe.exe
        if ($LASTEXITCODE -ne 0) { throw 'Projection probe build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/projection.c ports/gba/src/renderer.c ports/gba/tools/render_tool.c -o build/gba/host/dok-render.exe
        if ($LASTEXITCODE -ne 0) { throw 'Static renderer tool build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY -DDOK_RENDER_REFERENCE ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/projection.c ports/gba/src/renderer.c ports/gba/tools/render_tool.c -o build/gba/host/dok-render-reference.exe
        if ($LASTEXITCODE -ne 0) { throw 'Reference renderer build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY -DDOK_RENDER_FACE_CAPACITY=1 ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/projection.c ports/gba/src/renderer.c ports/gba/tools/render_tool.c -o build/gba/host/dok-render-one-face.exe
        if ($LASTEXITCODE -ne 0) { throw 'Renderer capacity probe build failed' }
        & $ZigExe @taskCommon ports/gba/src/res.c ports/gba/tools/res_tool.c -o build/gba/host/dok-res.exe
        if ($LASTEXITCODE -ne 0) { throw 'Resource tool build failed' }
        & $ZigExe @taskCommon ports/gba/src/res.c ports/gba/tests/test_res.c -o build/gba/host/dok-res-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Resource tests build failed' }
        & $ZigExe @taskCommon ports/gba/src/native_state.c ports/gba/tests/test_native_state.c -o build/gba/host/dok-native-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native state tests build failed' }
        & $ZigExe @taskCommon ports/gba/src/image.c ports/gba/src/animation.c ports/gba/tools/animation_tool.c -o build/gba/host/dok-anim.exe
        if ($LASTEXITCODE -ne 0) { throw 'Animation tool build failed' }
        & $ZigExe @taskCommon ports/gba/src/image.c ports/gba/src/animation.c ports/gba/tests/test_animation.c -o build/gba/host/dok-animation-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Animation tests build failed' }
        & $ZigExe @taskCommon ports/gba/src/sound.c ports/gba/tools/sound_tool.c -o build/gba/host/dok-sound.exe
        if ($LASTEXITCODE -ne 0) { throw 'Sound tool build failed' }
        & $ZigExe @taskCommon ports/gba/src/sound.c ports/gba/tests/test_sound.c -o build/gba/host/dok-sound-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Sound tests build failed' }
        & $ZigExe @taskCommon ports/gba/src/level.c ports/gba/tools/level_tool.c -o build/gba/host/dok-level.exe
        if ($LASTEXITCODE -ne 0) { throw 'Level tool build failed' }
        & $ZigExe @taskCommon ports/gba/src/level.c ports/gba/tests/test_level.c -o build/gba/host/dok-level-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Level tests build failed' }
        & $ZigExe @taskCommon ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/tools/world_tool.c -o build/gba/host/dok-world.exe
        if ($LASTEXITCODE -ne 0) { throw 'World tool build failed' }
        & $ZigExe @taskCommon ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/native_state.c ports/gba/tests/test_world.c -o build/gba/host/dok-world-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'World tests build failed' }
        & $ZigExe @taskCommon ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/runtime.c ports/gba/tools/runtime_tool.c -o build/gba/host/dok-runtime.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime tool build failed' }
        & $ZigExe @taskCommon ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/runtime.c ports/gba/tests/test_runtime.c -o build/gba/host/dok-runtime-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime tests build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/tools/rom_assets_tool.c -o build/gba/host/dok-rom-assets.exe
        if ($LASTEXITCODE -ne 0) { throw 'ROM assets tool build failed' }
        & $ZigExe @taskCommon -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/tests/test_rom_assets.c -o build/gba/host/dok-rom-assets-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'ROM assets tests build failed' }
        & $ZigExe @taskCommon ports/gba/platform/memory.c ports/gba/tests/test_gba_helpers.c -o build/gba/host/dok-gba-helper-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'GBA helper tests build failed' }
        & ./build/gba/host/dok-rom-assets-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'ROM assets tests failed' }
        & ./build/gba/host/dok-gba-helper-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'GBA helper tests failed' }
        & ./build/gba/host/dok-runtime-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime tests failed' }
        & ./build/gba/host/dok-world-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'World tests failed' }
        & ./build/gba/host/dok-level-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Level tests failed' }
        & ./build/gba/host/dok-res-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Resource tests failed' }
        & ./build/gba/host/dok-native-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native state tests failed' }
        & ./build/gba/host/dok-animation-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Animation tests failed' }
        & ./build/gba/host/dok-sound-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Sound tests failed' }
        if ($CrossChecks) {
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/ai.c ports/gba/src/combat.c ports/gba/src/items.c ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/src/scheduler.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/res.c ports/gba/tests/test_ai.c -o build/gba/host/dok-ai-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux AI build failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/ai.c ports/gba/src/combat.c ports/gba/src/items.c ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/src/scheduler.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/res.c ports/gba/tests/test_ai_combat.c -o build/gba/host/dok-ai-combat-tests-linux
            if($LASTEXITCODE -ne 0){throw 'G15 Linux compile failed'}
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/ai.c -o build/gba/host/ai-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb AI compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include -DDOK_RUNTIME_ROM_ONLY ports/gba/src/combat.c ports/gba/src/items.c ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/test_combat.c -o build/gba/host/dok-combat-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux combat build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/combat.c -o build/gba/host/combat-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb combat compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/items.c ports/gba/tests/test_items.c -o build/gba/host/dok-items-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux items build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/items.c -o build/gba/host/items-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb items compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/interactions_cases.c ports/gba/tests/test_interactions.c -o build/gba/host/dok-interactions-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux interactions build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/interactions.c -o build/gba/host/interactions-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb interactions compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/collision_cases.c ports/gba/tests/test_collision.c -o build/gba/host/dok-collision-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux collision suite build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/collision.c -o build/gba/host/collision-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'Thumb collision compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/movement.c ports/gba/tests/movement_cases.c ports/gba/tests/test_movement.c -o build/gba/host/dok-movement-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux movement tests build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/movement.c -o build/gba/host/movement-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM movement compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/scheduler.c ports/gba/src/clock.c ports/gba/tests/time_cases.c ports/gba/tests/test_time.c -o build/gba/host/dok-time-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux time tests build failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/res.c ports/gba/tools/res_tool.c -o build/gba/host/dok-res-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux resource tool build failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/native_state.c ports/gba/tests/test_native_state.c -o build/gba/host/dok-native-tests-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux state tests build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/native_state.c -o build/gba/host/native-state-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM7TDMI compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/image.c ports/gba/src/animation.c ports/gba/tools/animation_tool.c -o build/gba/host/dok-anim-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux animation tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/image.c -o build/gba/host/image-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM image compile failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/animation.c -o build/gba/host/animation-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM animation compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/sound.c ports/gba/tools/sound_tool.c -o build/gba/host/dok-sound-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux sound tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/sound.c -o build/gba/host/sound-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM sound compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/level.c ports/gba/tools/level_tool.c -o build/gba/host/dok-level-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux level tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/level.c -o build/gba/host/level-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM level compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/tools/world_tool.c -o build/gba/host/dok-world-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux world tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/world.c -o build/gba/host/world-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM world compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/runtime.c ports/gba/tools/runtime_tool.c -o build/gba/host/dok-runtime-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux runtime tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/runtime.c -o build/gba/host/runtime-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM runtime compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/tools/rom_assets_tool.c -o build/gba/host/dok-rom-assets-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux ROM assets tool build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/rom_assets.c -o build/gba/host/rom-assets-arm7tdmi.o
            if ($LASTEXITCODE -ne 0) { throw 'ARM ROM reader compile failed' }
            & $ZigExe cc -target x86_64-linux-musl -static -std=c11 -Wall -Wextra -Werror -O2 -I include -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/projection.c ports/gba/src/renderer.c ports/gba/tools/render_tool.c -o build/gba/host/dok-render-linux
            if ($LASTEXITCODE -ne 0) { throw 'Linux static renderer build failed' }
            & $ZigExe cc -target thumb-freestanding-eabi -mcpu=arm7tdmi -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -I include -c ports/gba/src/renderer.c -o build/gba/host/renderer-arm7tdmi.o
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
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tests/interactions_cases.c ports/gba/tests/test_interactions.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-interactions-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-interactions-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tools/interactions_probe.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-interactions-probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /I include ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/interactions.c ports/gba/src/scheduler.c ports/gba/src/movement.c ports/gba/src/collision.c ports/gba/tools/interactions_scenarios.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-interactions-scenarios.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/movement.c ports/gba/tests/movement_cases.c ports/gba/tests/test_movement.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-movement-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-movement-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/movement.c ports/gba/tools/movement_probe.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-movement-probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /I include ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/projection.c ports/gba/src/renderer.c ports/gba/tools/render_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-render.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /DDOK_RENDER_FACE_CAPACITY=1 /I include ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/projection.c ports/gba/src/renderer.c ports/gba/tools/render_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-render-one-face.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/projection.c ports/gba/tools/projection_probe.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-projection-probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/res.c ports/gba/tools/res_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-res.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/res.c ports/gba/tests/test_res.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-res-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/native_state.c ports/gba/tests/test_native_state.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-native-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/image.c ports/gba/src/animation.c ports/gba/tools/animation_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-anim.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/image.c ports/gba/src/animation.c ports/gba/tests/test_animation.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-animation-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/sound.c ports/gba/tools/sound_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-sound.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/sound.c ports/gba/tests/test_sound.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-sound-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/level.c ports/gba/tools/level_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-level.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/level.c ports/gba/tests/test_level.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-level-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/tools/world_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-world.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/native_state.c ports/gba/tests/test_world.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-world-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I include ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/runtime.c ports/gba/tools/runtime_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-runtime.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/src/res.c ports/gba/src/image.c ports/gba/src/animation.c ports/gba/src/level.c ports/gba/src/world.c ports/gba/src/runtime.c ports/gba/tests/test_runtime.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-runtime-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /DDOK_RUNTIME_ROM_ONLY /I include ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/tools/rom_assets_tool.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-rom-assets.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /DDOK_RUNTIME_ROM_ONLY /I include ports/gba/src/runtime.c ports/gba/src/rom_assets.c ports/gba/tests/test_rom_assets.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-rom-assets-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /I include ports/gba/platform/memory.c ports/gba/tests/test_gba_helpers.c /Fo:build/gba/host/ /Fe:build/gba/host/dok-gba-helper-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-rom-assets-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-gba-helper-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-runtime-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-world-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-level-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-res-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-native-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-animation-tests.exe
if errorlevel 1 exit /b 1
build\gba\host\dok-sound-tests.exe
exit /b %errorlevel%
'@
Set-Content -LiteralPath "$taskRoot/build/gba/host/build.cmd" -Value $taskBatch -Encoding ASCII
Push-Location $taskRoot
try { & $env:ComSpec /d /c build\gba\host\build.cmd $VsDevCmd $taskRoot }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "Native build or tests failed: $LASTEXITCODE" }

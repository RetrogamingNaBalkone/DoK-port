param(
    [string]$ZigExe = "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe = 'C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$OutDir = 'build/gba/gba',
    [switch]$Verify,
    [switch]$MemoryProbe,
    [switch]$AssetsProbe,
    [switch]$RendererProbe,
    [switch]$HalfColumns,
    [switch]$TimeProbe,
    [switch]$BootSelfTests,
    [string]$AssetsBin = 'build/gba/assets/assets.bin',
    [string]$CorePath = "$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll"
)
$ErrorActionPreference = 'Stop'
$taskRoot = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../../")))
if (-not (Test-Path -LiteralPath $ZigExe)) { throw "Shared Zig not found: $ZigExe" }
if (-not (Test-Path -LiteralPath $PythonExe)) { throw "Python 3.11+ not found: $PythonExe" }
if (([int]$MemoryProbe.IsPresent + [int]$AssetsProbe.IsPresent + [int]$RendererProbe.IsPresent) -gt 1) {
    throw 'Select one of MemoryProbe, AssetsProbe or RendererProbe'
}
Push-Location $taskRoot
$taskOldCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskRoot 'build/gba/zig-cache'
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $taskArgs = @('cc','-target','thumb-freestanding-eabi','-mcpu=arm7tdmi',
        '-std=c11','-Wall','-Wextra','-Werror','-O2','-g','-ffreestanding',
        '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-fno-pic','-I','ports/gba/include','-c')
    if ($TimeProbe -and ($MemoryProbe -or $AssetsProbe -or $RendererProbe)) { throw 'TimeProbe is standalone' }
    if ($TimeProbe) { $taskArgs += '-DDOK_TIME_PROBE' }
    if ($MemoryProbe) { $taskArgs += '-DDOK_RUNTIME_PROBE' }
    if ($AssetsProbe) { $taskArgs += '-DDOK_ASSETS_PROBE' }
    if ($RendererProbe) { $taskArgs += '-DDOK_RENDERER_PROBE' }
    if ($BootSelfTests) { $taskArgs += '-DDOK_BOOT_SELFTEST' }
    if ($HalfColumns -and -not $RendererProbe) { throw 'HalfColumns requires RendererProbe' }
    if ($HalfColumns) { $taskArgs += '-DDOK_RENDER_HALF_COLUMNS' }
    $taskMain = if ($RendererProbe) {'ports/gba/platform/renderer_probe.c'} else {'ports/gba/platform/main.c'}
    & $ZigExe @taskArgs $taskMain -o "$OutDir/main.o"
    if ($LASTEXITCODE -ne 0) { throw 'GBA C compilation failed' }
    $taskStartupArgs = @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c')
    if ($RendererProbe) { $taskStartupArgs += '-DDOK_RENDERER_PROBE' }
    if ($HalfColumns) { $taskStartupArgs += '-DDOK_RENDER_HALF_COLUMNS' }
    & $ZigExe @taskStartupArgs ports/gba/platform/startup.S -o "$OutDir/startup.o"
    if ($LASTEXITCODE -ne 0) { throw 'GBA startup assembly failed' }
    $taskObjects = @("$OutDir/startup.o", "$OutDir/main.o")
    if ($RendererProbe) {
        & $PythonExe ports/gba/scripts/pack_ui_icons.py --assets $AssetsBin --output "$OutDir/ui-icons.S"
        if ($LASTEXITCODE -ne 0) { throw 'UI icon packing failed' }
        & $ZigExe @taskStartupArgs "$OutDir/ui-icons.S" -o "$OutDir/ui-icons.o"
        if ($LASTEXITCODE -ne 0) { throw 'UI icon assembly failed' }
        $taskObjects += "$OutDir/ui-icons.o"
        & $PythonExe ports/gba/scripts/pack_animation_index.py --assets $AssetsBin --output "$OutDir/animation-index.S"
        if ($LASTEXITCODE -ne 0) { throw 'Animation index validation failed' }
        & $ZigExe @taskStartupArgs "$OutDir/animation-index.S" -o "$OutDir/animation-index.o"
        if ($LASTEXITCODE -ne 0) { throw 'Animation index assembly failed' }
        $taskObjects += "$OutDir/animation-index.o"
        & $PythonExe ports/gba/scripts/render_tables.py --output "$OutDir/render-tables.S"
        if ($LASTEXITCODE -ne 0) { throw 'Renderer tables failed' }
        & $ZigExe @taskStartupArgs "$OutDir/render-tables.S" -o "$OutDir/render-tables.o"
        if ($LASTEXITCODE -ne 0) { throw 'Renderer tables assembly failed' }
        $taskObjects += "$OutDir/render-tables.o"
        & $ZigExe @taskStartupArgs ports/gba/platform/divmod.S -o "$OutDir/divmod.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM EABI divmod assembly failed' }
        $taskObjects += "$OutDir/divmod.o"
        & $ZigExe @taskStartupArgs ports/gba/platform/columns.S -o "$OutDir/columns.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM column assembly failed' }
        $taskObjects += "$OutDir/columns.o"
    }
    if ($MemoryProbe) {
        & $ZigExe @taskArgs ports/gba/platform/memory_probe.c -o "$OutDir/memory-probe.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM memory probe compilation failed' }
        $taskObjects += "$OutDir/memory-probe.o"
        & $ZigExe @taskArgs -DDOK_RUNTIME_ROM_ONLY ports/gba/src/runtime.c -o "$OutDir/runtime.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM runtime compilation failed' }
        & $ZigExe @taskArgs ports/gba/platform/memory.c -o "$OutDir/memory.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM EABI helper compilation failed' }
        $taskObjects += @("$OutDir/runtime.o", "$OutDir/memory.o")
    }
    $taskLinkArgs = @()
    if ($RendererProbe) {
        foreach ($taskSource in @('ports/gba/src/flow.c','ports/gba/platform/flow_backend.c','ports/gba/src/ui.c','ports/gba/platform/ui_backend.c','ports/gba/src/audio.c','ports/gba/src/sound.c','ports/gba/platform/audio_backend.c','ports/gba/src/ai.c','ports/gba/platform/ai_probe.c','ports/gba/src/combat.c','ports/gba/platform/combat_probe.c','ports/gba/src/items.c','ports/gba/src/movement.c','ports/gba/src/collision.c','ports/gba/src/interactions.c','ports/gba/platform/movement_probe.c','ports/gba/tests/movement_cases.c','ports/gba/tests/collision_cases.c','ports/gba/tests/interactions_cases.c')) {
            $taskObject="$OutDir/motion-"+[IO.Path]::GetFileNameWithoutExtension($taskSource)+'.o'
            & $ZigExe @taskArgs $taskSource -o $taskObject
            if ($LASTEXITCODE -ne 0) { throw "Movement compile failed: $taskSource" }
            $taskObjects += $taskObject
        }
    }
    if ($TimeProbe -or $RendererProbe) {
        foreach ($taskSource in @('ports/gba/src/scheduler.c','ports/gba/src/clock.c','ports/gba/platform/clock.c','ports/gba/platform/time_probe.c','ports/gba/tests/time_cases.c')) {
            $taskObject="$OutDir/time-"+[IO.Path]::GetFileNameWithoutExtension($taskSource)+'.o'
            if ($taskSource -eq 'ports/gba/platform/clock.c') { $taskObject="$OutDir/gba-clock.o" }
            & $ZigExe @taskArgs $taskSource -o $taskObject
            if ($LASTEXITCODE -ne 0) { throw "Time probe compile failed: $taskSource" }
            $taskObjects += $taskObject
        }
        if ($TimeProbe) {
            & $ZigExe @taskArgs ports/gba/platform/memory.c -o "$OutDir/memory.o"
            if ($LASTEXITCODE -ne 0) { throw 'Time helpers compile failed' }
            $taskObjects += "$OutDir/memory.o"
        }
        $taskLinkArgs += '--defsym=__iwram_reserved_override=8192'
    }
    if ($AssetsProbe -or $RendererProbe) {
        if (-not (Test-Path -LiteralPath $AssetsBin)) { throw "Pack assets first: $AssetsBin" }
        $taskAssetPath = (Resolve-Path -LiteralPath $AssetsBin).Path.Replace('\','/')
        $taskAssetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $AssetsBin).Hash.ToLowerInvariant()
        $taskColumnSource = ''
        if ($RendererProbe) {
            & $PythonExe ports/gba/scripts/pack_render_columns.py --assets $AssetsBin --output "$OutDir/render-columns.bin"
            if ($LASTEXITCODE -ne 0) { throw 'Render column pack failed' }
            $taskColumnPath = (Resolve-Path -LiteralPath "$OutDir/render-columns.bin").Path.Replace('\','/')
            # Zig's assembly cache does not track changes inside .incbin files.
            # Put their digests in the assembly input to invalidate that cache.
            $taskColumnHash = (Get-FileHash -Algorithm SHA256 -LiteralPath "$OutDir/render-columns.bin").Hash.ToLowerInvariant()
            $taskColumnSource = ".ascii `"$taskColumnHash`"`n.balign 4`n.global dok_render_bank`ndok_render_bank:`n.incbin `"$taskColumnPath`"`n.global dok_render_bank_end`ndok_render_bank_end:"
        }
        $taskAssetSource = @"
.section .rodata.dok_assets,"a",%progbits
.ascii "$taskAssetHash"
.balign 4
.global dok_gba_assets
dok_gba_assets:
.incbin "$taskAssetPath"
.global dok_gba_assets_end
dok_gba_assets_end:
$taskColumnSource
"@
        Set-Content -Encoding ascii -LiteralPath "$OutDir/assets-data.S" -Value $taskAssetSource
        & $ZigExe @taskArgs "$OutDir/assets-data.S" -o "$OutDir/assets-data.o"
        if ($LASTEXITCODE -ne 0) { throw 'Asset payload assembly failed' }
        $taskAssetSources = if ($RendererProbe) {
            @('ports/gba/platform/memory.c','ports/gba/src/runtime.c','ports/gba/src/rom_assets.c','ports/gba/src/projection.c','ports/gba/src/image.c','ports/gba/src/animation.c','ports/gba/src/visual.c','ports/gba/src/renderer.c')
        } else {@('ports/gba/platform/assets_probe.c','ports/gba/platform/memory.c','ports/gba/src/runtime.c','ports/gba/src/rom_assets.c')}
        foreach ($taskSource in $taskAssetSources) {
            $taskObject = "$OutDir/" + [IO.Path]::GetFileNameWithoutExtension($taskSource) + '.o'
            $taskSourceArgs = $taskArgs
            if ($RendererProbe -and $taskSource -in @('ports/gba/src/renderer.c','ports/gba/src/projection.c','ports/gba/platform/memory.c')) {
                $taskSourceArgs = $taskArgs -replace 'thumb-freestanding-eabi','arm-freestanding-eabi'
            }
            & $ZigExe @taskSourceArgs -DDOK_RUNTIME_ROM_ONLY $taskSource -o $taskObject
            if ($LASTEXITCODE -ne 0) { throw "ARM assets source compilation failed: $taskSource" }
            $taskObjects += $taskObject
        }
        $taskObjects += "$OutDir/assets-data.o"
        $taskLinkArgs += '--defsym=__iwram_reserved_override=8192'
    }
    # Direct bundled LLD exposes map output, unsupported by zig cc's flag parser.
    & $ZigExe ld.lld -T ports/gba/platform/gba.ld --gc-sections "-Map=$OutDir/dok-diagnostic.map" @taskLinkArgs @taskObjects -o "$OutDir/dok-diagnostic.elf"
    if ($LASTEXITCODE -ne 0) { throw 'GBA ELF link failed' }
    & $ZigExe objcopy -O binary "$OutDir/dok-diagnostic.elf" "$OutDir/dok-diagnostic.gba"
    if ($LASTEXITCODE -ne 0) { throw 'GBA ROM extraction failed' }
    & $PythonExe ports/gba/scripts/gba_rom.py --elf "$OutDir/dok-diagnostic.elf" --rom "$OutDir/dok-diagnostic.gba" --out "$OutDir/build-report.json"
    if ($LASTEXITCODE -ne 0) { throw 'GBA header/ELF validation failed' }
    if ($MemoryProbe) {
        & $PythonExe tools/originals/runtime_memory.py --elf "$OutDir/dok-diagnostic.elf" --report "$OutDir/memory-layout.json"
        if ($LASTEXITCODE -ne 0) { throw 'ARM memory layout validation failed' }
    }
    & $ZigExe version | Set-Content -Encoding ascii "$OutDir/zig-version.txt"
    Write-Output "GBA ELF, ROM, map, and build report: $OutDir"
    if ($Verify) {
        if ($TimeProbe) {
            & $PythonExe ports/gba/scripts/verify_time_gba.py --core $CorePath --build-dir $OutDir
        } elseif ($MemoryProbe) {
            & $PythonExe ports/gba/scripts/verify_gba.py --core $CorePath --build-dir $OutDir --task G02-memory --report analysis/gba/g02-memory-run.json
        } elseif ($RendererProbe) {
            & $PythonExe ports/gba/scripts/verify_sprites_gba.py --core $CorePath --build-dir $OutDir --assets-bin $AssetsBin
            if ($LASTEXITCODE -ne 0) { throw 'mGBA renderer verification failed' }
            & $PythonExe ports/gba/scripts/verify_time_gba.py --core $CorePath --build-dir $OutDir --report analysis/gba/g06-renderer-time.json
        } elseif ($AssetsProbe) {
            & $PythonExe ports/gba/scripts/verify_assets_gba.py --core $CorePath --build-dir $OutDir --assets-bin $AssetsBin
        } else {
            & $PythonExe ports/gba/scripts/verify_gba.py --core $CorePath --build-dir $OutDir
        }
        if ($LASTEXITCODE -ne 0) { throw 'mGBA diagnostic verification failed' }
    }
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $taskOldCache
    Pop-Location
}

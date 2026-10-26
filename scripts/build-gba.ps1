param(
    [string]$ZigExe = "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe = 'C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$OutDir = 'build/gba',
    [switch]$Verify,
    [switch]$MemoryProbe,
    [switch]$AssetsProbe,
    [switch]$RendererProbe,
    [switch]$HalfColumns,
    [switch]$TimeProbe,
    [string]$AssetsBin = 'build/gba/assets/assets.bin',
    [string]$CorePath = "$env:LOCALAPPDATA/Programs/mGBA/libretro-20261024/mgba_libretro.dll"
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath $ZigExe)) { throw "Shared Zig not found: $ZigExe" }
if (-not (Test-Path -LiteralPath $PythonExe)) { throw "Python 3.11+ not found: $PythonExe" }
if (([int]$MemoryProbe.IsPresent + [int]$AssetsProbe.IsPresent + [int]$RendererProbe.IsPresent) -gt 1) {
    throw 'Select one of MemoryProbe, AssetsProbe or RendererProbe'
}
Push-Location $taskRoot
$taskOldCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskRoot 'build/zig-cache'
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $taskArgs = @('cc','-target','thumb-freestanding-eabi','-mcpu=arm7tdmi',
        '-std=c11','-Wall','-Wextra','-Werror','-O2','-g','-ffreestanding',
        '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-fno-pic','-I','include','-c')
    if ($TimeProbe -and ($MemoryProbe -or $AssetsProbe -or $RendererProbe)) { throw 'TimeProbe is standalone' }
    if ($TimeProbe) { $taskArgs += '-DDOK_TIME_PROBE' }
    if ($MemoryProbe) { $taskArgs += '-DDOK_RUNTIME_PROBE' }
    if ($AssetsProbe) { $taskArgs += '-DDOK_ASSETS_PROBE' }
    if ($RendererProbe) { $taskArgs += '-DDOK_RENDERER_PROBE' }
    if ($HalfColumns -and -not $RendererProbe) { throw 'HalfColumns requires RendererProbe' }
    if ($HalfColumns) { $taskArgs += '-DDOK_RENDER_HALF_COLUMNS' }
    $taskMain = if ($RendererProbe) {'gba/renderer_probe.c'} else {'gba/main.c'}
    & $ZigExe @taskArgs $taskMain -o "$OutDir/main.o"
    if ($LASTEXITCODE -ne 0) { throw 'GBA C compilation failed' }
    $taskStartupArgs = @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c')
    if ($RendererProbe) { $taskStartupArgs += '-DDOK_RENDERER_PROBE' }
    if ($HalfColumns) { $taskStartupArgs += '-DDOK_RENDER_HALF_COLUMNS' }
    & $ZigExe @taskStartupArgs gba/startup.S -o "$OutDir/startup.o"
    if ($LASTEXITCODE -ne 0) { throw 'GBA startup assembly failed' }
    $taskObjects = @("$OutDir/startup.o", "$OutDir/main.o")
    if ($RendererProbe) {
        & $PythonExe scripts/render_tables.py --output "$OutDir/render-tables.S"
        if ($LASTEXITCODE -ne 0) { throw 'Renderer tables failed' }
        & $ZigExe @taskStartupArgs "$OutDir/render-tables.S" -o "$OutDir/render-tables.o"
        if ($LASTEXITCODE -ne 0) { throw 'Renderer tables assembly failed' }
        $taskObjects += "$OutDir/render-tables.o"
        & $ZigExe @taskStartupArgs gba/divmod.S -o "$OutDir/divmod.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM EABI divmod assembly failed' }
        $taskObjects += "$OutDir/divmod.o"
        & $ZigExe @taskStartupArgs gba/columns.S -o "$OutDir/columns.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM column assembly failed' }
        $taskObjects += "$OutDir/columns.o"
    }
    if ($MemoryProbe) {
        & $ZigExe @taskArgs gba/memory_probe.c -o "$OutDir/memory-probe.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM memory probe compilation failed' }
        $taskObjects += "$OutDir/memory-probe.o"
        & $ZigExe @taskArgs -DDOK_RUNTIME_ROM_ONLY src/runtime.c -o "$OutDir/runtime.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM runtime compilation failed' }
        & $ZigExe @taskArgs gba/memory.c -o "$OutDir/memory.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM EABI helper compilation failed' }
        $taskObjects += @("$OutDir/runtime.o", "$OutDir/memory.o")
    }
    $taskLinkArgs = @()
    if ($RendererProbe) {
        foreach ($taskSource in @('src/ai.c','gba/ai_probe.c','src/combat.c','gba/combat_probe.c','src/items.c','src/movement.c','src/collision.c','src/interactions.c','gba/movement_probe.c','tests/movement_cases.c','tests/collision_cases.c','tests/interactions_cases.c')) {
            $taskObject="$OutDir/motion-"+[IO.Path]::GetFileNameWithoutExtension($taskSource)+'.o'
            & $ZigExe @taskArgs $taskSource -o $taskObject
            if ($LASTEXITCODE -ne 0) { throw "Movement compile failed: $taskSource" }
            $taskObjects += $taskObject
        }
    }
    if ($TimeProbe -or $RendererProbe) {
        foreach ($taskSource in @('src/scheduler.c','src/clock.c','gba/clock.c','gba/time_probe.c','tests/time_cases.c')) {
            $taskObject="$OutDir/time-"+[IO.Path]::GetFileNameWithoutExtension($taskSource)+'.o'
            if ($taskSource -eq 'gba/clock.c') { $taskObject="$OutDir/gba-clock.o" }
            & $ZigExe @taskArgs $taskSource -o $taskObject
            if ($LASTEXITCODE -ne 0) { throw "Time probe compile failed: $taskSource" }
            $taskObjects += $taskObject
        }
        if ($TimeProbe) {
            & $ZigExe @taskArgs gba/memory.c -o "$OutDir/memory.o"
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
            & $PythonExe scripts/pack_render_columns.py --assets $AssetsBin --output "$OutDir/render-columns.bin"
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
            @('gba/memory.c','src/runtime.c','src/rom_assets.c','src/projection.c','src/image.c','src/animation.c','src/visual.c','src/renderer.c')
        } else {@('gba/assets_probe.c','gba/memory.c','src/runtime.c','src/rom_assets.c')}
        foreach ($taskSource in $taskAssetSources) {
            $taskObject = "$OutDir/" + [IO.Path]::GetFileNameWithoutExtension($taskSource) + '.o'
            $taskSourceArgs = $taskArgs
            if ($RendererProbe -and $taskSource -in @('src/renderer.c','src/projection.c','gba/memory.c')) {
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
    & $ZigExe ld.lld -T gba/gba.ld --gc-sections "-Map=$OutDir/dok-diagnostic.map" @taskLinkArgs @taskObjects -o "$OutDir/dok-diagnostic.elf"
    if ($LASTEXITCODE -ne 0) { throw 'GBA ELF link failed' }
    & $ZigExe objcopy -O binary "$OutDir/dok-diagnostic.elf" "$OutDir/dok-diagnostic.gba"
    if ($LASTEXITCODE -ne 0) { throw 'GBA ROM extraction failed' }
    & $PythonExe scripts/gba_rom.py --elf "$OutDir/dok-diagnostic.elf" --rom "$OutDir/dok-diagnostic.gba" --out "$OutDir/build-report.json"
    if ($LASTEXITCODE -ne 0) { throw 'GBA header/ELF validation failed' }
    if ($MemoryProbe) {
        & $PythonExe scripts/runtime_memory.py --elf "$OutDir/dok-diagnostic.elf" --report "$OutDir/memory-layout.json"
        if ($LASTEXITCODE -ne 0) { throw 'ARM memory layout validation failed' }
    }
    & $ZigExe version | Set-Content -Encoding ascii "$OutDir/zig-version.txt"
    Write-Output "GBA ELF, ROM, map, and build report: $OutDir"
    if ($Verify) {
        if ($TimeProbe) {
            & $PythonExe scripts/verify_time_gba.py --core $CorePath --build-dir $OutDir
        } elseif ($MemoryProbe) {
            & $PythonExe scripts/verify_gba.py --core $CorePath --build-dir $OutDir --task G02-memory --report analysis/g02-memory-run.json
        } elseif ($RendererProbe) {
            & $PythonExe scripts/verify_sprites_gba.py --core $CorePath --build-dir $OutDir --assets-bin $AssetsBin
            if ($LASTEXITCODE -ne 0) { throw 'mGBA renderer verification failed' }
            & $PythonExe scripts/verify_time_gba.py --core $CorePath --build-dir $OutDir --report analysis/g06-renderer-time.json
        } elseif ($AssetsProbe) {
            & $PythonExe scripts/verify_assets_gba.py --core $CorePath --build-dir $OutDir --assets-bin $AssetsBin
        } else {
            & $PythonExe scripts/verify_gba.py --core $CorePath --build-dir $OutDir
        }
        if ($LASTEXITCODE -ne 0) { throw 'mGBA diagnostic verification failed' }
    }
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $taskOldCache
    Pop-Location
}

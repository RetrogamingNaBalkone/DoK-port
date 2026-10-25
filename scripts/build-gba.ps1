param(
    [string]$ZigExe = "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe",
    [string]$PythonExe = 'C:/Source/wasdify/.venv/Scripts/python.exe',
    [string]$OutDir = 'build/gba',
    [switch]$Verify,
    [switch]$MemoryProbe,
    [switch]$AssetsProbe,
    [switch]$RendererProbe,
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
    if ($MemoryProbe) { $taskArgs += '-DDOK_RUNTIME_PROBE' }
    if ($AssetsProbe) { $taskArgs += '-DDOK_ASSETS_PROBE' }
    if ($RendererProbe) { $taskArgs += '-DDOK_RENDERER_PROBE' }
    $taskMain = if ($RendererProbe) {'gba/renderer_probe.c'} else {'gba/main.c'}
    & $ZigExe @taskArgs $taskMain -o "$OutDir/main.o"
    if ($LASTEXITCODE -ne 0) { throw 'GBA C compilation failed' }
    $taskStartupArgs = @('cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-c')
    if ($RendererProbe) { $taskStartupArgs += '-DDOK_RENDERER_PROBE' }
    & $ZigExe @taskStartupArgs gba/startup.S -o "$OutDir/startup.o"
    if ($LASTEXITCODE -ne 0) { throw 'GBA startup assembly failed' }
    $taskObjects = @("$OutDir/startup.o", "$OutDir/main.o")
    if ($RendererProbe) {
        & $ZigExe @taskStartupArgs gba/divmod.S -o "$OutDir/divmod.o"
        if ($LASTEXITCODE -ne 0) { throw 'ARM EABI divmod assembly failed' }
        $taskObjects += "$OutDir/divmod.o"
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
    if ($AssetsProbe -or $RendererProbe) {
        if (-not (Test-Path -LiteralPath $AssetsBin)) { throw "Pack assets first: $AssetsBin" }
        $taskAssetPath = (Resolve-Path -LiteralPath $AssetsBin).Path.Replace('\','/')
        $taskAssetSource = @"
.section .rodata.dok_assets,"a",%progbits
.balign 4
.global dok_gba_assets
dok_gba_assets:
.incbin "$taskAssetPath"
.global dok_gba_assets_end
dok_gba_assets_end:
"@
        Set-Content -Encoding ascii -LiteralPath "$OutDir/assets-data.S" -Value $taskAssetSource
        & $ZigExe @taskArgs "$OutDir/assets-data.S" -o "$OutDir/assets-data.o"
        if ($LASTEXITCODE -ne 0) { throw 'Asset payload assembly failed' }
        $taskAssetSources = if ($RendererProbe) {
            @('gba/memory.c','src/runtime.c','src/rom_assets.c','src/projection.c','src/image.c','src/renderer.c')
        } else {@('gba/assets_probe.c','gba/memory.c','src/runtime.c','src/rom_assets.c')}
        foreach ($taskSource in $taskAssetSources) {
            $taskObject = "$OutDir/" + [IO.Path]::GetFileNameWithoutExtension($taskSource) + '.o'
            & $ZigExe @taskArgs -DDOK_RUNTIME_ROM_ONLY $taskSource -o $taskObject
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
        if ($MemoryProbe) {
            & $PythonExe scripts/verify_gba.py --core $CorePath --build-dir $OutDir --task G02-memory --report analysis/g02-memory-run.json
        } elseif ($RendererProbe) {
            & $PythonExe scripts/verify_renderer_gba.py --core $CorePath --build-dir $OutDir --assets-bin $AssetsBin
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

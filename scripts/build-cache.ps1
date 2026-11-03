# Dot-source from a GBA builder; the owner closes its unique session in finally.
function Open-GbaBuildCache([string]$Root) {
    $Root = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $state = [pscustomobject]@{
        Root = $Root; Owned = $false; Path = $null
        Marker = $env:DOK_GBA_BUILD_CACHE
        Global = $env:ZIG_GLOBAL_CACHE_DIR
        Local = $env:ZIG_LOCAL_CACHE_DIR
    }
    $parent = [IO.Path]::GetFullPath((Join-Path $Root 'build/gba/.compiler-cache'))
    $path = if ($env:DOK_GBA_BUILD_CACHE) {
        [IO.Path]::GetFullPath($env:DOK_GBA_BUILD_CACHE)
    } else {
        Join-Path $parent ('run-' + [guid]::NewGuid().ToString('N'))
    }
    if ((Split-Path $path -Parent) -ne $parent -or (Split-Path $path -Leaf) -notmatch '^run-[a-f0-9]{32}$') {
        throw "Unexpected GBA compiler cache: $path"
    }
    $current = $path
    while ($current -ne $Root) {
        if ((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Compiler cache traverses a reparse point: $current"
        }
        $next = Split-Path $current -Parent
        if (!$next -or $next -eq $current) { throw "Cache outside workspace: $path" }
        $current = $next
    }
    if ($env:DOK_GBA_BUILD_CACHE) {
        if (!(Test-Path -LiteralPath $path -PathType Container)) { throw "Missing inherited cache: $path" }
    } else {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
        $state.Owned = $true
    }
    $state.Path = $path
    $env:DOK_GBA_BUILD_CACHE = $path
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $path 'global'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $path 'local'
    return $state
}

function Close-GbaBuildCache($State) {
    if ($null -eq $State) { return }
    try {
        if ($State.Owned -and $env:DOK_KEEP_COMPILER_CACHE -ne '1') {
            & py -3.14 -B (Join-Path $State.Root 'scripts/build_cache.py') --remove $State.Path
            if ($LASTEXITCODE -ne 0) { Write-Warning "Compiler cache retained: $($State.Path)" }
        }
    } finally {
        $env:DOK_GBA_BUILD_CACHE = $State.Marker
        $env:ZIG_GLOBAL_CACHE_DIR = $State.Global
        $env:ZIG_LOCAL_CACHE_DIR = $State.Local
    }
}

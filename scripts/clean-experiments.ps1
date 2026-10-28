param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$buildRoot = Join-Path $taskRoot 'research/experiments'

function Assert-LocalPath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target outside workspace: $resolved"
    }
    # Never traverse junctions/symlinks, including an ancestor of a target.
    $current = $resolved
    while ($current -ne $taskRoot) {
        if ((Test-Path -LiteralPath $current) -and
            ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Cleanup target traverses a reparse point: $current"
        }
        $current = Split-Path $current -Parent
    }
    return $resolved
}

$targets = @()
if (Test-Path -LiteralPath $buildRoot) {
    # Baseline objects in select-menu-fix, level1-fixes and g20 remain usable.
    foreach ($directory in Get-ChildItem -LiteralPath $buildRoot -Directory -Filter 'g20a-*') {
        $null = Assert-LocalPath $directory.FullName
        $targets += Get-ChildItem -LiteralPath $directory.FullName -Recurse -File -Filter '*.o'
    }
}
foreach ($relative in @('build/gba/zig-cache', 'scripts/__pycache__',
                         'build/gba/assets-check/corruptions',
                         'build/gba/assets-check/repeat-a', 'build/gba/assets-check/repeat-b')) {
    $path = Assert-LocalPath (Join-Path $taskRoot $relative)
    if (Test-Path -LiteralPath $path) { $targets += Get-Item -LiteralPath $path -Force }
}
$entries = @()
foreach ($target in $targets) {
    $path = Assert-LocalPath $target.FullName
    $files = @($target)
    if ($target.PSIsContainer) { $files = @(Get-ChildItem -LiteralPath $path -Recurse -Force) }
    foreach ($item in $files) { $null = Assert-LocalPath $item.FullName }
    $regularFiles = @($files | Where-Object { -not $_.PSIsContainer })
    $bytes = ($regularFiles | Measure-Object -Property Length -Sum).Sum
    $entries += [pscustomobject]@{
        path = $path.Substring($taskRoot.Length + 1).Replace('\', '/')
        files = $regularFiles.Count
        bytes = [long]$bytes
    }
    if ($Apply) { Remove-Item -LiteralPath $path -Recurse -Force }
}
$totalBytes = [long](($entries | Measure-Object -Property bytes -Sum).Sum)
$result = [ordered]@{
    applied = [bool]$Apply
    files = [long](($entries | Measure-Object -Property files -Sum).Sum)
    bytes = $totalBytes
    removed = $entries
}
if ($Apply -and $entries.Count -gt 0) {
    $report = Join-Path $taskRoot ('analysis/artifact-cleanup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding UTF8
}
[pscustomobject]@{ Applied = [bool]$Apply; Files = $result.files; MiB = [math]::Round($totalBytes / 1MB, 1) }

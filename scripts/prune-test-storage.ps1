param(
    [Parameter(Mandatory=$true)][string]$Manifest,
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskManifestPath = [IO.Path]::GetFullPath((Join-Path $taskRoot $Manifest))
if (-not $taskManifestPath.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Manifest outside workspace'
}
$taskPlan = Get-Content -LiteralPath $taskManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskPlan.workspace -ne $taskRoot -or $taskPlan.schema -ne 1) { throw 'Wrong workspace/schema' }
$taskTargets = [System.Collections.Generic.List[string]]::new()
$taskChecked = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$taskTracked = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($taskTrackedPath in (& git -C $taskRoot -c core.quotepath=false ls-files)) { $null = $taskTracked.Add($taskTrackedPath) }
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect Git index' }
# Validate every target before deleting anything. No recursive filesystem operations.
foreach ($taskItem in $taskPlan.delete) {
    $taskRelative = $taskItem.path
    if ($taskTracked.Contains($taskRelative)) { throw "Tracked file: $taskRelative" }
    $taskCache = $taskRelative -match '^build/(?:.*/)?(?:zig-cache|zig-global-cache|zig-local-cache|\.zig-cache|\.compiler-cache|__pycache__)/' -or
        $taskRelative -match '^(?:ports|scripts|tools)/(?:.*/)?__pycache__/'
    $taskDisk = $taskRelative -match '^build/nds/.*/save/melonDS DS/dldi_sd_card\.bin(?:\.idx)?$'
    if (-not ($taskCache -or $taskDisk) -or $taskRelative.Contains('..')) { throw "Unexpected deletion: $taskRelative" }
    $taskPath = [IO.Path]::GetFullPath((Join-Path $taskRoot $taskRelative))
    if (-not $taskPath.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $taskPath" }
    $taskAncestor = $taskPath
    while ($taskAncestor -ne $taskRoot -and -not $taskChecked.Contains($taskAncestor)) {
        $taskEntry = Get-Item -LiteralPath $taskAncestor -Force
        if ($taskEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $taskAncestor" }
        $null = $taskChecked.Add($taskAncestor)
        $taskAncestor = Split-Path $taskAncestor -Parent
    }
    $taskFile = Get-Item -LiteralPath $taskPath -Force
    if ($taskFile.PSIsContainer -or $taskFile.Length -ne $taskItem.bytes) { throw "Changed file: $taskPath" }
    if ((Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskItem.sha256) { throw "Changed bytes: $taskPath" }
    # An active emulator must not lose its mounted disk.
    if ($taskDisk) {
        $taskLock = [IO.File]::Open($taskPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $taskLock.Dispose()
    }
    $taskTargets.Add($taskPath)
}
Write-Output "Validated $($taskTargets.Count) files, $($taskPlan.bytes) bytes"
if (-not $Apply) { return }
$taskCount = 0
foreach ($taskPath in $taskTargets) {
    Remove-Item -LiteralPath $taskPath -Force
    $taskCount++
    if ($taskCount % 5000 -eq 0) { Write-Output "Deleted $taskCount files" }
}
Write-Output "Deleted $taskCount files; run prune_test_storage.py verify with the same manifest"

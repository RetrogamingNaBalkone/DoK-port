param([switch]$Apply, [string]$Python = 'C:/Source/wasdify/.venv/Scripts/python.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskPlanPath = Join-Path $taskRoot 'research/_migration/prune-generated-files.json'
if (-not (Test-Path -LiteralPath $taskPlanPath)) {
    & $Python "$PSScriptRoot/prune_generated_storage.py" plan
    if ($LASTEXITCODE -ne 0) { throw 'Inventory failed' }
}
$taskPlan = Get-Content -LiteralPath $taskPlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
$taskTargets = [System.Collections.Generic.List[string]]::new()
$taskCheckedAncestors = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($taskItem in $taskPlan.delete) {
    if ($taskItem.path -notmatch '^(build/|research/experiments/|artifacts/|scripts/__pycache__/)') { throw 'Unexpected deletion root' }
    $taskPath = [IO.Path]::GetFullPath((Join-Path $taskRoot $taskItem.path))
    if (-not $taskPath.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $taskPath" }
    $taskAncestor = $taskPath
    while ($taskAncestor -ne $taskRoot -and -not $taskCheckedAncestors.Contains($taskAncestor)) {
        if ((Test-Path -LiteralPath $taskAncestor) -and
            ((Get-Item -LiteralPath $taskAncestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point: $taskAncestor" }
        $taskAncestor = Split-Path $taskAncestor -Parent
    }
    $taskFile = Get-Item -LiteralPath $taskPath -Force
    if ($taskFile.PSIsContainer -or $taskFile.Length -ne $taskItem.bytes) { throw "Changed target: $taskPath" }
    $taskParent = Split-Path $taskPath -Parent
    while ($taskParent -ne $taskRoot -and $taskCheckedAncestors.Add($taskParent)) { $taskParent = Split-Path $taskParent -Parent }
    $taskTargets.Add($taskPath)
}
if (-not $Apply) { "Deletion candidates: $($taskTargets.Count)"; return }
# Only regular files, with all absolute targets validated above; no recursive delete.
foreach ($taskPath in $taskTargets) { Remove-Item -LiteralPath $taskPath -Force }
& $Python "$PSScriptRoot/prune_generated_storage.py" verify
if ($LASTEXITCODE -ne 0) { throw 'Post-cleanup verification failed' }

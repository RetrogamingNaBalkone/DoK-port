param(
    [string]$Python = 'C:/Source/wasdify/.venv/Scripts/python.exe',
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskReport = Join-Path $taskRoot 'analysis/research-relocation-20261027.json'

function Assert-WorkspacePath([string]$Relative) {
    $taskPath = if ([IO.Path]::IsPathRooted($Relative)) {
        [IO.Path]::GetFullPath($Relative)
    } else { [IO.Path]::GetFullPath((Join-Path $taskRoot $Relative)) }
    if (-not $taskPath.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside workspace: $taskPath"
    }
    $taskCurrent = $taskPath
    while ($taskCurrent -ne $taskRoot) {
        if ((Test-Path -LiteralPath $taskCurrent) -and
            ((Get-Item -LiteralPath $taskCurrent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse point: $taskCurrent"
        }
        $taskCurrent = Split-Path $taskCurrent -Parent
    }
    return $taskPath
}

if (-not (Test-Path -LiteralPath $taskReport)) {
    & $Python "$PSScriptRoot/relocate_research.py" plan
    if ($LASTEXITCODE -ne 0) { throw 'Planning failed' }
}
$taskPlan = Get-Content -LiteralPath $taskReport -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskPlan.status -ne 'planned') { throw "Migration status: $($taskPlan.status); use Python verify for rechecks" }
# Preflight all moves, including descendants, before moving the first directory.
foreach ($taskMove in $taskPlan.moves) {
    if (-not $taskMove.source.StartsWith('build/') -or -not $taskMove.destination.StartsWith('research/')) {
        throw 'Unexpected source/destination root'
    }
    $taskSource = Assert-WorkspacePath $taskMove.source
    $taskDestination = Assert-WorkspacePath $taskMove.destination
    if (-not (Test-Path -LiteralPath $taskSource)) { throw "Missing source: $taskSource" }
    if (Test-Path -LiteralPath $taskDestination) { throw "Destination exists: $taskDestination" }
    foreach ($taskChild in Get-ChildItem -LiteralPath $taskSource -Recurse -Force) {
        $null = Assert-WorkspacePath $taskChild.FullName
    }
}
if (-not $Apply) { $taskPlan.moves | Format-Table; return }
foreach ($taskMove in $taskPlan.moves) {
    $taskSource = Assert-WorkspacePath $taskMove.source
    $taskDestination = Assert-WorkspacePath $taskMove.destination
    New-Item -ItemType Directory -Force (Split-Path $taskDestination -Parent) | Out-Null
    Move-Item -LiteralPath $taskSource -Destination $taskDestination
}
& $Python "$PSScriptRoot/relocate_research.py" update
if ($LASTEXITCODE -ne 0) { throw 'Reference update failed; retain manifest and moved files' }
& $Python "$PSScriptRoot/relocate_research.py" verify
if ($LASTEXITCODE -ne 0) { throw 'Verification failed; inspect the manifest' }

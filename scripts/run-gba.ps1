param(
    [string]$EmulatorExe = "$env:LOCALAPPDATA/Programs/mGBA/mGBA-0.10.5-win64/mGBA.exe",
    [string]$Rom = 'build/gba/dok-diagnostic.gba',
    [switch]$Gdb
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
    if (-not (Test-Path -LiteralPath $EmulatorExe)) { throw "Shared mGBA not found: $EmulatorExe" }
    if (-not (Test-Path -LiteralPath $Rom)) { throw "Build the ROM first: $Rom" }
    $taskArgs = @('-3')
    if ($Gdb) { $taskArgs += '-g' }
    & $EmulatorExe @taskArgs $Rom
} finally { Pop-Location }

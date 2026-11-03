param([string]$PackageName = 'windows-playtest')
$ErrorActionPreference = 'Stop'
if ($PackageName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'PackageName must be a directory name' }
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$taskBuild = Join-Path $taskRoot 'build/sdl2/windows'
$taskOut = Join-Path $taskRoot "artifacts/sdl2/$PackageName"
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
foreach ($name in @('dok_port.exe','SDL2.dll')) {
    Copy-Item -LiteralPath (Join-Path $taskBuild $name) -Destination (Join-Path $taskOut $name) -Force
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'ports/sdl2/PLAYTEST.md') -Destination (Join-Path $taskOut 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs/SDL2/SDL2-2.32.10/LICENSE.txt') -Destination (Join-Path $taskOut 'SDL2-LICENSE.txt') -Force
foreach ($mode in @('OpenGL','Software')) {
    $backend = if ($mode -eq 'OpenGL') {'gl'} else {'software'}
    $launcher = '@echo off' + "`r`n" + '"%~dp0dok_port.exe" --renderer ' + $backend + ' %*' + "`r`n"
    [IO.File]::WriteAllText((Join-Path $taskOut "Play-$mode.cmd"), $launcher, [Text.Encoding]::ASCII)
}
Get-ChildItem -LiteralPath $taskOut -File | Get-FileHash -Algorithm SHA256 | Select-Object Hash,Path

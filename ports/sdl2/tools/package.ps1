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
$taskSdlLicense = [IO.File]::ReadAllText((Join-Path $env:LOCALAPPDATA 'Programs/SDL2/SDL2-2.32.10/LICENSE.txt'))
$taskAttribution = [IO.File]::ReadAllText((Join-Path $taskRoot 'ports/sdl2/NOTICES.txt'))
$taskLicenses = "SDL2`r`n====`r`n" + $taskSdlLicense + "`r`nUI methodology`r`n==============`r`n" + $taskAttribution
[IO.File]::WriteAllText((Join-Path $taskOut 'LICENSES.txt'), $taskLicenses, [Text.UTF8Encoding]::new($false))
foreach ($name in @('Play.cmd','Play-OpenGL.cmd','Play-Software.cmd','NOTICES.txt','SDL2-LICENSE.txt')) {
    # Remove only our known obsolete files when refreshing an existing package.
    Remove-Item -LiteralPath (Join-Path $taskOut $name) -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -LiteralPath $taskOut -File | Get-FileHash -Algorithm SHA256 | Select-Object Hash,Path

param([string]$PackageName = 'windows-playtest', [switch]$WithoutResources)
$ErrorActionPreference = 'Stop'
if ($PackageName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'PackageName must be a directory name' }
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$taskBuild = Join-Path $taskRoot 'build/sdl2/windows'
$taskOut = Join-Path $taskRoot "artifacts/sdl2/$PackageName"
$taskResources = @{
    'OLD_GOLD.RES' = 'originals/Old_Gold_3D/OLD_GOLD.RES'
    'PKR.RES' = 'originals/pkr/RUS/PKR.RES'
    'PKE.RES' = 'originals/pkr/ENG/PKE.RES'
}
if (!$WithoutResources) {
    foreach ($taskSource in $taskResources.Values) {
        if (!(Test-Path -LiteralPath (Join-Path $taskRoot $taskSource) -PathType Leaf)) {
            throw "Required RES file missing: $taskSource"
        }
    }
}
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
foreach ($name in @('OGDoK.exe','SDL2.dll')) {
    Copy-Item -LiteralPath (Join-Path $taskBuild $name) -Destination (Join-Path $taskOut $name) -Force
}
foreach ($taskName in $taskResources.Keys) {
    $taskDestination = Join-Path $taskOut $taskName
    if ($WithoutResources) {
        Remove-Item -LiteralPath $taskDestination -Force -ErrorAction SilentlyContinue
    } else {
        Copy-Item -LiteralPath (Join-Path $taskRoot $taskResources[$taskName]) -Destination $taskDestination -Force
    }
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'ports/sdl2/README.txt') -Destination (Join-Path $taskOut 'README.txt') -Force
$taskSdlLicense = [IO.File]::ReadAllText((Join-Path $env:LOCALAPPDATA 'Programs/SDL2/SDL2-2.32.10/LICENSE.txt'))
$taskAttribution = [IO.File]::ReadAllText((Join-Path $taskRoot 'ports/sdl2/NOTICES.txt'))
$taskAttribution += "`r`nxBR`r`n===`r`n" + [IO.File]::ReadAllText((Join-Path $taskRoot 'ports/sdl2/third_party/xbr-LICENSE.txt'))
$taskLicenses = "SDL2`r`n====`r`n" + $taskSdlLicense + "`r`nUI methodology`r`n==============`r`n" + $taskAttribution
[IO.File]::WriteAllText((Join-Path $taskOut 'LICENSES.txt'), $taskLicenses, [Text.UTF8Encoding]::new($false))
foreach ($name in @('README.md','dok_port.exe','Play.cmd','Play-OpenGL.cmd','Play-Software.cmd','NOTICES.txt','SDL2-LICENSE.txt')) {
    # Remove only our known obsolete files when refreshing an existing package.
    Remove-Item -LiteralPath (Join-Path $taskOut $name) -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -LiteralPath $taskOut -File | Get-FileHash -Algorithm SHA256 | Select-Object Hash,Path

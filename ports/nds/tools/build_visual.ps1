<#
.SYNOPSIS
Build the independent N06 ROM with the shared Windows BlocksDS installation.
.DESCRIPTION
Uses an MSYS2 UCRT64 login shell and /opt/wonderful/bin/wf-env. Environment
changes apply only to the child process. A present toolchain_versions.json is
checked against wf-pacman -Q before building. No SDK packages are installed.
.PARAMETER Clean
Remove only this target's fixed output files, then exit without building.
.PARAMETER Rebuild
Check the SDK, remove this target's fixed output files, then build again.
#>
[CmdletBinding()]
param(
    [string]$MsysRoot = (Join-Path $env:USERPROFILE '.local/msys64'),
    [switch]$Clean,
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Clean -and $Rebuild) {
    throw 'Choose either -Clean (remove outputs only) or -Rebuild (clean and build).'
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build/nds/n06'))
$lockPath = Join-Path $repoRoot 'ports/nds/toolchain_versions.json'
$reportPath = Join-Path $outputRoot 'build-report.json'
$outputNames = @(
    'dok_nds_visual.elf', 'dok_nds_visual.map', 'dok_nds_visual.nds',
    'obj/main.o', 'obj/main.d', 'obj/sha256.o', 'obj/sha256.d',
    'obj/projection.o', 'obj/projection.d',
    'obj/test_projection.o', 'obj/test_projection.d',
    'obj/projection_compile.o', 'obj/projection_compile.d',
    'build-report.json'
)

if (!(Test-Path -LiteralPath (Join-Path $repoRoot 'AGENTS.md') -PathType Leaf) -or
    !(Test-Path -LiteralPath (Join-Path $repoRoot 'ports/nds/Makefile.n06') -PathType Leaf)) {
    throw "The driver is not inside the expected repository: $repoRoot"
}

function Assert-OutputPaths {
    # Never traverse a build junction/symlink when writing or removing outputs.
    foreach ($relativeDirectory in @('build', 'build/nds', 'build/nds/n06', 'build/nds/n06/obj')) {
        $directory = Join-Path $repoRoot $relativeDirectory
        if (Test-Path -LiteralPath $directory) {
            $item = Get-Item -LiteralPath $directory -Force
            if (!$item.PSIsContainer -or
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Expected a real build directory, not a file/link: $directory"
            }
        }
    }
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build/nds/n06'))
    if ($outputRoot -ne $expectedRoot -or $outputRoot -eq [IO.Path]::GetPathRoot($outputRoot)) {
        throw 'The N06 output root failed validation.'
    }
    $prefix = $outputRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    foreach ($name in $outputNames) {
        $target = [IO.Path]::GetFullPath((Join-Path $outputRoot $name))
        if (!$target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Output escapes the fixed N06 directory: $target"
        }
        if (Test-Path -LiteralPath $target) {
            $item = Get-Item -LiteralPath $target -Force
            if ($item.PSIsContainer -or
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Expected a regular N06 output file: $target"
            }
        }
    }
}

function Remove-VisualProbeOutputs {
    Assert-OutputPaths
    foreach ($name in $outputNames) {
        $target = [IO.Path]::GetFullPath((Join-Path $outputRoot $name))
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            Remove-Item -LiteralPath $target -Force
            Write-Host "Removed $target"
        }
    }
}

if ($Clean) {
    Remove-VisualProbeOutputs
    return
}

$msysAbsolute = [IO.Path]::GetFullPath($MsysRoot)
$bashPath = Join-Path $msysAbsolute 'usr/bin/bash.exe'
$environmentScript = Join-Path $msysAbsolute 'opt/wonderful/bin/wf-env'
if (!(Test-Path -LiteralPath $bashPath -PathType Leaf)) {
    throw "MSYS2 bash not found: $bashPath"
}
if (!(Test-Path -LiteralPath $environmentScript -PathType Leaf)) {
    throw "Shared Wonderful environment script not found: $environmentScript"
}

function ConvertTo-BashLiteral([string]$Value) {
    # A single quote inside a POSIX single-quoted string is written as '\''.
    return "'" + $Value.Replace("'", "'\''") + "'"
}

function ConvertTo-MsysPath([string]$Path) {
    $normalized = $Path.Replace('\', '/')
    if ($normalized -match '^([A-Za-z]):/(.*)$') {
        return '/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2]
    }
    if ($normalized.StartsWith('//')) {
        return $normalized
    }
    throw "Expected an absolute Windows drive or UNC path: $Path"
}

function Invoke-Msys([string]$Script) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $bashPath
    # Script travels over stdin: no Windows argv quoting of shell source occurs.
    $info.Arguments = '--login -s'
    $info.WorkingDirectory = $repoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables['MSYSTEM'] = 'UCRT64'
    $info.EnvironmentVariables['CHERE_INVOKING'] = '1'
    $info.EnvironmentVariables['MSYS2_PATH_TYPE'] = 'minimal'
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.StandardInput.Write($Script.Replace("`r`n", "`n") + "`n")
        $process.StandardInput.Close()
        $process.WaitForExit()
        return [PSCustomObject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdoutTask.GetAwaiter().GetResult()
            Stderr = $stderrTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

# wf-env is an SDK-owned shell script, sourced only inside the child process.
$shellPrefix = "set -eo pipefail`nsource /opt/wonderful/bin/wf-env`n"
$query = Invoke-Msys ($shellPrefix + 'wf-pacman -Q')
if ($query.ExitCode -ne 0) {
    throw "wf-pacman -Q failed ($($query.ExitCode)):`n$($query.Stderr)`n$($query.Stdout)"
}
$installedPackages = @{}
foreach ($entry in ($query.Stdout -split "`r?`n")) {
    if ($entry -match '^(\S+)\s+(\S+)$') {
        $installedPackages[$Matches[1]] = $Matches[2]
    }
}
if ($installedPackages.Count -eq 0) {
    throw 'wf-pacman returned no recognizable installed packages.'
}

$lockVerified = $false
$lockHash = $null
if (Test-Path -LiteralPath $lockPath -PathType Leaf) {
    $lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($lock.schema_version -ne 1 -or !$lock.packages) {
        throw 'Unsupported or empty N06 toolchain_versions.json.'
    }
    $expectedPackages = @($lock.packages.PSObject.Properties)
    if ($expectedPackages.Count -eq 0) {
        throw 'The toolchain lock must list at least one package.'
    }
    foreach ($package in $expectedPackages) {
        if (!$installedPackages.ContainsKey($package.Name)) {
            throw "Missing locked SDK package: $($package.Name)"
        }
        if ($installedPackages[$package.Name] -cne [string]$package.Value) {
            throw "SDK package version mismatch: $($package.Name), expected $($package.Value), installed $($installedPackages[$package.Name])"
        }
    }
    $lockHash = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $lockVerified = $true
    Write-Host "Verified $($expectedPackages.Count) locked toolchain packages."
} else {
    throw 'toolchain_versions.json is required for the N06 visual probe.'
}

Assert-OutputPaths
if ($Rebuild) {
    Remove-VisualProbeOutputs
}
$repoLiteral = ConvertTo-BashLiteral (ConvertTo-MsysPath $repoRoot)
$build = Invoke-Msys ($shellPrefix + "cd $repoLiteral`nmake -C ports/nds -f Makefile.n06 all")
if ($build.Stdout) { [Console]::Write($build.Stdout) }
if ($build.Stderr) { [Console]::Error.Write($build.Stderr) }
if ($build.ExitCode -ne 0) {
    throw "N06 make failed with exit code $($build.ExitCode)."
}

$artifacts = @()
foreach ($name in @('dok_nds_visual.nds', 'dok_nds_visual.elf', 'dok_nds_visual.map')) {
    $artifactPath = Join-Path $outputRoot $name
    if (!(Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Build succeeded but an expected artifact is missing: $artifactPath"
    }
    $item = Get-Item -LiteralPath $artifactPath
    $artifacts += [ordered]@{
        path = 'build/nds/n06/' + $name
        bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$packageReport = [ordered]@{}
foreach ($name in ($installedPackages.Keys | Sort-Object)) {
    $packageReport[$name] = $installedPackages[$name]
}
$report = [ordered]@{
    schema_version = 1
    target = 'N06 visual probe; DS mode'
    command = 'MSYSTEM=UCRT64 bash --login; source /opt/wonderful/bin/wf-env; make -C ports/nds -f Makefile.n06 all'
    toolchain_lock_verified = $lockVerified
    toolchain_lock_sha256 = $lockHash
    installed_packages = $packageReport
    artifacts = $artifacts
}
$json = $report | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($reportPath, $json + "`n", (New-Object Text.UTF8Encoding($false)))
Write-Host "N06 build report: $reportPath"


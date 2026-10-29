<#
.SYNOPSIS
Build the independent N11 ROM with the shared Windows BlocksDS installation.
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
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build/nds/n11'))
$lockPath = Join-Path $repoRoot 'ports/nds/toolchain_versions.json'
$reportPath = Join-Path $outputRoot 'build-report.json'
$outputNames = @(
    'dok_nds_interactions.elf',
    'dok_nds_interactions.map',
    'dok_nds_interactions.nds',
    'obj/main.o',
    'obj/main.d',
    'obj/main.su',
    'obj/contracts.o',
    'obj/contracts.d',
    'obj/contracts.su',
    'obj/input.o',
    'obj/input.d',
    'obj/input.su',
    'obj/controls.o',
    'obj/controls.d',
    'obj/controls.su',
    'obj/motion.o',
    'obj/motion.d',
    'obj/motion.su',
    'obj/camera.o',
    'obj/camera.d',
    'obj/camera.su',
    'obj/time.o',
    'obj/time.d',
    'obj/time.su',
    'obj/events.o',
    'obj/events.d',
    'obj/events.su',
    'obj/clock.o',
    'obj/clock.d',
    'obj/clock.su',
    'obj/nds_input.o',
    'obj/nds_input.d',
    'obj/nds_input.su',
    'obj/render_backend.o',
    'obj/render_backend.d',
    'obj/render_backend.su',
    'obj/scene.o',
    'obj/scene.d',
    'obj/scene.su',
    'obj/materials.o',
    'obj/materials.d',
    'obj/materials.su',
    'obj/geometry.o',
    'obj/geometry.d',
    'obj/geometry.su',
    'obj/assets.o',
    'obj/assets.d',
    'obj/assets.su',
    'obj/memory.o',
    'obj/memory.d',
    'obj/memory.su',
    'obj/sha256.o',
    'obj/sha256.d',
    'obj/sha256.su',
    'obj/world.o',
    'obj/world.d',
    'obj/world.su',
    'obj/balance.o',
    'obj/balance.d',
    'obj/balance.su',
    'obj/file_reader.o',
    'obj/file_reader.d',
    'obj/file_reader.su',
    'obj/collision.o',
    'obj/collision.d',
    'obj/collision.su',
    'obj/interactions.o',
    'obj/interactions.d',
    'obj/interactions.su',
    'obj/interactions_move.o',
    'obj/interactions_move.d',
    'obj/interactions_move.su',
    'obj/door_render.o',
    'obj/door_render.d',
    'obj/door_render.su',
    'obj/interaction_cases.o',
    'obj/interaction_cases.d',
    'obj/interaction_cases.su',
    'obj/door_render_cases.o',
    'obj/door_render_cases.d',
    'obj/door_render_cases.su',
    'obj/test_interactions.o',
    'obj/test_interactions.d',
    'obj/test_interactions.su',
    'obj/test_door_render.o',
    'obj/test_door_render.d',
    'obj/test_door_render.su',
    'obj/interaction_dump.o',
    'obj/interaction_dump.d',
    'obj/interaction_dump.su',
    'obj/interactions_compile.o',
    'obj/interactions_compile.d',
    'obj/interactions_compile.su',
    'obj/interaction_router_cases.o',
    'obj/interaction_router_cases.d',
    'obj/interaction_router_cases.su',
    'obj/test_interaction_router.o',
    'obj/test_interaction_router.d',
    'obj/test_interaction_router.su',
    'build-report.json'
)

if (!(Test-Path -LiteralPath (Join-Path $repoRoot 'AGENTS.md') -PathType Leaf) -or
    !(Test-Path -LiteralPath (Join-Path $repoRoot 'ports/nds/Makefile.n11') -PathType Leaf)) {
    throw "The driver is not inside the expected repository: $repoRoot"
}

function Assert-OutputPaths {
    # Never traverse a build junction/symlink when writing or removing outputs.
    foreach ($relativeDirectory in @('build', 'build/nds', 'build/nds/n11', 'build/nds/n11/obj')) {
        $directory = Join-Path $repoRoot $relativeDirectory
        if (Test-Path -LiteralPath $directory) {
            $item = Get-Item -LiteralPath $directory -Force
            if (!$item.PSIsContainer -or
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Expected a real build directory, not a file/link: $directory"
            }
        }
    }
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build/nds/n11'))
    if ($outputRoot -ne $expectedRoot -or $outputRoot -eq [IO.Path]::GetPathRoot($outputRoot)) {
        throw 'The N11 output root failed validation.'
    }
    $prefix = $outputRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    foreach ($name in $outputNames) {
        $target = [IO.Path]::GetFullPath((Join-Path $outputRoot $name))
        if (!$target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Output escapes the fixed N11 directory: $target"
        }
        if (Test-Path -LiteralPath $target) {
            $item = Get-Item -LiteralPath $target -Force
            if ($item.PSIsContainer -or $item.LinkType -eq 'HardLink' -or
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Expected a regular N11 output file: $target"
            }
        }
    }
}

function Remove-InteractionsProbeOutputs {
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
    Remove-InteractionsProbeOutputs
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
        throw 'Unsupported or empty N11 toolchain_versions.json.'
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
    throw 'toolchain_versions.json is required for the N11 interactions probe.'
}

Assert-OutputPaths
if ($Rebuild) {
    Remove-InteractionsProbeOutputs
}
$repoLiteral = ConvertTo-BashLiteral (ConvertTo-MsysPath $repoRoot)
$build = Invoke-Msys ($shellPrefix + "cd $repoLiteral`nmake -C ports/nds -f Makefile.n11 all")
if ($build.Stdout) { [Console]::Write($build.Stdout) }
if ($build.Stderr) { [Console]::Error.Write($build.Stderr) }
if ($build.ExitCode -ne 0) {
    throw "N11 make failed with exit code $($build.ExitCode)."
}

$artifacts = @()
foreach ($name in @('dok_nds_interactions.nds', 'dok_nds_interactions.elf', 'dok_nds_interactions.map')) {
    $artifactPath = Join-Path $outputRoot $name
    if (!(Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Build succeeded but an expected artifact is missing: $artifactPath"
    }
    $item = Get-Item -LiteralPath $artifactPath
    $artifacts += [ordered]@{
        path = 'build/nds/n11/' + $name
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
    target = 'N11 interactions probe; DS mode'
    command = 'MSYSTEM=UCRT64 bash --login; source /opt/wonderful/bin/wf-env; make -C ports/nds -f Makefile.n11 all'
    toolchain_lock_verified = $lockVerified
    toolchain_lock_sha256 = $lockHash
    installed_packages = $packageReport
    artifacts = $artifacts
}
$json = $report | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($reportPath, $json + "`n", (New-Object Text.UTF8Encoding($false)))
Write-Host "N11 build report: $reportPath"


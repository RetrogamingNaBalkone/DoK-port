<#
.SYNOPSIS
Verify two clean N02 builds in the same output directory without copying ROMs.
.DESCRIPTION
Requires toolchain_versions.json. Runs build_diag.ps1 -Rebuild twice, verifies
each new build report against the files on disk, then compares the three
artifact hashes/sizes and installed package versions. Only compact JSON
evidence is retained; the output directory contains the second build.
#>
[CmdletBinding()]
param(
    [string]$MsysRoot = (Join-Path $env:USERPROFILE '.local/msys64')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$driverPath = Join-Path $PSScriptRoot 'build_diag.ps1'
$lockPath = Join-Path $repoRoot 'ports/nds/toolchain_versions.json'
$buildReportPath = Join-Path $repoRoot 'build/nds/n02/build-report.json'
$evidencePath = Join-Path $repoRoot 'research/evidence/nds/n02/rebuild-check.json'
$expectedPaths = @(
    'build/nds/n02/dok_nds_diag.nds',
    'build/nds/n02/dok_nds_diag.elf',
    'build/nds/n02/dok_nds_diag.map'
)
$result = [ordered]@{
    schema_version = 1
    target = 'N02 diagnostic clean-build reproducibility'
    success = $false
    status = 'in_progress'
    command = 'ports/nds/tools/check_diag_rebuild.ps1 (build_diag.ps1 -Rebuild twice)'
    toolchain_lock_sha256 = $null
    builds = @()
    artifacts_match = $false
    package_versions_match = $false
    error = $null
}

function Write-Evidence {
    $directory = Split-Path -Parent $evidencePath
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $json = $result | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText($evidencePath, $json + "`n", (New-Object Text.UTF8Encoding($false)))
}

function Read-VerifiedBuildReport([int]$BuildNumber, [string]$LockHash) {
    if (!(Test-Path -LiteralPath $buildReportPath -PathType Leaf)) {
        throw "Build $BuildNumber did not create a build-report.json."
    }
    $report = Get-Content -LiteralPath $buildReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($report.schema_version -ne 1 -or $report.toolchain_lock_verified -ne $true) {
        throw "Build $BuildNumber did not verify the required toolchain lock."
    }
    $currentLockHash = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($currentLockHash -cne $LockHash -or $report.toolchain_lock_sha256 -cne $LockHash) {
        throw "The toolchain lock changed during build $BuildNumber or differs from its report."
    }
    $packages = [ordered]@{}
    foreach ($package in ($report.installed_packages.PSObject.Properties | Sort-Object Name)) {
        $packages[$package.Name] = [string]$package.Value
    }
    if ($packages.Count -eq 0) {
        throw "Build $BuildNumber reported no installed toolchain packages."
    }
    $reportedArtifacts = @($report.artifacts)
    if ($reportedArtifacts.Count -ne $expectedPaths.Count) {
        throw "Build $BuildNumber must report exactly the ROM, ELF and map."
    }
    $artifacts = @()
    foreach ($path in $expectedPaths) {
        $matchesForPath = @($reportedArtifacts | Where-Object { $_.path -ceq $path })
        if ($matchesForPath.Count -ne 1) {
            throw "Build $BuildNumber has a missing/duplicate artifact: $path"
        }
        $reported = $matchesForPath[0]
        $artifactPath = Join-Path $repoRoot $path
        if (!(Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Build $BuildNumber artifact is absent: $path"
        }
        $bytes = (Get-Item -LiteralPath $artifactPath).Length
        $hash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($bytes -le 0 -or $reported.bytes -ne $bytes -or $reported.sha256 -cne $hash) {
            throw "Build $BuildNumber report does not match the current artifact: $path"
        }
        $artifacts += [ordered]@{ path = $path; bytes = $bytes; sha256 = $hash }
    }
    return [ordered]@{
        number = $BuildNumber
        toolchain_lock_verified = $true
        toolchain_lock_sha256 = $LockHash
        installed_packages = $packages
        artifacts = $artifacts
    }
}

try {
    # Overwrite any former success before starting this attempt.
    Write-Evidence
    if (!(Test-Path -LiteralPath $driverPath -PathType Leaf)) {
        throw "N02 build driver is missing: $driverPath"
    }
    if (!(Test-Path -LiteralPath $lockPath -PathType Leaf)) {
        throw 'Clean-build acceptance requires ports/nds/toolchain_versions.json.'
    }
    $lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($lock.schema_version -ne 1 -or !$lock.packages -or
        @($lock.packages.PSObject.Properties).Count -eq 0) {
        throw 'Clean-build acceptance requires a nonempty schema_version 1 package lock.'
    }
    $lockHash = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $result.toolchain_lock_sha256 = $lockHash
    for ($number = 1; $number -le 2; ++$number) {
        Write-Host "N02 clean build $number / 2"
        # The driver may fail its SDK preflight before cleaning. Remove the old
        # report first so it cannot serve as evidence for a failed new build.
        if (Test-Path -LiteralPath $buildReportPath) {
            Remove-Item -LiteralPath $buildReportPath -Force
        }
        $global:LASTEXITCODE = 0
        & $driverPath -MsysRoot $MsysRoot -Rebuild
        if ($LASTEXITCODE -ne 0) {
            throw "Build $number returned exit code $LASTEXITCODE."
        }
        $result.builds += Read-VerifiedBuildReport $number $lockHash
        Write-Evidence
    }

    $first = $result.builds[0]
    $second = $result.builds[1]
    $artifactsMatch = $true
    for ($i = 0; $i -lt $expectedPaths.Count; ++$i) {
        if ($first.artifacts[$i].sha256 -cne $second.artifacts[$i].sha256 -or
            $first.artifacts[$i].bytes -ne $second.artifacts[$i].bytes) {
            $artifactsMatch = $false
        }
    }
    $result.artifacts_match = $artifactsMatch
    $firstPackages = $first.installed_packages | ConvertTo-Json -Compress
    $secondPackages = $second.installed_packages | ConvertTo-Json -Compress
    $result.package_versions_match = $firstPackages -ceq $secondPackages
    if (!$result.artifacts_match -or !$result.package_versions_match) {
        throw 'The two clean builds differ in artifact hashes/sizes or package versions.'
    }
    $result.success = $true
    $result.status = 'passed'
    Write-Evidence
    Write-Host "Two locked clean builds match: $evidencePath"
    exit 0
} catch {
    $result.success = $false
    $result.status = 'failed'
    $result.error = $_.Exception.Message
    Write-Evidence
    [Console]::Error.WriteLine("N02 reproducibility check failed: $($result.error)")
    exit 1
}

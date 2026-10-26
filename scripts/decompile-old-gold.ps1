param(
    [string]$GhidraHome = 'C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome = 'C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1',
    [string]$ProjectDirectory = 'build/ghidra/old-gold-full',
    [string]$OutputDirectory = 'decompiled/old-gold'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskSource = "$taskRoot/originals/Old_Gold_3D"
$taskProject = Join-Path $taskRoot $ProjectDirectory
$taskOutput = Join-Path $taskRoot $OutputDirectory
$taskExpected = @{
    'OG.EXE' = '7965e1d67128f196fbcf2bc449d97197f355c6b81cf043608fc9e01103ea043f'
    'OLD_GOLD.RES' = 'e376855167b64a7190ec2da0ba9bd1da129ba178c4033aebadebca3f08b2be25'
}
$taskBefore = @{}
foreach ($taskName in $taskExpected.Keys) {
    $taskBefore[$taskName] = (Get-FileHash -LiteralPath "$taskSource/$taskName" -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($taskBefore[$taskName] -ne $taskExpected[$taskName]) { throw "Unexpected sample: $taskName" }
}
New-Item -ItemType Directory -Force $taskProject, $taskOutput | Out-Null
$taskPreviousLocal = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = "$taskRoot/build/ghidra-local"
    $taskArguments = @(
        '-Djava.system.class.loader=ghidra.GhidraClassLoader',
        "-Duser.home=$taskRoot/build/ghidra-home",
        "-Dapplication.settingsdir=$taskRoot/build/ghidra-settings",
        '-Djava.awt.headless=true', '-Xmx3G', '-cp',
        "$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
        'ghidra.Ghidra', 'ghidra.app.util.headless.AnalyzeHeadless', $taskProject, 'OldGold'
    )
    if (Test-Path "$taskProject/OldGold.gpr") {
        $taskArguments += @('-process', 'OG.EXE', '-noanalysis')
    } else {
        $taskArguments += @('-import', "$taskSource/OG.EXE", '-processor', 'x86:LE:16:Real Mode', '-analysisTimeoutPerFile', '600', '-preScript', 'PrepareOldGold.java')
    }
    $taskArguments += @('-scriptPath', $PSScriptRoot, '-postScript', 'InspectOldGoldContext.java', '-postScript', 'ExportOldGold.java',
        $taskOutput, "$taskRoot/analysis/old-gold/seeds.tsv")
    & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
    if ($LASTEXITCODE -ne 0) { throw "Ghidra failed: $taskOutput/ghidra.log" }
    if (-not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch 'OLD_GOLD_EXPORT_COMPLETE' -Quiet)) { throw 'Incomplete export' }
    if (Select-String -LiteralPath "$taskOutput/ghidra.log" -Pattern '^ERROR' -Quiet) { throw 'Ghidra errors' }
    $taskAfter = @{}
    foreach ($taskName in $taskExpected.Keys) {
        $taskAfter[$taskName] = (Get-FileHash -LiteralPath "$taskSource/$taskName" -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($taskAfter[$taskName] -ne $taskBefore[$taskName]) { throw "Original changed: $taskName" }
    }
    [ordered]@{
        sample='originals/Old_Gold_3D/OG.EXE'; sha256_before=$taskBefore; sha256_after=$taskAfter
        project="$ProjectDirectory/OldGold.gpr"; export=$OutputDirectory
        ghidra_version='12.1.2'; language='x86:LE:16:Real Mode'; compiler='default'
        scripts=@('scripts/PrepareOldGold.java','scripts/InspectOldGoldContext.java','scripts/ExportOldGold.java','scripts/decompile-old-gold.ps1','analysis/old-gold/seeds.tsv') | ForEach-Object {
            @{path=$_;sha256=(Get-FileHash -LiteralPath "$taskRoot/$_").Hash.ToLowerInvariant()}
        }
    } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 "$taskOutput/manifest.json"
    Get-Content "$taskOutput/summary.txt"
} finally { $env:LOCALAPPDATA = $taskPreviousLocal }

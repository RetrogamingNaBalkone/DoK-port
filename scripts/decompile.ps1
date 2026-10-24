param(
    [ValidateSet('ru','en','all')][string]$Language = 'all',
    [string]$GhidraHome = 'C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome = 'C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskProfiles = @{
    ru = @{ File='RUS/PKR.EXE'; Hash='12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28' }
    en = @{ File='ENG/PKE.EXE'; Hash='cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b' }
}
$taskLanguages = if ($Language -eq 'all') { @('ru','en') } else { @($Language) }
$taskPreviousLocalAppData = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = "$taskRoot/build/ghidra-local"
    foreach ($taskLang in $taskLanguages) {
        $taskProfile = $taskProfiles[$taskLang]
        $taskFile = Join-Path "$taskRoot/originals/pkr" $taskProfile.File
        if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskProfile.Hash) { throw "Unexpected reference: $taskFile" }
        $taskProject = "$taskRoot/build/ghidra/$taskLang"
        $taskOutput = "$taskRoot/decompiled/$taskLang"
        New-Item -ItemType Directory -Force $taskProject, $taskOutput | Out-Null
        $taskArguments = @(
            '-Djava.system.class.loader=ghidra.GhidraClassLoader',
            "-Duser.home=$taskRoot/build/ghidra-home",
            "-Dapplication.settingsdir=$taskRoot/build/ghidra-settings",
            '-Djava.awt.headless=true', '-Xmx3G', '-cp',
            "$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
            'ghidra.Ghidra', 'ghidra.app.util.headless.AnalyzeHeadless',
            $taskProject, 'Kremlin'
        )
        if (Test-Path "$taskProject/Kremlin.gpr") {
            $taskArguments += @('-process', (Split-Path $taskFile -Leaf), '-noanalysis')
        } else {
            $taskArguments += @('-import', $taskFile, '-processor', 'x86:LE:16:Protected Mode', '-analysisTimeoutPerFile', '600')
        }
        $taskArguments += @('-scriptPath', $PSScriptRoot, '-postScript', 'ExportKremlin.java', $taskOutput, "$taskRoot/analysis/seeds-$taskLang.tsv")
        & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
        if ($LASTEXITCODE -ne 0) { throw "Ghidra exited with $LASTEXITCODE; see $taskOutput/ghidra.log" }
        if (-not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch 'KREMLIN_EXPORT_COMPLETE' -Quiet)) { throw "Incomplete export; see $taskOutput/ghidra.log" }
        if (Select-String -LiteralPath "$taskOutput/ghidra.log" -Pattern '^ERROR' -Quiet) { throw "Ghidra errors; see $taskOutput/ghidra.log" }
        if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskProfile.Hash) { throw 'Reference changed' }
        Get-Content "$taskOutput/summary.txt"
    }
} finally { $env:LOCALAPPDATA = $taskPreviousLocalAppData }

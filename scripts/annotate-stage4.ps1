param(
    [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPreviousLocal=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA="$taskRoot/build/ghidra-local"
    foreach ($taskLang in @('ru','en')) {
        $taskInventory=Get-Content "$taskRoot/analysis/inventory-$taskLang.json" -Raw | ConvertFrom-Json
        $taskFile=Join-Path $taskRoot $taskInventory.file
        if ((Get-FileHash -LiteralPath $taskFile).Hash.ToLowerInvariant() -ne $taskInventory.sha256) { throw 'Reference hash mismatch' }
        $taskOutput="$taskRoot/build/stage4/$taskLang"
        New-Item -ItemType Directory -Force $taskOutput | Out-Null
        $taskArguments=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',
            "-Duser.home=$taskRoot/build/ghidra-home", "-Dapplication.settingsdir=$taskRoot/build/ghidra-settings",
            '-Djava.awt.headless=true','-Xmx3G','-cp',"$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
            'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',"$taskRoot/build/ghidra/$taskLang",'Kremlin',
            '-process',(Split-Path $taskFile -Leaf),'-noanalysis','-scriptPath',"$taskRoot/scripts",'-postScript',
            'AnnotateSounds.java',$taskLang,$taskOutput)
        & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
        if ($LASTEXITCODE -ne 0 -or (Select-String -LiteralPath "$taskOutput/ghidra.log" -Pattern '^ERROR' -Quiet) -or
            -not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch -Pattern 'KREMLIN_STAGE4_COMPLETE' -Quiet)) { throw "Stage 4 Ghidra failed: $taskLang" }
        if ((Get-FileHash -LiteralPath $taskFile).Hash.ToLowerInvariant() -ne $taskInventory.sha256) { throw 'Reference changed' }
        Get-Content "$taskOutput/roles.tsv"
    }
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}

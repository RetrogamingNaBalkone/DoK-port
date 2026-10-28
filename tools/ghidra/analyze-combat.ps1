param(
    [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1',
    [ValidateSet('both','ru','en')][string]$Language='both'
)
$ErrorActionPreference='Stop'
$taskRoot=([IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../../")))
$taskPreviousLocal=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
    $taskLanguages=if($Language -eq 'both') {@('ru','en')} else {@($Language)}
    foreach($taskLang in $taskLanguages) {
        $taskInventory=Get-Content "$taskRoot/analysis/originals/inventory-$taskLang.json" -Raw | ConvertFrom-Json
        $taskFile=Join-Path $taskRoot $taskInventory.file
        $taskBefore=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if($taskBefore -ne $taskInventory.sha256) {throw 'Reference hash mismatch'}
        $taskOutput="$taskRoot/research/evidence/combat/$taskLang"
        New-Item -ItemType Directory -Force $taskOutput | Out-Null
        $taskArguments=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',
            "-Duser.home=$taskRoot/research/ghidra/home","-Dapplication.settingsdir=$taskRoot/research/ghidra/settings",
            '-Djava.awt.headless=true','-Xmx3G','-cp',"$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
            'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',"$taskRoot/research/ghidra/projects/$taskLang",'Kremlin',
            '-process',(Split-Path $taskFile -Leaf),'-noanalysis','-readOnly','-scriptPath',$PSScriptRoot,'-postScript',
            'InspectCombat.java',$taskLang,$taskOutput)
        & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
        if($LASTEXITCODE -ne 0 -or (Select-String -LiteralPath "$taskOutput/ghidra.log" -Pattern '^ERROR' -Quiet) -or
           -not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch -Pattern 'KREMLIN_COMBAT_COMPLETE' -Quiet)) {
            throw "G13 Ghidra failed: $taskLang; inspect $taskOutput/ghidra.log"
        }
        $taskAfter=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if($taskAfter -ne $taskBefore) {throw 'Original changed during analysis'}
        [ordered]@{language=$taskLang;sample=$taskInventory.file;sha256_before=$taskBefore;sha256_after=$taskAfter;
            project="research/ghidra/projects/$taskLang/Kremlin.gpr";script='tools/ghidra/InspectCombat.java';
            script_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot/InspectCombat.java" -Algorithm SHA256).Hash.ToLowerInvariant();
            driver_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();
            address_system='Ghidra NE loader; not file offset or runtime DPMI selector'} |
            ConvertTo-Json | Set-Content -Encoding utf8 "$taskOutput/analysis-manifest.json"
        Write-Output "Combat evidence: $taskOutput"
    }
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}








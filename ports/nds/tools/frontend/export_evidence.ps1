param(
    [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1'
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$taskPreviousLocal=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
    foreach($taskLang in @('ru','en')) {
        $taskInventory=Get-Content -Encoding utf8 "$taskRoot/analysis/originals/inventory-$taskLang.json" -Raw | ConvertFrom-Json
        $taskFile=Join-Path $taskRoot $taskInventory.file
        $taskBefore=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if($taskBefore -ne $taskInventory.sha256) {throw 'Reference hash mismatch'}
        $taskOutput="$taskRoot/research/evidence/nds/n21/original/$taskLang"
        New-Item -ItemType Directory -Force $taskOutput | Out-Null
        $taskArguments=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',
            "-Duser.home=$taskRoot/research/ghidra/home","-Dapplication.settingsdir=$taskRoot/research/ghidra/settings",
            '-Djava.awt.headless=true','-Xmx3G','-cp',"$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
            'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',"$taskRoot/research/ghidra/projects/$taskLang",'Kremlin',
            '-process',(Split-Path $taskFile -Leaf),'-readOnly','-noanalysis','-scriptPath',$PSScriptRoot,'-postScript',
            'ExportFrontend.java',$taskLang,$taskOutput)
        & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
        if($LASTEXITCODE -ne 0 -or (Select-String -LiteralPath "$taskOutput/ghidra.log" -Pattern '^ERROR' -Quiet) -or
           -not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch -Pattern 'N21_FRONTEND_COMPLETE' -Quiet)) {
            throw "N21 Ghidra export failed: $taskLang; inspect $taskOutput/ghidra.log"
        }
        $taskAfter=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if($taskAfter -ne $taskBefore) {throw 'Original changed during analysis'}
        $taskFiles=[ordered]@{}
        foreach($taskExport in (Get-ChildItem -LiteralPath $taskOutput -File | Where-Object Extension -In '.bin','.asm','.c' | Sort-Object Name)) {
            $taskFiles[$taskExport.Name]=(Get-FileHash -LiteralPath $taskExport.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        [ordered]@{task='N21';language=$taskLang;sample=$taskInventory.file;sha256_before=$taskBefore;sha256_after=$taskAfter;
            project="research/ghidra/projects/$taskLang/Kremlin.gpr";read_only=$true;
            script='ports/nds/tools/frontend/ExportFrontend.java';script_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot/ExportFrontend.java" -Algorithm SHA256).Hash.ToLowerInvariant();
            exports=$taskFiles;driver='ports/nds/tools/frontend/export_evidence.ps1';
            driver_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();
            address_system='Ghidra NE loader; not file offset, NE index or runtime DPMI selector'} |
            ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$taskOutput/analysis-manifest.json"
        Write-Output "N21 read-only Ghidra export passed: $taskLang"
    }
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}

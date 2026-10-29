param(
    [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1'
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$taskPreviousLocal=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
    foreach($taskLang in @('ru','en')) {
        $taskInventory=Get-Content -Encoding utf8 "$taskRoot/analysis/originals/inventory-$taskLang.json" -Raw | ConvertFrom-Json
        $taskFile=Join-Path $taskRoot $taskInventory.file
        $taskBefore=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if($taskBefore -ne $taskInventory.sha256) {throw 'Reference hash mismatch'}
        $taskOutput="$taskRoot/research/evidence/nds/n17/compiler/$taskLang"
        New-Item -ItemType Directory -Force $taskOutput | Out-Null
        $taskArguments=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',
            "-Duser.home=$taskRoot/research/ghidra/home","-Dapplication.settingsdir=$taskRoot/research/ghidra/settings",
            '-Djava.awt.headless=true','-Xmx3G','-cp',"$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
            'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',"$taskRoot/research/ghidra/projects/$taskLang",'Kremlin',
            '-process',(Split-Path $taskFile -Leaf),'-readOnly','-noanalysis','-scriptPath',"$taskRoot/tools/ghidra",'-postScript',
            'ExportNdsCompiler.java',$taskLang,$taskOutput)
        & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
        if($LASTEXITCODE -ne 0 -or (Select-String -LiteralPath "$taskOutput/ghidra.log" -Pattern '^ERROR' -Quiet) -or
           -not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch -Pattern 'NDS_COMPILER_EXPORT_COMPLETE' -Quiet)) {
            throw "N17 Ghidra export failed: $taskLang"
        }
        $taskAfter=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if($taskAfter -ne $taskBefore) {throw 'Original changed during analysis'}
        $taskBinaries=[ordered]@{}
        foreach($taskBinary in (Get-ChildItem -LiteralPath $taskOutput -Filter '*.bin' | Sort-Object Name)) {
            $taskBinaries[$taskBinary.BaseName]=(Get-FileHash -LiteralPath $taskBinary.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        [ordered]@{language=$taskLang;sample=$taskInventory.file;sha256_before=$taskBefore;sha256_after=$taskAfter;
            project="research/ghidra/projects/$taskLang/Kremlin.gpr";read_only=$true;
            script='tools/ghidra/ExportNdsCompiler.java';script_sha256=(Get-FileHash -LiteralPath "$taskRoot/tools/ghidra/ExportNdsCompiler.java" -Algorithm SHA256).Hash.ToLowerInvariant();
            binary_sha256=$taskBinaries;
            driver='ports/nds/tools/export_combat_compiler.ps1';driver_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();
            address_system='Ghidra NE loader; not file offset or runtime DPMI selector'} |
            ConvertTo-Json | Set-Content -Encoding utf8 "$taskOutput/analysis-manifest.json"
        Write-Output "N17 read-only Ghidra export passed: $taskLang"
    }
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}

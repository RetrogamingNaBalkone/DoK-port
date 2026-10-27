param(
    [switch]$Weapons,
    [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPreviousLocal=$env:LOCALAPPDATA
$taskAddresses=@{
    ru=@('1068:02f5','1068:0a85','1068:0eb1','1068:136a','1068:3747','1068:37cf','1068:3845','1068:3eca','1068:3f80','1098:30c1','1130:026f')
    en=@('1020:07cf','1020:0f8f','1020:3eb0','1020:3f38','1020:3fae','1020:4633','1020:46e9')
}
$taskEvidenceName='enemy-fields'
if($Weapons) {
    $taskEvidenceName='weapon-fields'
    $taskAddresses=@{
        ru=@('1068:1b10','1068:1cc2','1068:136a','1068:2368','1068:2547','1068:2669','1068:26e8','1068:2c7c','1068:2d71','1068:2db9','1068:2127')
        en=@('1020:20c4','1020:2276','1020:1874','1020:2960','1020:2bea','1020:2d8b','1020:331f','1020:3414','1020:345c')
    }
}
$taskHashes=@{}
try {
    $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
    foreach($taskLang in @('ru','en')) {
        $taskInventory=Get-Content "$taskRoot/analysis/inventory-$taskLang.json" -Raw | ConvertFrom-Json
        foreach($taskSample in @($taskInventory,$taskInventory.resource)) {
            $taskHash=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskSample.file)).Hash.ToLowerInvariant()
            if($taskHash -ne $taskSample.sha256) {throw 'Reference hash mismatch'}
            $taskHashes[$taskSample.file]=$taskHash
        }
        $taskOutput="$taskRoot/build/$taskEvidenceName/$taskLang"
        New-Item -ItemType Directory -Force $taskOutput | Out-Null
        $taskArguments=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',
            "-Duser.home=$taskRoot/research/ghidra/home", "-Dapplication.settingsdir=$taskRoot/research/ghidra/settings",
            '-Djava.awt.headless=true','-Xmx3G','-cp',"$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
            'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',"$taskRoot/research/ghidra/projects/$taskLang",'Kremlin',
            '-process',(Split-Path $taskInventory.file -Leaf),'-noanalysis','-readOnly',
            '-scriptPath',"$taskRoot/scripts",'-postScript','InspectEnemyFields.java',$taskOutput)
        $taskArguments += $taskAddresses[$taskLang]
        & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
        if($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch -Pattern 'ENEMY_FIELDS_COMPLETE' -Quiet)) {throw "Enemy evidence export failed: $taskLang"}
    }
    foreach($taskPath in @($taskHashes.Keys)) {
        if((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPath)).Hash.ToLowerInvariant() -ne $taskHashes[$taskPath]) {throw 'Reference changed'}
    }
    $taskHashes | ConvertTo-Json | Set-Content -Encoding UTF8 "$taskRoot/build/$taskEvidenceName/verified-hashes.json"
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}

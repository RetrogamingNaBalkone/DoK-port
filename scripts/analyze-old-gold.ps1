param(
 [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
 [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPreviousLocal=$env:LOCALAPPDATA
try {
 $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
 foreach($taskVersion in @('Old_Gold_Old_Version','Old_Gold_3D')) {
  $taskFile="$taskRoot/originals/$taskVersion/OG.EXE"
  $taskOutput="$taskRoot/research/evidence/old-gold/$taskVersion"
  New-Item -ItemType Directory -Force $taskOutput | Out-Null
  $taskBefore=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
  $taskArguments=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',
   "-Duser.home=$taskRoot/research/ghidra/home","-Dapplication.settingsdir=$taskRoot/research/ghidra/settings",
   '-Djava.awt.headless=true','-Xmx3G','-cp',"$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar",
   'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',$taskOutput,'OldGold',
   '-import',$taskFile,'-overwrite','-scriptPath',"$taskRoot/scripts",'-postScript','InspectOldGold.java',$taskOutput)
  & "$JavaHome/bin/java.exe" @taskArguments *> "$taskOutput/ghidra.log"
  if($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath "$taskOutput/ghidra.log" -SimpleMatch 'OLD_GOLD_INSPECTION_COMPLETE' -Quiet)) {throw "Ghidra failed: $taskOutput/ghidra.log"}
  $taskAfter=(Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash.ToLowerInvariant()
  if($taskBefore -ne $taskAfter){throw 'Original changed'}
  [ordered]@{sample="originals/$taskVersion/OG.EXE";sha256_before=$taskBefore;sha256_after=$taskAfter;
   project="research/evidence/old-gold/$taskVersion/OldGold.gpr";script='scripts/InspectOldGold.java';
   script_sha256=(Get-FileHash "$PSScriptRoot/InspectOldGold.java").Hash.ToLowerInvariant();
   driver_sha256=(Get-FileHash $PSCommandPath).Hash.ToLowerInvariant()} |
   ConvertTo-Json | Set-Content -Encoding UTF8 "$taskOutput/manifest.json"
  Write-Output "Ghidra evidence: $taskOutput"
 }
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}

param(
    [string]$GhidraHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC',
    [string]$JavaHome='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1',
    [string]$OutputDirectory='research/evidence/source-port/sp04-level-audit-20261104'
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOut=Join-Path $taskRoot $OutputDirectory
$taskExpected=@{
 'originals/Old_Gold_3D/OG.EXE'='7965e1d67128f196fbcf2bc449d97197f355c6b81cf043608fc9e01103ea043f'
 'originals/Old_Gold_3D/OLD_GOLD.RES'='e376855167b64a7190ec2da0ba9bd1da129ba178c4033aebadebca3f08b2be25'
}
$taskBefore=@{}
foreach($taskPath in $taskExpected.Keys){
 $taskHash=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPath)).Hash.ToLowerInvariant()
 if($taskHash -ne $taskExpected[$taskPath]){throw "Unexpected sample $taskPath"}
 $taskBefore[$taskPath]=$taskHash
}
New-Item -ItemType Directory -Force $taskOut | Out-Null
$taskPreviousLocal=$env:LOCALAPPDATA
try{
 $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
 & "$JavaHome/bin/java.exe" '-Djava.system.class.loader=ghidra.GhidraClassLoader' "-Duser.home=$taskRoot/research/ghidra/home" "-Dapplication.settingsdir=$taskRoot/research/ghidra/settings" '-Djava.awt.headless=true' '-Xmx2G' '-cp' "$GhidraHome/Ghidra/Framework/Utility/lib/Utility.jar" ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless "$taskRoot/research/ghidra/projects/old-gold-full" OldGold -process OG.EXE -noanalysis -readOnly -scriptPath $PSScriptRoot -postScript ExportSourcePortLevelAudit.java $taskOut *> "$taskOut/ghidra.log"
 if($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath "$taskOut/ghidra.log" -SimpleMatch 'SOURCE_PORT_LEVEL_AUDIT_COMPLETE' -Quiet)){throw 'Incomplete Ghidra export'}
 $taskAfter=@{}
 foreach($taskPath in $taskExpected.Keys){
  $taskAfter[$taskPath]=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskPath)).Hash.ToLowerInvariant()
  if($taskAfter[$taskPath] -ne $taskBefore[$taskPath]){throw "Original changed $taskPath"}
 }
 [ordered]@{sample='OG.EXE';address_system='Ghidra MZ real mode segmented addresses, load segment 1000';project='research/ghidra/projects/old-gold-full/OldGold.gpr';read_only=$true;sha256_before=$taskBefore;sha256_after=$taskAfter;script_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot/ExportSourcePortLevelAudit.java").Hash.ToLowerInvariant();exports=@('vtable-readers.tsv','readers.asm')|ForEach-Object{@{path=$_;sha256=(Get-FileHash -LiteralPath "$taskOut/$_").Hash.ToLowerInvariant()}}}|ConvertTo-Json -Depth 6|Set-Content -Encoding UTF8 "$taskOut/manifest.json"
 Get-Content "$taskOut/vtable-readers.tsv"
}finally{$env:LOCALAPPDATA=$taskPreviousLocal}

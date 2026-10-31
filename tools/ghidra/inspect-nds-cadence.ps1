param([string]$Output='research/evidence/nds/n23/dos-cadence/ghidra-ru')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput=[IO.Path]::GetFullPath((Join-Path $taskRoot $Output))
if(-not $taskOutput.StartsWith((Join-Path $taskRoot 'research')+[IO.Path]::DirectorySeparatorChar)){throw 'Output outside research'}
$taskSample=Join-Path $taskRoot 'originals/pkr/RUS/PKR.EXE'
$taskBefore=(Get-FileHash -LiteralPath $taskSample -Algorithm SHA256).Hash.ToLowerInvariant()
if($taskBefore -ne '12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'){throw 'Original SHA mismatch'}
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskGhidra='C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC'
$taskJava='C:/Users/dspev/AppData/Local/Programs/Ghidra/java/jdk-21.0.12.1+1/bin/java.exe'
$taskArgs=@('-Djava.system.class.loader=ghidra.GhidraClassLoader',"-Duser.home=$taskRoot/research/ghidra/home",
    "-Dapplication.settingsdir=$taskRoot/research/ghidra/settings",'-Djava.awt.headless=true','-Xmx3G','-cp',
    "$taskGhidra/Ghidra/Framework/Utility/lib/Utility.jar",'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',
    "$taskRoot/research/ghidra/projects/ru",'Kremlin','-process','PKR.EXE','-readOnly','-noanalysis',
    '-scriptPath',$PSScriptRoot,'-postScript','InspectN23Cadence.java',$taskOutput)
$taskPreviousLocal=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA="$taskRoot/research/ghidra/local"
    & $taskJava @taskArgs *> (Join-Path $taskOutput 'ghidra.log')
} finally {$env:LOCALAPPDATA=$taskPreviousLocal}
if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath (Join-Path $taskOutput 'ghidra.log') -SimpleMatch 'N23_ORIGINAL_CADENCE_COMPLETE' -Quiet)){throw 'Ghidra export failed'}
$taskAfter=(Get-FileHash -LiteralPath $taskSample -Algorithm SHA256).Hash.ToLowerInvariant()
if($taskAfter -ne $taskBefore){throw 'Original changed'}
$taskFiles=@{}
Get-ChildItem -LiteralPath $taskOutput -File | Where-Object Extension -In '.asm','.bin' | ForEach-Object {$taskFiles[$_.Name]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
@{sample='originals/pkr/RUS/PKR.EXE';sha256_before=$taskBefore;sha256_after=$taskAfter;
    address_system='Ghidra NE loader, distinct from file offsets and runtime DPMI selectors';
    project='research/ghidra/projects/ru/Kremlin.gpr';exports=$taskFiles;
    script_sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'InspectN23Cadence.java') -Algorithm SHA256).Hash.ToLowerInvariant()} |
    ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $taskOutput 'manifest.json')
Write-Output 'Original timing export PASS'

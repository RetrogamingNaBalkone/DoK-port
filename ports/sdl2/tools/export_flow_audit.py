"""Fresh Ghidra SP16 game-over/retry export; original bytes never changed."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    local=Path(os.environ['LOCALAPPDATA'])/'Programs/Ghidra'
    java=local/'java/jdk-21.0.12.1+1/bin/java.exe'
    ghidra=local/'ghidra_12.1.2_PUBLIC'
    script=ROOT/'tools/ghidra/ExportSourcePortFlow.java'
    for profile in json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles']:
        sample=ROOT/profile['sample'];before=sha(sample)
        assert before==profile['sha256_before']
        project=ROOT/profile['project'];output=ROOT/'research/evidence/source-port/sp16-flow/ghidra'/profile['id'];output.mkdir(parents=True,exist_ok=True)
        ranges={'ru':[('objects','1068:0000','0x4400'),('retry','1098:125b','0x80'),('copy','1000:5cb3','0x70')],
                'en':[('objects','1020:0000','0x5000'),('retry','1030:133f','0x80'),('copy','1000:5cb3','0x70')],
                'old-gold':[('objects','1ae1:0000','0x4300'),('retry','21b8:10e6','0x80'),('copy','1000:1126','0x70')]}[profile['id']]
        args=[str(java),'-Djava.system.class.loader=ghidra.GhidraClassLoader',f'-Duser.home={ROOT}/research/ghidra/home',f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings','-Djava.awt.headless=true','-Xmx3G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(project.parent),project.stem,'-process',sample.name,'-readOnly','-noanalysis','-scriptPath',str(script.parent),'-postScript',script.name,str(output),before,profile['id']]+[v for row in ranges for v in row]
        with (output/'ghidra.log').open('w',encoding='utf-8') as log:result=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,LOCALAPPDATA=str(ROOT/'research/ghidra/local')))
        assert result.returncode==0 and 'SOURCE_PORT_FLOW_EXPORTED' in (output/'ghidra.log').read_text(encoding='utf-8')
        assert sha(sample)==before
        (output/'manifest.json').write_text(json.dumps(dict(profile=profile['id'],sample=profile['sample'],sha256_before=before,sha256_after=sha(sample),project=profile['project'],ranges=ranges,address_system='Ghidra MZ/NE, not file offset or runtime selector',script_sha256=sha(script),driver_sha256=sha(Path(__file__)),bytes_sha256={name:sha(output/(name+'.bin')) for name,_,_ in ranges}),indent=2)+'\n')
        print(profile['id'],'flow exported; SHA unchanged',flush=True)
if __name__=='__main__':main()

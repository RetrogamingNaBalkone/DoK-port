"""Read-only Ghidra export of the distinct Old Gold automap implementation."""
import hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    profile=next(p for p in json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles'] if p['id']=='old-gold')
    sample=ROOT/profile['sample'];before=sha(sample);assert before==profile['sha256_before']
    project=ROOT/profile['project'];out=ROOT/'research/evidence/source-port/sp15-map/old-gold';out.mkdir(parents=True,exist_ok=True)
    local=Path(os.environ['LOCALAPPDATA'])/'Programs/Ghidra';script=ROOT/'tools/ghidra/ExportSourcePortScheduler.java'
    ranges=[('world-code','21b8:000f','0x316c'),('data','3255:0000','0x1bb2'),('input','1f1d:0da1','0x3f3'),('viewport','203d:00cb','0x170')]
    args=[local/'java/jdk-21.0.12.1+1/bin/java.exe','-Djava.system.class.loader=ghidra.GhidraClassLoader',f'-Duser.home={ROOT}/research/ghidra/home',f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings','-Djava.awt.headless=true','-Xmx2G','-cp',local/'ghidra_12.1.2_PUBLIC/Ghidra/Framework/Utility/lib/Utility.jar','ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',project.parent,project.stem,'-process',sample.name,'-readOnly','-noanalysis','-scriptPath',script.parent,'-postScript',script.name,out,before]+[v for r in ranges for v in r]
    with (out/'ghidra.log').open('w',encoding='utf-8') as log:result=subprocess.run(list(map(str,args)),env=dict(os.environ,LOCALAPPDATA=str(ROOT/'research/ghidra/local')),stdout=log,stderr=subprocess.STDOUT)
    assert result.returncode==0 and 'SOURCE_PORT_SCHEDULER_EXPORTED' in (out/'ghidra.log').read_text()
    assert sha(sample)==before
    (out/'manifest.json').write_text(json.dumps(dict(sample=profile['sample'],project=profile['project'],sha256_before=before,sha256_after=sha(sample),address_system='Ghidra MZ canonical segments; Unicorn uses physical CS*16+IP aliases',ranges=ranges,script_sha256=sha(script),driver_sha256=sha(Path(__file__)),outputs={r[0]:sha(out/(r[0]+'.bin')) for r in ranges}),indent=2)+'\n')
    print('Old Gold map Ghidra exported; original unchanged')
if __name__=='__main__':main()

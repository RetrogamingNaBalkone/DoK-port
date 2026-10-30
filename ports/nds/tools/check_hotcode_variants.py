"""Per-ROM ITCM guard for final SCN/frontend/fixture variants; no execution."""
import json,os,re,subprocess,sys
from pathlib import Path
from check_strip_equality import ROOT,E,pin

def main(labels):
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal');rows=[]
    for label in labels:
        target=ROOT/'build/nds/n21p'/label;elf=target/'dok_nds_gameplay.elf';unix='/'+elf.as_posix()[0].lower()+elf.as_posix()[2:]
        command='source /opt/wonderful/bin/wf-env\n/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-objdump -h '+unix+'\n'
        result=subprocess.run([str(Path.home()/'.local/msys64/usr/bin/bash.exe'),'--login','-s'],input=command,text=True,capture_output=True,env=env,check=True)
        sections={}
        for line in result.stdout.splitlines():
            m=re.match(r'\s*\d+\s+(\.\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)',line)
            if m:sections[m[1]]=dict(bytes=int(m[2],16),vma=int(m[3],16),lma=int(m[4],16))
        itcm=sections['.itcm'];assert itcm['vma']==0x01000000 and itcm['bytes']<=32768,label
        assert sections['.dtcm']['bytes']==0,label
        manifest=json.loads((target/'build-manifest.json').read_text('utf8'))
        product=[]
        for source in manifest['sources']:
            path=ROOT/source['path']
            if path.suffix in ('.c','.h','.inc','.S','.ld','.lds') or path.name.startswith('Makefile'):
                assert pin(path)==source,('Product source changed after frozen build',label,path)
                product.append(source)
        row=dict(label=label,passed=True,itcm=itcm,itcm_limit=32768,headroom=32768-itcm['bytes'],dtcm=sections['.dtcm'],artifacts=[pin(elf),pin(target/'dok_nds_gameplay.nds'),pin(target/'dok_nds_gameplay.map')],manifest=pin(target/'build-manifest.json'),product_sources=product)
        (E/('hotcode-layout-'+label+'.json')).write_text(json.dumps(row,indent=2)+'\n','utf8');rows.append(row)
    report=dict(task='N21P final ITCM variant guards',passed=True,variants=rows,tool=pin(Path(__file__)))
    if len(labels)==7:(E/'hotcode-variant-layout.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps([dict(label=row['label'],itcm_bytes=row['itcm']['bytes'],rom=row['artifacts'][1]['sha256'],elf=row['artifacts'][0]['sha256']) for row in rows]))
if __name__=='__main__':main(sys.argv[1:])

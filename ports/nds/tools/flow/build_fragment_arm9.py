"""Reconstruct V14 allocation layout through public SDK/backend calls only."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
from check_host import ROOT,pin

def dimensions(size):
    return {32768:(128,256),16384:(128,128),8192:(128,64),4096:(64,64),2048:(64,32),256:(16,16)}[size]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('variant',choices=('baseline','partial'));args=parser.parse_args()
    out=ROOT/f'build/nds/n22/fragment-{args.variant}-01';out.mkdir(parents=True,exist_ok=True)
    generated=out/'generated';generated.mkdir(exist_ok=True)
    source=ROOT/'research/evidence/nds/n22/root-v14-layout.json';data=json.loads(source.read_text(encoding='utf-8'))
    last=data['snapshots'][-1];slots=sorted(last['slots'],key=lambda row:row['gpu']);prefix=slots[:]
    dummy=0
    for hole in last['holes']:
        address=hole['gpu'];left=hole['bytes']
        while left:
            size=16384 if left>=16384 else 4096
            w,h=dimensions(size);prefix.append(dict(slot=128+dummy,key=0xf000000000000100+dummy,asset=0xf0000000+dummy,
                 gpu=address,bytes=size,storage_width=w,storage_height=h));dummy+=1;left-=size;address+=size
    prefix.sort(key=lambda row:row['gpu'])
    bykey={row['key']:row for row in slots}
    final=[bykey[row['key']] for row in last['demand'] if row['key']!=data['evicted'][0]['key']]
    for row in data['new_demands']:
        w,h=dimensions(row['bytes']);final.append(dict(**row,gpu=0,storage_width=w,storage_height=h,slot=0))
    assert len(prefix)==21 and len(slots)==16 and len(final)==19
    def lines(name,rows):
        result=[f'static const RootTexture {name}[]={{']
        result += [f'{{UINT64_C({row["key"]}),{row["asset"]}u,{row["gpu"]}u,{row["bytes"]}u,{row["storage_width"]}u,{row["storage_height"]}u}},' for row in rows]
        return result+['};']
    header=['/* Generated from pinned root-v14-layout.json. Physical addresses, not private allocator writes. */']
    header+=lines('root_prefix',prefix)+lines('root_previous',slots)+lines('root_final',final)
    (generated/'fragment_layout.inc').write_text('\n'.join(header)+'\n',encoding='utf-8')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    relative=out.relative_to(ROOT/'build/nds/n22').as_posix()
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n22fragment OUTDIR=../../build/nds/n22/"+relative+' PARTIAL='+str(int(args.variant=='partial'))+'\n'
    process=subprocess.run([str(Path.home()/'.local/msys64/usr/bin/bash.exe'),'--login','-s'],input=command,
                           env=env,cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
    (out/'build.log').write_text(process.stdout+process.stderr,encoding='utf-8')
    if process.returncode:raise RuntimeError(process.stdout+process.stderr)
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text(encoding='utf-8-sig'))
    assert all(k+' '+v in process.stdout for k,v in lock['packages'].items())
    evidence=ROOT/f'research/evidence/nds/n22/fragment-{args.variant}-01';evidence.mkdir(parents=True,exist_ok=True)
    inputs=[source,Path(__file__).resolve(),generated/'fragment_layout.inc',ROOT/'ports/nds/Makefile.n22fragment',
            ROOT/'ports/nds/tools/flow/fragment_arm9.c',ROOT/'ports/nds/platform/render_probe/render_backend.c',
            ROOT/'ports/nds/platform/render_probe/render_backend.h',ROOT/'ports/nds/render/vram_layout.c']
    inputs+=sorted((ROOT/'ports/nds/include').rglob('*.h'));snapshots=[]
    for current in inputs:
        snapshot=evidence/'inputs'/current.relative_to(ROOT);snapshot.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(current,snapshot)
        snapshots.append(dict(current=pin(current),snapshot=pin(snapshot)))
    result=dict(task='N22',variant=args.variant,source=pin(source),inputs=snapshots,command=command,packages=lock['packages'],
                arm7=pin(ROOT/'build/nds/n18/arm7/dok_audio_arm7.elf'),
                artifacts=[pin(out/f'dok_nds_fragment.{suffix}') for suffix in ('nds','elf','map')],layout=dict(prefix=prefix,previous=slots,final=final))
    for destination in (out/'build-manifest.json',evidence/'build-manifest.json'):
        destination.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result['artifacts']))

if __name__=='__main__':main()

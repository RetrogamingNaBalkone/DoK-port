"""C16 prove placement-only source delta and actual SDK ITCM/veneer layout."""
import json,os,re,subprocess
from pathlib import Path
from check_strip_equality import ROOT,E,pin

def main():
    archive=E/'pre-itcm/pins.json';baseline=json.loads(archive.read_text('utf8'));rows=baseline['source_pins']
    for row in rows:
        old=ROOT/row['archive'];assert pin(old)['sha256']==row['sha256']
        new=(ROOT/row['path']).read_text('utf8');before=old.read_text('utf8')
        if row['path'].endswith('Makefile.n21p'):
            suffix='\n# Placement-only hot code experiment; SDK linker already owns ITCM.\nN21P_ITCM ?= 1\nCPPFLAGS += -DN21P_ITCM=$(N21P_ITCM)\nCPPFLAGS += -I$(BLOCKSDS)/libs/libnds/include\nPLATFORM_CPPFLAGS += -DN21P_ITCM=$(N21P_ITCM)\n'
            assert new==before+suffix
        else:
            assert new.replace('#include "dok/hotcode.h"\n','',1).replace('DOK_NDS_HOT_CODE ','')==before,('Non-placement change',row['path'])
    header=ROOT/'ports/nds/include/dok/hotcode.h';assert '#define DOK_NDS_HOT_CODE ITCM_CODE' in header.read_text('utf8')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    evidence=E/'hotcode-placement';evidence.mkdir(exist_ok=True)
    layouts={};inputs=[]
    for label in ('candidate15-ru','candidate16-ru'):
        target=ROOT/'build/nds/n21p'/label;elf=target/'dok_nds_gameplay.elf';unix='/'+elf.as_posix()[0].lower()+elf.as_posix()[2:]
        base='/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'
        command='source /opt/wonderful/bin/wf-env\n'+base+'objdump -h '+unix+'\n'+base+'nm -S '+unix+'\n'
        result=subprocess.run([str(Path.home()/'.local/msys64/usr/bin/bash.exe'),'--login','-s'],input=command,text=True,capture_output=True,env=env,check=True)
        output=evidence/(label+'-layout.txt');output.write_text(result.stdout,'utf8');inputs.extend([pin(elf),pin(target/'dok_nds_gameplay.nds'),pin(target/'dok_nds_gameplay.map'),pin(output)])
        sections={};symbols={}
        for line in result.stdout.splitlines():
            m=re.match(r'\s*\d+\s+(\.\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)',line)
            if m:sections[m[1]]=dict(bytes=int(m[2],16),vma=int(m[3],16),lma=int(m[4],16))
            m=re.match(r'([0-9a-f]+)\s+([0-9a-f]+)\s+\w\s+(\S+)',line)
            if m:symbols[m[3]]=dict(address=int(m[1],16),bytes=int(m[2],16))
        layouts[label]=dict(sections=sections,symbols=symbols)
    old,new=layouts.values();itcm=new['sections']['.itcm'];assert itcm['vma']==0x01000000 and itcm['bytes']<=32768
    required=('build_cell','dok_geometry_build_cell_prepared','dok_render_scene_prepare','dok_render_merge','dok_door_scene_prepare','record_geometry','dok_map_discovery_update','dok_map_discovery_records','dok_map_raster','dok_nds_render_submit','planned_cell_clip')
    for name in required:assert itcm['vma']<=new['symbols'][name]['address']<itcm['vma']+itcm['bytes'],name
    sdk_old={name:row for name,row in old['symbols'].items() if 0x01000000<=row['address']<0x01008000}
    for name,row in sdk_old.items():assert new['symbols'][name]['bytes']==row['bytes'],name
    assert old['sections']['.dtcm']['bytes']==new['sections']['.dtcm']['bytes']==0
    assert new['symbols']['vblank_irq']['bytes']==old['symbols']['vblank_irq']['bytes']
    veneers={k:v for k,v in new['symbols'].items() if k.endswith('_from_thumb') or k.endswith('_veneer')}
    assert '__dok_map_discovery_update_from_thumb' in veneers and '__dok_nds_render_submit_from_thumb' in veneers
    report=dict(task='N21P C16 placement-only ITCM',passed=True,archive=pin(archive),source_pins=[pin(ROOT/row['path']) for row in rows]+[pin(header),pin(Path(__file__))],inputs=inputs,itcm=itcm,itcm_limit=32768,itcm_headroom=32768-itcm['bytes'],old_itcm=old['sections']['.itcm'],hot_functions={k:new['symbols'][k] for k in required},inherited_sdk_itcm_symbols=sdk_old,sdk_symbol_sizes_unchanged=True,dtcm_unchanged=True,vblank_irq_size_unchanged=True,interworking_veneers=veneers,source_delta='Conditional SDK ITCM_CODE section+long_call attributes, header inclusion and Makefile flags only. No function bodies, API declarations, ABI, data/scratch lifetimes, IRQ handlers or stack sizes changed.',runtime_acceptance=False)
    (E/'hotcode-placement.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(dict(passed=True,itcm_bytes=itcm['bytes'],headroom=report['itcm_headroom'])))
if __name__=='__main__':main()

"""Death UI/integration fixtures: inject health=0, then use real buttons.

Offsets are derived from the exact ELF's DWARF; no fixed gameplay layout.
"""
import ctypes as C
import json,os,struct,subprocess,sys
from pathlib import Path
import run_frontend_emulator as front
sys.path.insert(0,str(Path(__file__).resolve().parent/'flow'))
from inspect_ai_layout import parse,member


def layout(elf,root_name='SpriteWorld'):
    tool=Path.home()/'.local/msys64/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-readelf.exe'
    env=dict(os.environ);env['PATH']=str(Path.home()/'.local/msys64/opt/wonderful/bin')+';'+env['PATH']
    nodes=parse(subprocess.check_output([tool,'--debug-dump=info',elf],env=env,text=True,encoding='utf8'))
    root=next(n for n in nodes.values()if n['tag']=='DW_TAG_structure_type' and n['attrs'].get('name')==root_name)
    def offset(*names):
        node=root;total=0
        for name in names:
            value,node=member(nodes,node,name);total+=value
        return total
    return offset


def put(f,address,data):
    assert 0x02000000<=address<=0x02400000-len(data)
    C.memmove(int(f.core.retro_get_memory_data(2))+address-0x02000000,data,len(data))


class DeathObserver(front.FrontendObserver):
    def check_pixels(self,p):
        if p[3]==13:return # Dedicated death text oracle below.
        super().check_pixels(p)


def main():
    target=Path(sys.argv[1]).resolve();elf=target/'dok_nds_gameplay.elf'
    offset=layout(elf);directory=target/'death39';directory.mkdir(exist_ok=True)
    report=dict(passed=False,scope=__doc__,rows=[])
    for language in (1,2):
        f=DeathObserver(front.CORE,target,directory/str(language))
        front.write_fixture(f.save/'melonDS DS/dldi_sd_card.bin',front.settings_bytes(language),embedded=True)
        try:
            f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
            f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8);f.tick(12)
            at,_=front.transport.elf_symbol(elf,'storage')
            storage=struct.unpack('<I',front.render.ram(f,at,4))[0]
            flow_at,_=front.transport.elf_symbol(elf,'g_nds_flow_probe')
            flow=lambda:struct.unpack('<32I',front.render.ram(f,flow_at,128))
            for attempts in (2,1,0):
                # Held gameplay A must not skip the new screen.
                f.joypad=1<<front.A
                put(f,storage+offset('game','items','inventory','health'),struct.pack('<h',0))
                f.wait_for(13,unblocked=False);f.tick(20);assert f.probe[3]==13
                before=tuple(f.probe[40:52]);f.tick(30);assert tuple(f.probe[40:52])==before
                if attempts:assert flow()[7]==attempts and flow()[14]==2-attempts
                # Exact CP866 font pixels and actual LCD transfer are inspected
                # through the published INDEX8 canvases; RGB captures retained.
                top=front.render.ram(f,f.probe[20],49152)
                labels=json.loads((front.ROOT/'ports/nds/platform/frontend/labels.json').read_text('utf8'))['death'][language-1]
                font=f.art.resources[language]['FONT8']
                lines=[(labels[0 if attempts else 1],64)]
                lines+=([(labels[2 if attempts==1 else 3],88),(labels[4],144),(labels[5],164)]
                        if attempts else [(labels[6],156)])
                for value,y in lines:
                    value=value.encode('cp866');x=(256-len(value)*8)//2
                    for c,code in enumerate(value):
                        for r in range(8):
                            for b in range(8):
                                if font[code*8+r]&(128>>b):assert top[(y+r)*256+x+c*8+b]==2
                assert len(set(top))>1 and not any(front.render.ram(f,f.probe[21],49152))
                captures=f.capture(f'death-{language}-{attempts}')
                report['rows'].append(dict(language=language,attempts=attempts,flow=list(flow()),captures=captures))
                f.joypad=0;f.tick(3);f.tap(front.START if attempts==1 else front.A)
                f.wait_for(8 if attempts else 3)
                if attempts:
                    f.tick(8);assert flow()[14]==3-attempts and flow()[6]>0
                    assert f.probe[40]>=before[0] and f.probe[44]==before[4]
            # New world after game over; B abandons an ordinary death.
            f.tap(front.A);f.text_to(7,8);f.tick(12)
            put(f,storage+offset('game','items','inventory','health'),struct.pack('<h',0))
            f.wait_for(13);f.joypad=0;f.tick(3);f.tap(front.B);f.wait_for(3)
            assert flow()[3]==0
        finally:f.close();front.transport.flush_native_stdio()
    report.update(passed=True,rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,death_screens=len(report['rows']))))


if __name__=='__main__':main()

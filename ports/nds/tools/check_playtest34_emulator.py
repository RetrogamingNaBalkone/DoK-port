"""N23 UI/input smoke. Text-screen fixtures explicitly write frontend RAM only."""
import ctypes as C
import json,struct,sys
from pathlib import Path
import run_frontend_emulator as front
from frontend_pixel_oracle import text_pages, prepared_text

class FixtureObserver(front.FrontendObserver):
    fixture_draw=None
    def check_pixels(self,p):
        if self.fixture_draw:
            revision,index,minimum=self.fixture_draw
            # A RAM fixture can interrupt an old draw/audio command. Its
            # synthetic state is accepted only after its own new canvas exists.
            if p[11]!=revision or p[16]!=index or p[24]<minimum:return
            self.fixture_draw=None
        super().check_pixels(p)

    def wait_fixture(self):
        for _ in range(12000):
            if self.fixture_draw is None:return
            self.tick()
        raise AssertionError('Synthetic frontend state was not redrawn')

def main():
    target=Path(sys.argv[1]).resolve();directory=target/'playtest34';directory.mkdir(exist_ok=True)
    f=FixtureObserver(front.CORE,target,directory)
    image=f.save/'melonDS DS/dldi_sd_card.bin'
    front.write_fixture(image,None,embedded=True)
    elf=target/'dok_nds_gameplay.elf'
    report=dict(passed=False,scope='Real menu/input; explicit frontend RAM fixtures for outro/ending, no full playthrough')
    def pose():
        return list(f.probe[46:48])+[f.probe[41]]
    def text_fixture(state,language,level):
        # Native ARM9 dok_frontend offsets: pages0, page32, count34,
        # previous36, revision40, state48, language50, level51, blocked55.
        addr,_=front.transport.elf_symbol(elf,'frontend')
        address=struct.unpack('<I',front.render.ram(f,addr,4))[0]
        ptr=f.core.retro_get_memory_data(2)
        def put(offset,data):C.memmove(int(ptr)+address-0x02000000+offset,data,len(data))
        index=2*level-1 if state==7 else 2*level if state==10 else 7
        name=('README','BINTRO1','EINTRO1','BINTRO2','EINTRO2','BINTRO3','EINTRO3','ENDTXT')[index]
        count=1 if state==11 else len(text_pages(prepared_text(f.art.resources[language][name],index),30,11,prose=True))
        # Read the live state revision: publication can lag a completed draw
        # while a synchronous audio command waits for the other CPU.
        revision=struct.unpack('<I',front.render.ram(f,address+40,4))[0]
        f.fixture_draw=(revision+1,index,f.probe[24]+1)
        put(32,struct.pack('<HHI',0,count,0));put(40,struct.pack('<I',revision+1))
        put(48,bytes([state,0,language,level]));put(55,b'\x01')
        f.wait_for(state,language=language,page=0)
        f.wait_fixture()
        if state==10:f.tap(front.B);f.wait_for(state,page=0)
        if count>1:
            f.tap(front.A);f.wait_for(state,page=1)
            if state!=7:f.tap(front.B);f.wait_for(state,page=0)
        # Advance every page except the final intro/ending to avoid a synthetic world load/EXIT.
        for page in range(2 if state==7 and count>1 else 1,count):f.tap(front.A);f.wait_for(state,page=page)
        report.setdefault('texts',[]).append(dict(state=state,language=language,level=level,pages=count))
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        for language in (1,2):
            for level in (1,2,3):
                for state in (7,10):text_fixture(state,language,level)
            text_fixture(11,language,1)
        # Restore MAIN using the same narrow UI fixture, with correct menu revision.
        addr,_=front.transport.elf_symbol(elf,'frontend');address=struct.unpack('<I',front.render.ram(f,addr,4))[0]
        ptr=f.core.retro_get_memory_data(2)
        C.memmove(int(ptr)+address-0x02000000+32,struct.pack('<HHI',0,1,0),8)
        C.memmove(int(ptr)+address-0x02000000+48,bytes([2,2,1,1]),4)
        revision=struct.unpack('<I',front.render.ram(f,address+40,4))[0]
        f.fixture_draw=(revision+1,0xffffffff,f.probe[24]+1)
        C.memmove(int(ptr)+address-0x02000000+40,struct.pack('<I',revision+1),4)
        C.memmove(int(ptr)+address-0x02000000+55,b'\x01',1)
        f.wait_for(2,selection=2,language=1)
        f.wait_fixture()
        f.tap(front.A);f.wait_for(5,selection=0)
        f.tap(front.DOWN);f.wait_for(5,selection=1)
        f.capture('classic-controls');f.tap(front.A);f.wait_for(2)
        assert f.probe[28]==1 and f.probe[29]==0
        f.choose(2,0);f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8)
        f.tick(20)
        # Live hero heading comes from render camera, published by map live probe.
        map_address,_=front.transport.elf_symbol(elf,'g_nds_map_probe')
        def live():
            p=struct.unpack('<64I',front.render.ram(f,map_address,256))
            return list(struct.unpack('<48I',front.render.ram(f,p[61],192))[6:9])
        rows=[]
        for name,button,turn in [('turn-right',7,True),('turn-left',6,True),('strafe-right',11,False),('strafe-left',10,False)]:
            before=live();f.joypad=1<<button;f.tick(12);during=live();f.joypad=0;f.tick(8);stopped=live();f.tick(20)
            assert live()==stopped,(name,'release drift')
            assert during[2]!=before[2] if turn else during[:2]!=before[:2],(name,before,during)
            assert during[:2]==before[:2] if turn else during[2]==before[2],(name,'wrong action')
            rows.append(dict(action=name,before=before,during=during,stopped=stopped))
        f.tap(front.START);f.wait_for(9)
        frozen=f.probe[40:52];f.choose(9,2);f.tap(front.A);f.wait_for(5,selection=1)
        f.tap(front.UP);f.wait_for(5,selection=0);f.capture('modern-controls')
        f.tap(front.A);f.wait_for(9);assert f.probe[40:52]==frozen
        f.choose(9,2);f.tap(front.A);f.wait_for(5,selection=0);f.tap(front.DOWN);f.wait_for(5,selection=1);f.tap(front.A);f.wait_for(9)
        assert f.probe[28]==3 and f.probe[29]==0
        report.update(passed=True,controls=rows,index8_checks=f.report['index8_checks'],rgb_checks=f.report['rgb_checks'],screens=f.report['rows'],settings_writes=f.probe[28],memory=front.render.verify_memory(f,f.report['last_renderer'],elf))
    finally:f.close();front.transport.flush_native_stdio()
    files=front.volume_files(image);assert set(files)=={'/DOK/SETTINGS.DAT','/DOK_ND~1.NDS'},list(files)
    # melonDS injects a DLDI-patched ROM file; it is core-owned, not a game write.
    assert files['/DOK/SETTINGS.DAT']==front.settings_bytes(1,1)
    # Cold restart reads the real settings-only FAT write.
    f=front.FrontendObserver(front.CORE,target,directory)
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);assert f.probe[27]==1
        f.tap(front.A);f.wait_for(2);f.choose(2,2);f.tap(front.A);f.wait_for(5,selection=1)
        report['cold_restart_classic']=True
    finally:f.close();front.transport.flush_native_stdio()
    report['rom_sha256']=front.transport.sha(target/'dok_nds_gameplay.nds')
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps({k:v for k,v in report.items() if k!='screens'}))
if __name__=='__main__':main()

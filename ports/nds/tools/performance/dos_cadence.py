"""Unmodified PKR live cadence, native keyboard input and read-only DPMI data."""
import argparse,gzip,json,os,re,secrets,shutil,struct,subprocess,sys,time,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
import capture_visual_dos as base

def descriptor(b):return int.from_bytes(b[2:4],'little')|(b[4]<<16)|(b[7]<<24)
def locate_ldt(memory,data_base,code):
    tables=set()
    for access in (0x82,0xa2,0xc2,0xe2):
        at=5
        while (at:=memory.find(bytes([access]),at))>=0:
            d=memory[at-5:at+3];at+=1
            if len(d)!=8 or d[6]&128:continue
            limit=int.from_bytes(d[:2],'little')|((d[6]&15)<<16);table=descriptor(d)
            if not(256<=limit<65536 and 0<table<len(memory)-limit):continue
            bases=[descriptor(memory[p:p+8])for p in range(table,table+limit-6,8)]
            # Borland's three-byte MOV AX,SS/NOP entry is loader-fixable;
            # compare the following original instructions, not that fixup.
            if data_base in bases and any(memory[p+0x3848:p+0x3859]==code[0x3848:0x3859]for p in bases):tables.add(table)
    assert len(tables)==1,tables
    return tables.pop()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--run-id',required=True);parser.add_argument('--cycles',type=int,default=60000);parser.add_argument('--seconds',type=float,default=15)
    args=parser.parse_args();assert re.fullmatch('[a-z0-9-]+',args.run_id)
    out=ROOT/'research/evidence/nds/n23/dos-cadence'/args.run_id;out.mkdir(parents=True,exist_ok=False)
    source=ROOT/'originals/pkr/RUS';before={p.name:base.digest(p.read_bytes())for p in source.iterdir()if p.is_file()}
    assert before['PKR.EXE']=='12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'
    game=out/'game';shutil.copytree(source,game)
    port=8159
    try:urllib.request.urlopen(f'http://127.0.0.1:{port}/',timeout=.3)
    except OSError:pass
    else:raise ValueError('Port occupied')
    old=ROOT/'research/evidence/nds/n06/dos/ru/level1'
    config=(old/'dosbox.conf').read_text('ascii').replace(str(old/'game'),str(game))
    config=re.sub(r'(?m)^webserver_port = \d+$',f'webserver_port = {port}',config)
    config=re.sub(r'(?m)^(cpu_cycles(?:_protected)?) = \d+$',lambda m:m[1]+' = '+str(args.cycles),config)
    assert str(game) in config
    (out/'dosbox.conf').write_text(config,'ascii')
    api=base.Api(port,secrets.token_hex(32));env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',DOSBOX_API_TOKEN=api.token)
    report=dict(passed=False,source_before=before,cycles=args.cycles,events=[],samples=[],observer=base.digest(Path(__file__).read_bytes()),
        method='Original EXE/RES, native i8042 keys, read-only memory; no patches, pose writes, clock writes or injected callbacks',
        address_system='Validated live DPMI LDT resolves selectors; Ghidra NE addresses only identify code/data')
    def mem(address,n):return api.request(f'memory/{address}/{n}')
    proc=None
    try:
        with (out/'stdout.log').open('wb')as stdout,(out/'stderr.log').open('wb')as stderr:
            proc=subprocess.Popen([str(base.DEFAULT_DOSBOX),'--noprimaryconf','--nolocalconf','-conf',str(out/'dosbox.conf'),'-noconsole'],cwd=out,env=env,stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
            report['emulator']=base.wait_for(lambda:api.get('dosbox/info'))
            data_base=base.wait_for(lambda:base.base_of(api.memory(),'ru'))
            def key(name,duration=100):
                result=api.get('cpu/cycles')
                api.request('input/sequence',{'events':[{'type':'key','key':'KBD_'+name,'pressed':True,'t':0},{'type':'key','key':'KBD_'+name,'pressed':False,'t':duration}]},'POST')
                report['events'].append(dict(key=name,duration_ms=duration,host=time.monotonic(),cycles=result))
            time.sleep(1)
            for i in range(5):key('enter');time.sleep(1.5)
            memory=api.request('memory/0/33554432');code=(ROOT/'research/evidence/nds/n23/dos-cadence/ghidra-ru/1068.bin').read_bytes()
            with gzip.open(out/'initial-memory.bin.gz','wb')as f:f.write(memory)
            ldt=locate_ldt(memory,data_base,code)
            def ptr(raw,size=1):
                off,sel=struct.unpack('<HH',raw);assert sel&4
                d=memory[ldt+(sel&~7):ldt+(sel&~7)+8];limit=int.from_bytes(d[:2],'little')|((d[6]&15)<<16)
                if d[6]&128:limit=(limit<<12)|4095
                assert d[5]&0x90==0x90 and off+size-1<=limit
                address=descriptor(d)+off;assert 0<=address<=len(memory)-size;return address
            world=ptr(memory[data_base+0x266a:data_base+0x266e],0x74a)
            objects={}
            for glyph in range(256):
                raw=memory[world+0x30e+glyph*4:world+0x312+glyph*4]
                if raw==bytes(4):continue
                address=ptr(raw,128);kind=struct.unpack_from('<H',memory,address+0x12)[0]
                if kind in (1,7,9):objects[glyph]=(address,kind)
            assert any(k==9 for _,k in objects.values())
            hud=ptr(memory[data_base+0xa5d6:data_base+0xa5da],0x91)
            report.update(data_base=data_base,ldt=ldt,world=world,hud=hud,objects=objects,
                initial_c2=struct.unpack_from('<I',memory,data_base+0xc2)[0])
            key('enter');time.sleep(.4)
            t0=time.monotonic();last={};index=0
            event_arrays={g:ptr(memory[a+4:a+8],64)for g,(a,k)in objects.items()if k==9}
            first=min([a for a,k in objects.values()if k==9]+list(event_arrays.values()));end=max([a+256 for a,k in objects.values()if k==9]+[a+64 for a in event_arrays.values()])
            assert end-first<2*1024*1024
            report['actor_read_range']=[first,end]
            # Same native turn/move inputs for every comparison CPU rate.
            for name,duration in [('right',700),('up',1000),('space',100),('up',1200)]:key(name,duration);time.sleep(duration/1000+.2)
            while time.monotonic()-t0<args.seconds:
                start=time.monotonic();rows={}
                heap=mem(first,end-first)
                for glyph,(address,kind)in objects.items():
                    if kind!=9:continue
                    b=heap[address-first:address-first+256];state=struct.unpack_from('<h',b,0xa2)[0]
                    if not state:continue
                    value=dict(state=state,cursor=struct.unpack_from('<h',b,0x50)[0],interval=struct.unpack_from('<h',b,0x52)[0],
                        image=struct.unpack_from('<I',b,0x44)[0],xyz=struct.unpack_from('<iii',b,0x26))
                    count=struct.unpack_from('<H',b)[0];assert count<=4
                    value['events']=[struct.unpack_from('<IIII',heap,event_arrays[glyph]-first+i*16)for i in range(count)]
                    if value!=last.get(glyph):rows[glyph]=value;last[glyph]=value
                timing=mem(data_base+0xc2,4);clock=mem(data_base+0x20c6,10);frame=mem(data_base+0x1f9a,4);h=mem(hud,0x91)
                sample=dict(host_start=start-t0,host_end=time.monotonic()-t0,c2=struct.unpack('<I',timing)[0],
                    clock=struct.unpack('<HII',clock),frame_ticks=struct.unpack('<I',frame)[0],changes=rows,
                    text=h[0x2c:0x52].decode('cp866'),text_countdown=struct.unpack_from('<i',h,0x8d)[0])
                report['samples'].append(sample);index+=1
                if index%30==0:(out/'progress.json').write_text(json.dumps(dict(samples=index,last=sample)),'utf8')
                time.sleep(.005)
            report['passed']=True
    except Exception as error:report['error']=repr(error);raise
    finally:
        if proc:proc.terminate();proc.wait(timeout=10)
        after={p.name:base.digest(p.read_bytes())for p in source.iterdir()if p.is_file()}
        copied={p.name:base.digest(p.read_bytes())for p in game.iterdir()if p.is_file()}
        report.update(source_after=after,originals_unchanged=after==before,
            runtime_files_unchanged=all(copied[k]==before[k]for k in ('PKR.EXE','PKR.RES','RTM.EXE','DPMI16BI.OVL')))
        assert report['originals_unchanged']and report['runtime_files_unchanged']
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=report['passed'],samples=len(report['samples']),initial_c2=report['initial_c2'],objects=len(report['objects']))))

if __name__=='__main__':main()

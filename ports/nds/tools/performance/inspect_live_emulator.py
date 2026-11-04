"""Read published NDS probes from a running Windows emulator; never write RAM."""
import argparse,ctypes as c,json,struct,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_diag_emulator as transport
import run_renderer_emulator as render
from joint_profile_abi import decode_profile
ROOT=Path(__file__).resolve().parents[4]

class Region(c.Structure):
    _fields_=[('base',c.c_uint64),('allocation',c.c_uint64),('allocation_protect',c.c_uint32),
              ('padding',c.c_uint32),('size',c.c_uint64),('state',c.c_uint32),('protect',c.c_uint32),
              ('kind',c.c_uint32),('padding2',c.c_uint32)]

def main():
    p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--samples',type=int,default=20)
    args=p.parse_args();kernel=c.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[c.c_uint32,c.c_int,c.c_uint32];kernel.OpenProcess.restype=c.c_void_p
    kernel.VirtualQueryEx.argtypes=[c.c_void_p,c.c_void_p,c.POINTER(Region),c.c_size_t];kernel.VirtualQueryEx.restype=c.c_size_t
    kernel.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
    kernel.ReadProcessMemory.restype=c.c_int
    kernel.CloseHandle.argtypes=[c.c_void_p]
    process=kernel.OpenProcess(0x410,0,args.pid)
    if not process:raise OSError(c.get_last_error(),'OpenProcess read/query access')
    def read(address,size):
        buffer=c.create_string_buffer(size);got=c.c_size_t()
        if not kernel.ReadProcessMemory(process,address,buffer,size,c.byref(got)):return b''
        return buffer.raw[:got.value]
    try:
        candidates=[];address=0;scanned=0;hits=[]
        while address<0x7fffffff0000:
            region=Region()
            if not kernel.VirtualQueryEx(process,address,c.byref(region),c.sizeof(region)):break
            address=region.base+region.size
            if region.state!=0x1000 or region.kind not in (0x20000,0x40000) or region.protect&0x101 or not 0x10000<=region.size<=0x4000000:continue
            raw=read(region.base,region.size);scanned+=len(raw);start=0
            while True:
                offset=raw.find(struct.pack('<I',0x37524b44),start)
                if offset<0:break
                start=offset+4
                if offset+384>len(raw):continue
                values=struct.unpack_from('<24I',raw,offset)
                hits.append(dict(host=hex(region.base+offset),values=list(values)))
                if values[1]==1 and values[2]==360 and values[4] in (1,4):candidates.append(region.base+offset)
        fields=render.scalar_fields(ROOT/'ports/nds/platform/render_probe/render_probe.h','NdsRenderProbe')
        matches=[]
        for label in ('discovery32-ru','mapcache31-ru','animation26-ru','audio21-ru','candidate22-ru'):
            target=ROOT/'build/nds/n23'/label;elf=target/'dok_nds_gameplay.elf'
            if not elf.exists():continue
            try:
                symbols={key:transport.elf_symbol(elf,name)[0]for key,name in [('render','g_nds_render_probe'),('map','g_nds_map_probe'),('front','g_nds_frontend_probe'),('end','__end__')]}
            except Exception:continue
            for host in candidates:
                base=host-(symbols['render']-0x02000000)
                mp=read(base+symbols['map']-0x02000000,256);fp=read(base+symbols['front']-0x02000000,256)
                if len(mp)!=256 or len(fp)!=256:continue
                if struct.unpack_from('<I',mp)[0]!=0x20504b44 or struct.unpack_from('<I',fp)[0]!=0x21504b44:continue
                if base&4095:continue
                end=read(host+fields.index('static_end')*4,4)
                if len(end)!=4 or struct.unpack('<I',end)[0]!=symbols['end']:continue
                matches.append((label,base,symbols))
        report=dict(pid=args.pid,read_only=True,scanned_bytes=scanned,raw_hits=hits,candidate_probes=len(candidates),matches=[],samples=[])
        for label,base,symbols in matches:
            report['matches'].append(dict(label=label,host_ram_base=hex(base),symbols=symbols))
        history={}
        if matches:
            label,base,symbols=matches[0]
            for _ in range(args.samples):
                rr=read(base+symbols['render']-0x02000000,len(fields)*4)
                rr_after=read(base+symbols['render']-0x02000000,len(fields)*4)
                if len(rr)!=len(fields)*4 or rr[12:16]!=rr_after[12:16] or struct.unpack_from('<I',rr,12)[0]&1:
                    time.sleep(.05);continue
                mp=read(base+symbols['map']-0x02000000,256)
                fp=read(base+symbols['front']-0x02000000,256)
                renderer=dict(zip(fields,struct.unpack('<'+'I'*len(fields),rr)))
                history_fields=render.scalar_fields(ROOT/'ports/nds/platform/render_probe/render_probe.h','NdsRenderHistoryRecord')
                hraw=read(base+renderer['history_address']-0x02000000,renderer['history_capacity']*136)
                hraw_after=read(base+renderer['history_address']-0x02000000,len(hraw))
                for at in range(0,len(hraw)-135,136):
                    values=struct.unpack_from('<34I',hraw,at)
                    if values[0]!=0x37484b44 or values[3]&1 or hraw[at+12:at+16]!=hraw_after[at+12:at+16]:continue
                    row=dict(zip(history_fields,values));history[row['ordinal']]=row
                map_values=struct.unpack('<64I',mp);front_values=struct.unpack('<64I',fp)
                item=dict(renderer=renderer,map=list(map_values),frontend=list(front_values))
                ptr=map_values[60]+64
                if 0x02000000<=ptr<0x02400000-192:
                    raw=read(base+ptr-0x02000000,192)
                    raw_after=read(base+ptr-0x02000000,192)
                    if len(raw)==192:
                        words=struct.unpack('<48I',raw)
                        if words[0]==0x21504b44 and not words[1]&1 and raw[4:8]==raw_after[4:8]:item['profile']=decode_profile(words)
                report['samples'].append(item);time.sleep(.05)
        report['history_records']=sorted(history.values(),key=lambda r:r['ordinal'])
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n','utf8')
        profiles={s['profile']['frame_id']:s['profile']for s in report['samples']if 'profile'in s}
        stages=('cpu_excluding_wait','simulation','geometry','sprites','discovery','submit_including_wait','map','map_raster','effects')
        summary={k:dict(samples=len(profiles),mean_ms=sum(r[k]for r in profiles.values())/len(profiles)*1000/33513982,
                       minimum_ms=min(r[k]for r in profiles.values())*1000/33513982,maximum_ms=max(r[k]for r in profiles.values())*1000/33513982)
                 for k in stages}if profiles else {}
        print(json.dumps(dict(matches=report['matches'],profiles=len(profiles),summary=summary,history=len(history))))
    finally:kernel.CloseHandle(process)
if __name__=='__main__':main()

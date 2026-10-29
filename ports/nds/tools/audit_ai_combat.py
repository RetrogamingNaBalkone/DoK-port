"""Replay actual N17 callback transcripts with original PKR/PKE instructions.
Animation/geometry/motion/timer/audio/UI/allocation boundaries are controlled;
the far copy helper, state branches and nested player/stat handlers execute.
"""
from __future__ import annotations
import hashlib,json,re,struct,subprocess,random
from pathlib import Path
from audit_items import Original as Base,ROOT
from audit_sprites import originals
from audit_world import originals as archives,parse_level,expected_profile,scalar
from unicorn import x86_const as R
from unicorn import UC_HOOK_CODE
OUT=ROOT/'research/evidence/nds/n17'
def word(n):return (n+32768)%65536-32768
def trace_fields():
    text=(ROOT/'ports/nds/tests/ai_combat_cases.h').read_text()
    body=re.search(r'typedef struct dok_ai_combat_trace\s*\{(.*?)\}',text,re.S).group(1)
    return [v.strip() for v in body.replace('int32_t','').replace(';','').split(',')]
def animation(payload):
    n=struct.unpack_from('<h',payload)[0];cursor=8+10*n;frames=[]
    for i in range(n):
        width,height=struct.unpack_from('<2h',payload,cursor);interval=struct.unpack_from('<h',payload,8+10*i)[0]
        frames.append((interval,width,height));cursor+=18+width*height
    assert cursor==len(payload);return frames
def models():
    data=archives();result={}
    for language in (1,2):
        by_name={e['name']:e['payload'] for e in data[language]['entries']}
        for level in (1,2,3):
            common,_,_=expected_profile(parse_level(data[1]['entries'][17+level]['payload']),level)
            local=parse_level(data[language]['entries'][17+level]['payload'])
            for obj in common['objects']:
                if obj['class_id']!=9:continue
                translated=next(o for o in local['objects'] if o['glyph']==obj['glyph'])
                refs=[translated['animation']['hex']]+[next(f['value']['hex'] for f in translated['fields'] if f['native_offset']==off) for off in (0xe8,0xec)]
                result[language,level,obj['glyph']]=(obj,[animation(by_name[n]) for n in refs])
    return result
class Original(Base):
    def __init__(self,language):
        super().__init__('ru' if language==1 else 'en');self.language=language;self.world=0x1098 if self.ru else 0x1030;self.sight_mode=False
        source=json.loads((self.path/'analysis-manifest.json').read_text('utf-8-sig'))
        for name,sha in source['binary_sha256'].items():assert hashlib.sha256((self.path/(name+'.bin')).read_bytes()).hexdigest()==sha
        self.animation=0x1050 if self.ru else 0x1018;self.math=0x10c8 if self.ru else 0x1058
        self.clock=0x1130 if self.ru else 0x1078;self.scheduler=0x1038 if self.ru else 0x1010
        self.ids=dict(active=0x3845,wake=0x3747,map=0x1512,lookup=0x1435,sight=0x1276,spawn=0x136a,motion=0xa85,player=0x1b10,stat=0x1cc2,world_lookup=0x309,candidate=0xe13,read=0x26f,start=0x239,stop=0x2b6,repeat=0x265,cancel=0x214,next=0x54,get=0xdf) if self.ru else dict(active=0x3fae,wake=0x3eb0,map=0x1a1c,lookup=0x193f,sight=0x1780,spawn=0x1874,motion=0xf8f,player=0x20c4,stat=0x2276,world_lookup=0x3ed,candidate=0xef7,read=0x1905,start=0x18cf,stop=0x194c,repeat=0x161b,cancel=0x15ca,next=0xf62,get=0xfed)
        m=json.loads((OUT/'compiler'/('ru' if self.ru else 'en')/'analysis-manifest.json').read_text('utf-8-sig'))
        compiler=(OUT/'compiler'/('ru' if self.ru else 'en')/'1000.bin').read_bytes();assert hashlib.sha256(compiler).hexdigest()==m['binary_sha256']['1000']
        assert m['sha256_before']==m['sha256_after'];self.c.mem_write(0xd0000,compiler)
        asm=(self.path/'evidence.asm').read_text();self.allowed=set()
        for address in (self.ids['active'],self.ids['player'],self.ids['stat']):
            start=asm.index(f'FUNCTION {self.segment:04x}:{address:04x}');end=asm.find('FUNCTION ',start+10)
            body=asm[start:end if end>=0 else None]
            self.allowed|={(int(seg,16),int(ip,16)) for seg,ip in re.findall(r'CALLF 0x([0-9a-f]+):([0-9a-f]+)',body)}
    def ret(self,cleanup=0):
        c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        for reg,n in ((R.UC_X86_REG_SP,sp+4+cleanup),(R.UC_X86_REG_CS,cs),(R.UC_X86_REG_IP,ip)):c.reg_write(reg,n)
    def image(self,kind,index):return 0x1000+kind*0x1000+index*32
    def hook(self,c,address,size,context):
        cs=c.reg_read(R.UC_X86_REG_CS);ip=c.reg_read(R.UC_X86_REG_IP);sp=c.reg_read(R.UC_X86_REG_SP)
        if self.sight_mode:
            if cs==self.segment and ip==self.ids['sight']:return
            if cs==self.world and ip==self.ids['world_lookup']:
                args=struct.unpack('<5H',c.mem_read(0x30000+sp+4,10));base=args[3]*16+args[2]
                assert struct.unpack('<i',c.mem_read(base+0x16,4))[0]==-100
                self.sight_planes.append(args[4]);entry=self.sight_script[min(len(self.sight_planes)-1,len(self.sight_script)-1)]
                seg=0x4000 if entry=='self' else 0x5000 if entry=='hero' else 0x6000
                c.mem_write(0x60015,struct.pack('<H',8 if entry=='pass8' else 16 if entry=='pass16' else 0))
                c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,seg);self.ret();return
        if cs==0x1000 and ip==0x5cb3:
            c.reg_write(R.UC_X86_REG_CS,0xd000);c.reg_write(R.UC_X86_REG_IP,ip);return
        if cs in (self.segment,0xd000,0x6000) and not (cs==self.segment and ip in (self.ids['map'],self.ids['lookup'],self.ids['sight'],self.ids['spawn'],self.ids['motion'])):return
        if cs==self.segment:
            if ip==self.ids['map']:
                args=struct.unpack('<4H',c.mem_read(0x30000+sp+4,8));self.map_writes.append((args[2]&255,args[3]));self.ret();return
            if ip==self.ids['lookup']:
                c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0x6000);self.ret();return
            if ip==self.ids['sight']:
                c.reg_write(R.UC_X86_REG_AX,self.row['visible']);self.ret();return
            if ip==self.ids['spawn']:
                self.spawned+=1;self.spawn_glyph=struct.unpack('<H',c.mem_read(0x30000+sp+8,2))[0]&255
                c.reg_write(R.UC_X86_REG_AX,0);self.ret();return
            if ip==self.ids['motion']:self.ret();return
        if cs==self.world and ip==self.ids['world_lookup']:
            c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0x5000 if self.row['front_class']==1 else 0x6000);self.ret();return
        if cs==self.world and ip==self.ids['candidate']:
            # Controlled collision-resolved trial: advance one half cell on
            # the north axis. Actual resolver/movement are N09/N10 oracles.
            args=struct.unpack('<4H',c.mem_read(0x30000+sp+4,8));base=args[3]*16+args[2]
            z=struct.unpack('<i',c.mem_read(base+8,4))[0];c.mem_write(base+8,struct.pack('<i',z+240));self.ret();return
        if cs==self.math and ip==0x1c5:
            c.reg_write(R.UC_X86_REG_AX,0);self.ret();return
        if cs==self.animation and ip in (self.ids['next'],self.ids['get']):
            args=struct.unpack('<10H' if ip==self.ids['next'] else '<5H',c.mem_read(0x30000+sp+4,20 if ip==self.ids['next'] else 10))
            kind=args[0]//0x1000-1;assert kind in (0,1,2)
            if ip==self.ids['next']:
                pointer=args[9]*16+args[8];index=struct.unpack('<h',c.mem_read(pointer,2))[0]+1
                if index>=len(self.sequences[kind]):c.mem_write(pointer,b'\xff\xff');c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0);self.ret();return
                c.mem_write(pointer,struct.pack('<h',index));interval=args[7]*16+args[6]
            else:index=max(0,min(word(args[2]),len(self.sequences[kind])-1));interval=args[4]*16+args[3]
            c.mem_write(interval,struct.pack('<h',self.sequences[kind][index][0]));c.reg_write(R.UC_X86_REG_AX,self.image(kind,index));c.reg_write(R.UC_X86_REG_DX,0xc000);self.ret();return
        if cs==self.clock and ip in (self.ids['read'],self.ids['start'],self.ids['stop']):
            if ip==self.ids['read']:
                elapsed=self.row['elapsed']&0xffffffff;c.reg_write(R.UC_X86_REG_AX,elapsed&65535);c.reg_write(R.UC_X86_REG_DX,elapsed>>16)
            self.ret();return
        if cs==self.scheduler and ip in (self.ids['repeat'],self.ids['cancel']):
            if ip==self.ids['cancel']:self.cancelled+=1
            self.ret();return
        if cs==0xb000 and ip==0x100:
            args=struct.unpack('<6H',c.mem_read(0x30000+sp+4,12));mask=struct.unpack('<I',c.mem_read(args[3]*16+args[2],4))[0]
            assert mask==0x40000 and args[4:6]==(0,0x4000);self.melee+=1
            # Preserve original stack arguments and execute real player event
            # and stat handlers, including signed clamp and health0 refusal.
            c.reg_write(R.UC_X86_REG_CS,self.segment);c.reg_write(R.UC_X86_REG_IP,self.ids['player']);return
        assert (cs,ip) in self.allowed,('Unhandled original boundary',hex(cs),hex(ip))
        c.reg_write(R.UC_X86_REG_AX,1);self.ret()
    def callback(self,row,model):
        self.row=row;obj,self.sequences=model;c=self.c;self.map_writes=[];self.spawned=self.melee=self.cancelled=0
        c.mem_write(0x40000,bytes(0x100));c.mem_write(0x50000,bytes(0x100));c.mem_write(0x60000,bytes(0x100));c.mem_write(0x70000,bytes(0x800))
        c.mem_write(0x70000,struct.pack('<3h',64,1,64));c.mem_write(0x70442,struct.pack('<HH',0,0x5000));c.mem_write(0x70738,struct.pack('<h',480));c.mem_write(0x70742,struct.pack('<i',64))
        c.mem_write(0x3266a if self.ru else 0x3271a,struct.pack('<HH',0,0x7000))
        c.mem_write(0x40012,struct.pack('<H',9));c.mem_write(0x40015,struct.pack('<H',row['flags_in']));c.mem_write(0x40019,struct.pack('<h',row['height']))
        c.mem_write(0x40026,struct.pack('<iiih',2640,-row['height'],2160,0));c.mem_write(0x4005c,struct.pack('<H',row['glyph']))
        c.mem_write(0x50010,struct.pack('<HH',0x1000,1));c.mem_write(0x5005e,struct.pack('<4h',row['hero_in'],0,100,100))
        c.mem_write(0x50026,struct.pack('<iiih',2640,-300,2640 if row['front_class']==1 else 6000,180))
        tclass=(0 if row['scenario']==0 else 3) if row['health_in']<=0 else row['front_class']
        c.mem_write(0x31014,struct.pack('<HH',0x100,0xb000));c.mem_write(0x60012,struct.pack('<H',tclass))
        c.mem_write(0x400a2,struct.pack('<hh',row['state_in'],row['health_in']))
        c.mem_write(0x400a6,struct.pack('<5h',scalar(obj,0xa6),scalar(obj,0xa8),row['shots_in'],row['course_in'],scalar(obj,0xae)))
        c.mem_write(0x400e7,bytes([scalar(obj,0xe7)]))
        counter=row['next_in'] if row['state_in']==4 else row['next_in']-1;c.mem_write(0x40050,struct.pack('<h',counter))
        for kind,off in enumerate((0x48,0xe8,0xec)):
            pointer=0x1000*(kind+1);c.mem_write(0x40000+off,struct.pack('<HH',pointer,0xa000));c.mem_write(0xa0000+pointer,struct.pack('<h',len(self.sequences[kind])))
            for index,(_,width,height) in enumerate(self.sequences[kind]):c.mem_write(0xc0000+self.image(kind,index),struct.pack('<2h',width,height))
        c.mem_write(0x40044,struct.pack('<HH',self.image(row['kind_in'],row['cursor_in']),0xc000))
        c.mem_write(0x40052,struct.pack('<h',self.sequences[row['kind_in']][row['cursor_in']][0]))
        for reg,value in ((R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<4H',0xfff0,0x6000,0,0x4000));c.emu_start(self.segment*16+self.ids['active'],0x6fff0,count=30000)
        assert c.reg_read(R.UC_X86_REG_CS)==0x6000 and c.reg_read(R.UC_X86_REG_SP)==0xf004
        state,health=struct.unpack('<2h',c.mem_read(0x400a2,4));counter=struct.unpack('<h',c.mem_read(0x40050,2))[0]
        image,segment=struct.unpack('<2H',c.mem_read(0x40044,4));assert segment==0xc000
        kind=(image-0x1000)//0x1000;cursor=((image-0x1000)%0x1000)//32
        actual=dict(state_out=state,health_out=health,next_out=counter if state==4 else counter+1,kind_out=kind,cursor_out=cursor,
                    course_out=struct.unpack('<h',c.mem_read(0x400ac,2))[0],shots_out=struct.unpack('<h',c.mem_read(0x400aa,2))[0],
                    flags_out=struct.unpack('<H',c.mem_read(0x40015,2))[0],interval=struct.unpack('<h',c.mem_read(0x40052,2))[0],melee=self.melee,ranged=self.spawned,
                    dead=self.cancelled,corpse_out=int(bool(self.cancelled)),hero_out=struct.unpack('<h',c.mem_read(0x5005e,2))[0])
        if self.cancelled:actual['plane']=self.map_writes[-1][1];assert self.map_writes==[(32,0),(row['glyph'],0 if row['scenario'] else 1)]
        else:actual['plane']=-1
        for key,value in actual.items():assert value==row[key],(self.language,row['level'],row['glyph'],row['scenario'],key,value,row[key],row)
        if self.spawned:assert self.spawn_glyph==scalar(obj,0xe7)
    def sight_query(self,script):
        self.sight_mode=True;self.sight_script=script;self.sight_planes=[];c=self.c
        for reg,value in ((R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<6H',0xfff0,0x6000,0,0x4000,0,0x5000))
        try:c.emu_start(self.segment*16+self.ids['sight'],0x6fff0,count=500000)
        finally:self.sight_mode=False
        assert c.reg_read(R.UC_X86_REG_CS)==0x6000 and c.reg_read(R.UC_X86_REG_SP)==0xf004
        return c.reg_read(R.UC_X86_REG_AX),self.sight_planes
    def due_word(self,elapsed,delay):
        c=self.c;bp=0xf800;c.mem_write(0x3f806,struct.pack('<2H',0,0x4000));c.mem_write(0x400a8,struct.pack('<h',delay))
        for reg,value in ((R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_BP,bp),(R.UC_X86_REG_EAX,elapsed&65535),(R.UC_X86_REG_EDX,elapsed>>16)):c.reg_write(reg,value)
        start,yes,no=(0x3ab4,0x3add,0x3b86) if self.ru else (0x421d,0x4246,0x42ef);branch=[]
        def stop(cpu,address,size,context):
            if address in (self.segment*16+yes,self.segment*16+no):branch.append(address==self.segment*16+yes);cpu.emu_stop()
        h=c.hook_add(UC_HOOK_CODE,stop)
        try:c.emu_start(self.segment*16+start,0xfffff,count=100)
        finally:c.hook_del(h)
        assert len(branch)==1;return int(branch[0])
    def damage(self,health,damage):
        c=self.c;c.mem_write(0x400a2,struct.pack('<hh',1,health));c.mem_write(0x50000,struct.pack('<I',0x40000));c.mem_write(0x6005c,struct.pack('<h',damage))
        for reg,value in ((R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<8H',0xfff0,0x6000,0,0x4000,0,0x5000,0,0x6000))
        c.emu_start(self.segment*16+(0x37cf if self.ru else 0x3f38),0x6fff0,count=300)
        assert c.reg_read(R.UC_X86_REG_CS)==0x6000;return struct.unpack('<h',c.mem_read(0x400a4,2))[0]
def main():
    before=originals();model=models();fields=trace_fields();rows=[dict(zip(fields,json.loads(line),strict=True)) for line in (OUT/'callbacks.jsonl').read_text().splitlines()]
    report=dict(task='N17',passed=False,callback_records=len(rows),languages={},whole_DOS_session=False)
    rng=random.Random(171029);vectors=[(0,t,d,0,0) for t in (0,1,39999,40000,999999,1000000,0xffffffff) for d in (-32768,-1,0,3,99,32767)]
    vectors += [(1,rng.randint(-32768,32767),rng.randint(-32768,32767),0,0) for _ in range(2000)]
    vectors += [(1,0,d,0,0) for d in (-32768,-1,0,1,32767)]
    result=subprocess.run([ROOT/'build/nds/n17/ai_combat_dump.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),text=True,capture_output=True,check=True)
    expected=list(map(int,result.stdout.split()));assert len(expected)==len(vectors)
    for language in (1,2):
        native=Original(language);count=0
        for row in rows:
            if row['language']!=language:continue
            native.callback(row,model[language,row['level'],row['glyph']]);count+=1
        for v,value in zip(vectors,expected):assert (native.due_word(v[1],v[2]) if v[0]==0 else native.damage(v[1],v[2]))==value,(language,v,value)
        scripts=[(['self','self','hero'],1,[0,0,0]),(['pass8','pass16','hero'],1,[0,1,0]),(['block'],0,[0]),(['pass8','block'],0,[0,1]),(['hero'],1,[0]),(['self','block'],0,[0,0]),(['self'],0,[0]*5000)]
        for script,visible,planes in scripts:assert native.sight_query(script)==(visible,planes),(language,script)
        report['languages'][str(language)]=dict(callbacks=count,groups=len({(r['level'],r['glyph']) for r in rows if r['language']==language}),burst_thresholds=42,damage_words=2005,sight_scripts=7)
    assert before==originals();report.update(passed=True,original_sha256_before=before,original_sha256_after=originals(),
        controlled=['source animation accessors from verified original RES metadata','front lookup and source sight result in full callback','scripted world lookup targets for sight body','heading and collision trial services','N09 motion call boundary','burst stopwatch sample/reset','projectile allocation','audio/UI services'],
        executed=['original full active callback control flow','original far copy helper CLD/REP/RETF8','nested original player event/stat signed clamp','state/frame/course/shot counters','death height/flags and map-write requests','source sight body100-unit double lookup/self/hero/flags/5000 bound'],
        limitations=['Geometry/motion were independently verified in N09/N10, not re-executed here','Native resource heap/far layout, audio/UI drivers and full DOS/DPMI session are not claimed'])
    (OUT/'original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()

"""Execute G15 native decision/cleanup slices, with named external boundaries."""
import hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,'scripts')
from verify_ai_original import Original,R,UC_HOOK_CODE

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 report={'task':'G15','status':'passed','whole_DOS_session':False,'script_sha256':sha(__file__),'languages':{}}
 for lang in ('ru','en'):
  o=Original(lang);c=o.c;c.mem_write(o.seg*16,(o.path/'actor.bin').read_bytes());ru=o.ru
  addresses=dict(start=0x3ab4,end=0x3b86,sight=0x1276,spawn=0x136a,reset=0x3b58,death=0x3e31,map=0x1512,lookup=0x1435) if ru else dict(start=0x421d,end=0x42ef,sight=0x1780,spawn=0x1874,reset=0x42c1,death=0x459a,map=0x1a1c,lookup=0x193f)
  results={};active='';out={};sight=1;tclass=0
  def hook(cpu,address,size,unused):
   nonlocal out
   cs=cpu.reg_read(R.UC_X86_REG_CS);ip=address-cs*16;sp=cpu.reg_read(R.UC_X86_REG_SP)
   if cs==o.seg and ip==addresses['end']:cpu.emu_stop();return
   if active=='attack':
    if cs==o.seg and ip==(0x3e11 if ru else 0x457a):cpu.emu_stop();return
    if cs==(0x1050 if ru else 0x1018):
     out['animation_calls']=out.get('animation_calls',0)+1
     isnext=ip==(0x54 if ru else 0x0f62)
     cpu.reg_write(R.UC_X86_REG_AX,0 if ended and out['animation_calls']==1 and isnext else 1);cpu.reg_write(R.UC_X86_REG_DX,0);o.ret()
    elif cs==0x6000:
     args=struct.unpack('<6H',cpu.mem_read(0x30000+sp+4,12));off,seg=args[2:4];out['mask']=struct.unpack('<I',cpu.mem_read(seg*16+off,4))[0];out['source']=list(args[4:6]);o.ret()
   if active=='burst':
    if cs==o.seg and ip==addresses['sight']:cpu.reg_write(R.UC_X86_REG_AX,sight);o.ret()
    elif cs==o.seg and ip==addresses['spawn']:out['spawn']=struct.unpack('<H',cpu.mem_read(0x30000+sp+8,2))[0]&255;o.ret()
    elif cs!=o.seg:cpu.reg_write(R.UC_X86_REG_AX,1);o.ret()
    if cs==o.seg and ip==addresses['reset']:out['reset']=True
   elif active=='player':
    if cs==o.seg and ip==(0x1caa if ru else 0x225e):cpu.emu_stop();return
    if cs==o.seg and ip==(0x1cc2 if ru else 0x2276):
     args=struct.unpack('<3Hh',cpu.mem_read(0x30000+sp+4,8));out['stat']=args[2];out['damage_delta']=args[3];o.ret()
    elif cs!=o.seg:o.ret()
   elif active=='death':
    if cs==o.seg and ip==addresses['map']:
     _,_,glyph,plane=struct.unpack('<HHHH',cpu.mem_read(0x30000+sp+4,8));out.setdefault('maps',[]).append([glyph&255,plane]);o.ret()
    elif cs==o.seg and ip==addresses['lookup']:cpu.reg_write(R.UC_X86_REG_AX,0);cpu.reg_write(R.UC_X86_REG_DX,0x5000);o.ret()
    elif cs!=o.seg:
     out.setdefault('external',[]).append(f'{cs:04x}:{ip:04x}')
     # Final native owner removal follows text and sound_stop.
     if len(out['external'])==3:cpu.emu_stop()
     else:o.ret()
  h=c.hook_add(UC_HOOK_CODE,hook)
  def setup():
   for reg,v in ((R.UC_X86_REG_CS,o.seg),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,v)
   c.mem_write(0x3f806,struct.pack('<HH',0,0x4000));c.mem_write(0x3f7ee,struct.pack('<HH',0,0x5000));c.mem_write(0x400e7,b'4');c.mem_write(0x400ae,struct.pack('<h',24))
  active='burst';count=0
  for delay in (3,99,-1):
   for elapsed in (0,39999,40000,999999,1000000,0xffffffff):
    for limit in (0,4):
     for shots in (0,3,4):
      for cls in (0,8,9):
       for sight in (0,1):
        setup();out={};c.mem_write(0x400a6,struct.pack('<hhh',limit,delay,shots));c.mem_write(0x50012,struct.pack('<H',cls));c.reg_write(R.UC_X86_REG_EAX,elapsed&65535);c.reg_write(R.UC_X86_REG_EDX,elapsed>>16)
        c.emu_start(o.seg*16+addresses['start'],0xfffff,count=300)
        due=elapsed//10000>(delay&0xffffffff);shoot=due and shots<limit and cls not in (8,9) and sight
        assert ('spawn' in out)==bool(shoot),(lang,delay,elapsed,limit,shots,cls,sight,out)
        assert bool(out.get('reset'))==bool(due and not shoot)
        assert struct.unpack('<h',c.mem_read(0x400aa,2))[0]==(shots+1 if shoot else (0 if due else shots))
        if shoot:assert out['spawn']==52 and struct.unpack('<h',c.mem_read(0x400ac,2))[0]==24
        count+=1
  active='attack';attacks=[]
  for state in (1,2,3):
   for ended in (False,True):
    setup();out={};c.mem_write(0x400a2,struct.pack('<h',state));c.mem_write(0x40050,struct.pack('<h',5));c.mem_write(0x50010,struct.pack('<H',0x1000));c.mem_write(0x31014,struct.pack('<HH',0,0x6000))
    c.emu_start(o.seg*16+(0x3910 if ru else 0x4079),0xfffff,count=300)
    assert bool(out.get('mask'))==ended,(lang,state,ended,out)
    if ended:assert out['mask']==0x40000 and out['source']==[0,0x4000]
    assert struct.unpack('<h',c.mem_read(0x400a2,2))[0]==2
    if state==1:assert c.mem_read(0x40050,2)==b'\xff\xff'
    attacks.append(dict(state=state,ended=ended,**out))
  active='player';player_cases=[]
  for cls in (8,9):
   for health in (0,1):
    for damage in (-123,0,999):
     setup();out={};c.mem_write(0x3f80a,struct.pack('<HHHH',0,0x6000,0,0x5000));c.mem_write(0x60000,struct.pack('<I',0x40000));c.mem_write(0x4005e,struct.pack('<h',health));c.mem_write(0x50012,struct.pack('<H',cls));c.mem_write(0x5005c,struct.pack('<h',damage))
     c.emu_start(o.seg*16+(0x1c5a if ru else 0x220e),0xfffff,count=100)
     assert ('damage_delta' in out)==bool(health)
     if health:assert out['stat']==0 and out['damage_delta']==(-damage if cls==8 else -5)
     assert c.mem_read(0x60000,4)==bytes(4)
     player_cases.append(dict(source_class=cls,health=health,damage_field=damage,**out))
  active='death';corpses=[]
  for cls in (0,2,7,9):
   setup();out={};c.mem_write(0x50012,struct.pack('<H',cls));c.mem_write(0x4005c,b'\x80');c.mem_write(0x40015,struct.pack('<H',8));c.mem_write(0x40019,struct.pack('<H',250))
   c.emu_start(o.seg*16+addresses['death'],0xfffff,count=250)
   assert out['maps']==[[32,0],[128,1 if cls==0 else 0]],out
   assert struct.unpack('<H',c.mem_read(0x40015,2))[0]==24
   assert c.mem_read(0x40019,2)==b'\0\0' and len(out['external'])==3
   corpses.append(dict(tmap_class=cls,**out))
  c.hook_del(h)
  manifest=json.loads((o.path/'analysis-manifest.json').read_text(encoding='utf-8-sig'));assert sha(manifest['sample'])==manifest['sha256_after']==manifest['sha256_before']
  report['languages'][lang]=dict(burst_vectors=count,attack_vectors=attacks,player_melee_vectors=player_cases,corpse_vectors=corpses,manifest=manifest,manifest_sha256=sha(o.path/'analysis-manifest.json'))
 report['limits']='Native melee/hurt control flow and event mask/source, timer extraction, unsigned DIV/threshold, signed burst limit, front-class guard, counter increment/reset, projectile glyph, course reset, and death map/flags/height executed. Sight, animation, sound, spawn allocation, timer reset/start, text, stat-update and owner boundaries intercepted. Player stat0 delta checked before the stat updater (G12/G13 cover its arithmetic). No full DOS session or physical GBA.'
 Path('analysis/g15-original-verification.json').write_text(json.dumps(report,indent=2)+'\n');print('G15 original burst and corpse branches passed')
if __name__=='__main__':main()


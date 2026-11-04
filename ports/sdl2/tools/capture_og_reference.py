"""Isolated Old Gold DOS reference; researched game-state keyboard injection."""
import sys,os,json,shutil,subprocess,time,struct,argparse,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/originals'))
from dos_reference import Api,sha,capture,wait_for
parser=argparse.ArgumentParser();parser.add_argument('--label',default='03');parser.add_argument('--port',type=int,default=18740);args=parser.parse_args()
out=ROOT/f'research/evidence/source-port/sp07-render/og-dos-{args.label}';out.mkdir(parents=True,exist_ok=False)
source=ROOT/'originals/Old_Gold_3D';before={p.name:sha(p) for p in source.iterdir() if p.is_file()}
shutil.copytree(source,out/'game')
dosbox=Path(os.environ['LOCALAPPDATA'])/'Programs/DOSBox-Staging/wasdify-webserver-20261024/dosbox.exe'
config=f'''[sdl]
fullscreen=false
[dosbox]
machine=svga_s3
memsize=32
[cpu]
core=normal
cputype=pentium
cpu_cycles=60000
cpu_cycles_protected=60000
[mixer]
nosound=true
[webserver]
webserver_enabled=on
webserver_bind_address=127.0.0.1
webserver_port={args.port}
[autoexec]
mount c "{out/'game'}"
c:
OG.EXE -n
'''
(out/'dosbox.conf').write_text(config)
a=Api(args.port);report=dict(source_sha256_before=before,frames=[],address_system='OG real-mode linear RAM; DS anchor Not same device at Ghidra DS:189d')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
p=subprocess.Popen([str(dosbox),'--noprimaryconf','--nolocalconf','-conf',str(out/'dosbox.conf'),'-noconsole'],cwd=out,env=env,stdout=(out/'stdout.log').open('wb'),stderr=(out/'stderr.log').open('wb'),creationflags=subprocess.CREATE_NO_WINDOW)
try:
 wait_for(lambda: a.get('dosbox/info'));time.sleep(2)
 m=a.read(0,0x100000);(out/'boot-memory.bin').write_bytes(m)
 matches=[];i=0
 while True:
  i=m.find(b'Not same device\0',i+1)
  if i<0:break
  matches.append(i)
 report['palette_anchors']=matches;print('Anchors',matches,flush=True)
 report['cpu']=json.loads(a.get('cpu/state'));print(report['cpu'],flush=True)
 report['frames'].append(capture(a,out,'boot'))
 if len(matches)!=1:raise RuntimeError('Ambiguous OG data anchor')
 base=matches[0]-0x189d;report['data_base']=hex(base)
 for step in range(7):
  key=base+0xc563;pending=struct.unpack('<H',a.read(key-3,2))[0]
  a.write(key+0x1c,b'\3');a.write(key-3,struct.pack('<HB',(pending+1)&65535,0x1c));time.sleep(.12);a.write(key+0x1c,bytes([a.read(key+0x1c,1)[0]&~1]));time.sleep(1.5)
  print('enter',step,flush=True)
 m=a.read(0,0x100000);(out/'memory-6.bin').write_bytes(m)
 def far(at):
  off,seg=struct.unpack_from('<HH',m,at);return seg*16+off
 surface=far(base+0xce92);report['surface_linear']=hex(surface)
 pose=struct.unpack_from('<iii',m,base+0x1fea);hits=[];pos=0
 while True:
  pos=m.find(struct.pack('<iii',*pose),pos+1)
  if pos<0:break
  if pos>=0x26 and struct.unpack_from('<H',m,pos-0x26+0x12)[0]==1:hits.append(pos-0x26)
 if len(hits)!=1:raise RuntimeError('Ambiguous OG player')
 hero=hits[0];report['player_linear']=hex(hero);report['control_poses']=[];report['traps']=[]
 from native_draw_trap import hold,release
 from check_render import png,read_archive
 palette=dict(read_archive((source/'OLD_GOLD.RES').read_bytes()))[b'GAMEPAL']
 for heading in [83,0,45,135]:
  a.write(hero+0x32,struct.pack('<h',heading));time.sleep(.3)
  trap=hold(a,m,'og',0)
  try:selected=a.read(0,len(m));frame=selected[surface:surface+64000]
  finally:release(a,trap)
  report['traps'].append(trap);(out/f'heading-{heading}-memory.z').write_bytes(zlib.compress(selected,6));(out/f'heading-{heading}.idx').write_bytes(frame)
  png(out/f'heading-{heading}.png',frame,320,200,palette)
  report['control_poses'].append(dict(heading=heading,pose=pose,memory_file=f'heading-{heading}-memory.z'))
  print('held',heading,trap,flush=True)
finally:
 p.terminate();p.wait(timeout=5);after={x.name:sha(x) for x in source.iterdir() if x.is_file()};report['source_sha256_after']=after;assert before==after;(out/'report.json').write_text(json.dumps(report,indent=2))

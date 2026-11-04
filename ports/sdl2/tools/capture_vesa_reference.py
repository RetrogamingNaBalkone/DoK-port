"""Capture original Kremlin VESA mode state through an isolated DOS session."""
import sys,os,json,shutil,subprocess,time,struct,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/originals'))
from dos_reference import Api,sha,wait_for,key,ANCHOR
parser=argparse.ArgumentParser();parser.add_argument('--headings',default='0,45,135');parser.add_argument('--machine',default='svga_s3',choices=['svga_s3','svga_et4000','vesa_oldvbe']);parser.add_argument('--mode',required=True,choices=['vga','auto','1','2','3','4']);parser.add_argument('--trap',action='store_true');parser.add_argument('--port',type=int,default=18737);parser.add_argument('--controls',action='store_true');parser.add_argument('--language',choices=['ru','en'],default='ru');parser.add_argument('--level',type=int,choices=[1,2,3],default=1);parser.add_argument('--label',default='04');args=parser.parse_args()
out=ROOT/f'research/evidence/source-port/sp07-render/{args.language}-level{args.level}-{args.mode}-{args.label}';out.mkdir(parents=True,exist_ok=False)
delta=0 if args.language=='ru' else 0xb0;prefix='PKR' if args.language=='ru' else 'PKE';source=ROOT/'originals/pkr'/('RUS' if args.language=='ru' else 'ENG');before={p.name:sha(p) for p in source.iterdir() if p.is_file()};shutil.copytree(source,out/'game')
dosbox=Path(os.environ['LOCALAPPDATA'])/'Programs/DOSBox-Staging/wasdify-webserver-20261024/dosbox.exe'
config=f'''[sdl]
fullscreen=false
[dosbox]
machine={args.machine}
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
{prefix}.EXE -n {'' if args.mode=='vga' else '-r'+('' if args.mode=='auto' else args.mode)}
'''
(out/'dosbox.conf').write_text(config);a=Api(args.port);report=dict(source_sha256_before=before,mode=args.mode)
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
p=subprocess.Popen([str(dosbox),'--noprimaryconf','--nolocalconf','-conf',str(out/'dosbox.conf'),'-noconsole'],cwd=out,env=env,stdout=(out/'stdout.log').open('wb'),stderr=(out/'stderr.log').open('wb'),creationflags=subprocess.CREATE_NO_WINDOW)
try:
 wait_for(lambda:a.get('dosbox/info'))
 def data():
  m=a.read(0,0x1000000)
  return m.find(ANCHOR)-0x1082-delta if m.count(ANCHOR)==1 else None
 base=wait_for(data);report['data_base']=hex(base)
 for i in range(2):key(a,base,delta,0x1c);time.sleep(1.5)
 for i in range(args.level-1):key(a,base,delta,0x50);time.sleep(.2)
 for i in range(4):key(a,base,delta,0x1c);time.sleep(1.5)
 time.sleep(5)
 if args.mode=='4':key(a,base,delta,0x1c);time.sleep(2)
 m=a.read(0,0x2000000);(out/'memory.bin').write_bytes(m)
 report['video_dimensions']=struct.unpack_from('<HH',m,base+delta+0xc1fc)
 report['camera']=dict(D=struct.unpack_from('<i',m,base+delta+0xaa16)[0],width_height_center=struct.unpack_from('<hhhh',m,base+delta+0xaa1a),clip=struct.unpack_from('<iiii',m,base+delta+0xaa22),scale=struct.unpack_from('<h',m,base+delta+0xaa32)[0],focal=struct.unpack_from('<h',m,base+(0x934 if args.language=='ru' else 0xa44))[0],pose=struct.unpack_from('<iii',m,base+delta+0xaa46))
 report['cpu']=json.loads(a.get('cpu/state'));report['dos']=json.loads(a.get('dos/internals'))
 # Resolve protected-mode far pointer through measured LDT, never selector*16.
 ds=report['cpu']['registers']['ss']; candidates=[];pos=0
 while True:
  pos=m.find(struct.pack('<HB',base&65535,(base>>16)&255),pos+1)
  if pos<0:break
  d=m[pos-2:pos+6]
  if d[5]&0xf0==0xf0 and d[7]==base>>24:candidates.append(pos-2)
 off,sel=struct.unpack_from('<HH',m,base+delta+0xb53a)
 candidates=[at for at in candidates if m[at-(ds&~7)+(sel&~7)+5]&0xf0==0xf0]
 if len(candidates)!=1:raise RuntimeError('Ambiguous valid DS/surface descriptor pair')
 ldt=candidates[0]-(ds&~7);off,sel=struct.unpack_from('<HH',m,base+delta+0xb53a);descriptor=ldt+(sel&~7);d=m[descriptor:descriptor+8]
 address=int.from_bytes(d[2:4],'little')+(d[4]<<16)+(d[7]<<24)+off;w,h=report['video_dimensions'];v=m[address:address+w*h]
 if len(v)!=w*h:raise RuntimeError('Surface outside RAM dump')
 capture_address=0xa0000 if args.mode=='vga' else address
 report['capture_linear']=hex(capture_address)
 samples={}; coverage={};
 for sample in range(80):
  frame=a.read(capture_address,w*h);digest=__import__('hashlib').sha256(frame).hexdigest();samples[digest]=samples.get(digest,0)+1
  coverage[digest]=(sum(x!=0 for x in frame[:w*(report['camera']['clip'][3]+1)]),frame)
  time.sleep(.02)
 best=max(coverage,key=lambda k:(coverage[k][0],samples[k]));v=coverage[best][1]
 report['surface_sampling']=dict(samples=80,selected_sha256=best,selected_occurrences=samples[best],selected_nonzero=coverage[best][0],unique_frames=len(samples))
 (out/'surface.idx').write_bytes(v)
 sys.path.insert(0,str(ROOT/'ports/sdl2/tools'));from check_render import png,read_archive
 png(out/'surface.png',v,w,h,dict(read_archive((source/(prefix+'.RES')).read_bytes()))[f'GAMEP{args.level}'.encode()])
 report['surface_address']=dict(runtime_ds=hex(ds),ds_descriptor=hex(candidates[0]),ldt_linear=hex(ldt),surface_selector=hex(sel),surface_offset=hex(off),surface_descriptor=hex(descriptor),descriptor_bytes=d.hex(),surface_linear=hex(address))
 if args.controls:
  pattern=struct.pack('<iii',*report['camera']['pose']);hits=[];pos=0
  while True:
   pos=m.find(pattern,pos+1)
   if pos<0:break
   if pos>=0x26 and struct.unpack_from('<H',m,pos-0x26+0x12)[0]==1:hits.append(pos-0x26)
  if len(hits)!=1:raise RuntimeError('Ambiguous native player pose')
  hero=hits[0];report['player_linear']=hex(hero);report['control_poses']=[]
  for heading in map(int,args.headings.split(',')):
   a.write(hero+0x32,struct.pack('<h',heading));time.sleep(.3)
   if args.trap:
    from native_draw_trap import hold,release
    trap=hold(a,m,args.language,ldt)
    try:selected=a.read(0,len(m));frame=selected[address:address+w*h]
    finally:release(a,trap)
    sample=0;report.setdefault('traps',[]).append(trap)
   else:
    selected=None
    for sample in range(120):
     low=a.read(0,0x800000)
     if args.mode=='vga' and low[address:address+w*h]==low[0xa0000:0xa0000+w*h]:selected=low;break
     time.sleep(.01)
    if selected is None:raise RuntimeError('No completed surface matching displayed VGA frame')
    frame=selected[0xa0000:0xa0000+w*h]
   import zlib
   (out/f'heading-{heading}-memory.z').write_bytes(zlib.compress(selected+m[len(selected):],6))
   (out/f'heading-{heading}.idx').write_bytes(frame)
   report['control_poses'].append(dict(heading=heading,pose=report['camera']['pose'],sha256=__import__('hashlib').sha256(frame).hexdigest(),method='post-draw runtime trap; original epilogue restored' if args.trap else 'native player+32 heading; RAM snapshot offscreen surface equals displayed VGA bytes; simulation state retained',memory_file=f'heading-{heading}-memory.z',samples_until_complete=sample+1))
 print(json.dumps({k:v for k,v in report.items() if k not in ['source_sha256_before','dos']}),flush=True)
finally:
 p.terminate();p.wait(timeout=5);after={x.name:sha(x) for x in source.iterdir() if x.is_file()};report['source_sha256_after']=after;assert before==after;(out/'report.json').write_text(json.dumps(report,indent=2))

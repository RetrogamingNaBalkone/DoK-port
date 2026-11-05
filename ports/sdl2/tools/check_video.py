"""SP21 real Windows GL/software matrix: automatic projection and fullscreen canvas."""
from pathlib import Path
import configparser, hashlib, json, os, subprocess, struct, zlib
# Port PNG writer always emits RGB8, filter0 rows; reject any other format.
class Frame:
 def __init__(self,w,h,data):self.size=(w,h);self.data=data
 def crop(self,box):
  x,y,r,b=box;w,h=self.size
  return Frame(r-x,b-y,b''.join(self.data[(row*w+x)*3:(row*w+r)*3] for row in range(y,b)))
 def getbbox(self):return True if any(self.data) else None
class Image:
 @staticmethod
 def open(path):
  data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';pos=8;compressed=b''
  while pos<len(data):
   length=struct.unpack_from('>I',data,pos)[0];kind=data[pos+4:pos+8];body=data[pos+8:pos+8+length];pos+=length+12
   if kind==b'IHDR':w,h,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',body);assert (depth,color,compression,filtering,interlace)==(8,2,0,0,0)
   if kind==b'IDAT':compressed+=body
  raw=zlib.decompress(compressed);stride=w*3+1;assert len(raw)==stride*h and all(raw[y*stride]==0 for y in range(h))
  return Frame(w,h,b''.join(raw[y*stride+1:(y+1)*stride] for y in range(h)))
class ImageChops:
 @staticmethod
 def difference(a,b):
  assert a.size==b.size
  return Frame(*a.size,bytes(x^y for x,y in zip(a.data,b.data)))
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp08-video/checks'
EXE=ROOT/'build/sdl2/windows/dok_port.exe'

def main():
 global OUT
 OUT=ROOT/'research/evidence/source-port/sp21-video'
 OUT.mkdir(parents=True,exist_ok=True)
 profiles=[('pkr','originals/pkr/RUS/PKR.RES','LEVEL1'),('pke','originals/pkr/ENG/PKE.RES','LEVEL1'),('og','originals/Old_Gold_3D/OLD_GOLD.RES','LEVEL4')]
 results=[]
 def run(name,args,settings='',script='',frames=1):
  d=OUT/name;d.mkdir(exist_ok=True)
  (d/'settings.ini').write_text('[settings]\nversion=1\n[interface]\nfps=0\n'+settings,encoding='utf-8')
  (d/'script.txt').write_text(script,encoding='utf-8')
  command=[str(EXE),'--hidden','--user-dir',str(d),'--ui-script',str(d/'script.txt'),'--frames',str(frames),'--screenshot',str(d/'frame.png'),*args]
  r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=40,env=dict(os.environ,SDL_AUDIODRIVER='dummy'))
  log=(d/'port.log').read_text(encoding='utf-8')
  assert r.returncode==0 and 'OpenGL fallback:' not in log and 'Clean shutdown;' in log,(name,r.stderr,log)
  img=Image.open(d/'frame.png')
  assert img.getbbox(),name
  results.append({'name':name,'size':img.size,'sha256':hashlib.sha256((d/'frame.png').read_bytes()).hexdigest()})
  return img,log,d
 for dataset,res,level in profiles:
  common=['--res',res,'--level',level,'--skip-intro']
  for locale in ['ru','en']:
   for backend in ['gl','software']:
    for hud in ['classic','desktop']:
     classic_refs={}
     for w,h in [(800,600),(960,600),(960,540),(1000,600),(1440,600)]:
      name=f'{dataset}-{locale}-{backend}-{hud}-{w}x{h}'
      img,_,_=run(name,common+['--locale',locale,'--renderer',backend,'--hud',hud,'--size',f'{w}x{h}'])
      assert img.size==(w,h),(name,img.size)
      # Software is always 4:3; OpenGL retains its chosen widescreen aspect.
      if w*3>h*4:
       if backend=='software':
        x=(w-h*4//3)//2
        assert img.crop((0,0,x,h)).getbbox() is None and img.crop((w-x,0,w,h)).getbbox() is None,(name,'Software did not reset to 4:3')
       else:
        assert img.crop((0,0,8,h//2)).getbbox() and img.crop((w-8,0,w,h//2)).getbbox(),(name,'Pillarboxed GL world')
      if backend=='gl' and hud=='classic':
       pw=round(h*4/3);ph=h*(30 if dataset=='og' else 40)//200;x=(w-pw)//2;top=h-ph
       if w==pw:classic_refs[h]=img
       else:
        if h not in classic_refs:
         classic_refs[h],_,_=run(f'{dataset}-{locale}-classic-reference-{h}',common+['--locale',locale,'--renderer','gl','--hud','classic','--size',f'{pw}x{h}'])
        assert img.crop((x,top,x+pw,h)).data==classic_refs[h].crop((0,top,pw,h)).data,(name,'Classic panel differs from 4:3')
        left=img.crop((0,top,x,h)).data;right=img.crop((x+pw,top,w,h)).data
        colors=lambda data:set(zip(data[::3],data[1::3],data[2::3]))
        assert len(colors(left))>=(2 if dataset=='og' else 4) and len(colors(right))>=(2 if dataset=='og' else 4),(name,'Missing native panel texture in side extensions')
     print(dataset,locale,backend,hud,'PASS',flush=True)
  for hud in ['classic','desktop']:
   a,_,_=run(f'{dataset}-{hud}-fxaa-off',common+['--renderer','gl','--hud',hud,'--size','960x540','--fxaa','off'])
   b,_,_=run(f'{dataset}-{hud}-fxaa-on',common+['--renderer','gl','--hud',hud,'--size','960x540','--fxaa','on'])
   if hud=='classic':
    top=459 if dataset=='og' else 432
    assert a.crop((0,top,960,540)).data==b.crop((0,top,960,540)).data,'FXAA modified panel or side extensions'
  # Borderless must preserve the selected internal 4:3 frame, irrespective of desktop aspect.
  img,log,d=run(f'{dataset}-fullscreen43',common+['--renderer','gl','--hud','classic'],
    '[video]\nfullscreen=1\nfullscreen_width=800\nfullscreen_height=600\n',script='0 state fullscreen\n')
  assert 'size=800x600' in log,log
  w,h=img.size;fw=min(w,h*4//3);fh=fw*3//4;x=(w-fw)//2;y=(h-fh)//2
  if x:assert img.crop((0,0,x,h)).getbbox() is None,'Missing pillarbox'
  if y:assert img.crop((0,0,w,y)).getbbox() is None,'Missing letterbox'
  # F11 must configure the same internal canvas, and preserve the live world.
  _,log,_=run(f'{dataset}-f11',common+['--renderer','gl','--size','800x600'],
    script='0 state before\n1 key F11\n2 state fullscreen\n3 key F11\n4 state window\n',frames=5)
  states=[line for line in log.splitlines() if line.startswith('STATE')]
  assert len(states)==3 and all('size=800x600' in line for line in states),log
  assert len({line.split(' world=')[1].split()[0] for line in states})==1,'World replaced by F11'
  script='0 state before\n1 key Escape\n2 key Down\n3 key Return\n4 key Return\n5 key Right\n6 key Down\n7 key Down\n8 key Down\n9 key Return\n10 key Return\n11 key Escape\n12 key Escape\n13 key Escape\n14 state software\n'
  img,log,_=run(f'{dataset}-gl-to-software-fullscreen',common+['--renderer','gl','--size','1280x720'],
    '[video]\nfullscreen=1\n',script=script,frames=16)
  states=[line for line in log.splitlines() if line.startswith('STATE')]
  assert len(states)==2 and 'renderer=0' in states[1] and 'menu=0' in states[1],log
  assert len({line.split(' world=')[1].split()[0] for line in states})==1,'World replaced by backend switch'
  w,h=img.size;x=(w-h*4//3)//2
  if x>0:assert img.crop((0,0,x,h)).getbbox() is None and img.crop((w-x,0,w,h)).getbbox() is None,'Switched software retained wide aspect'
 # Menu through the same internal framebuffer.
 run('fullscreen-menu',['--renderer','gl'],'[video]\nfullscreen=1\nfullscreen_width=800\nfullscreen_height=600\n',script='0 key Down\n1 key Return\n2 key Return\n',frames=4)
 for option in ['--fov','--aspect']:
  d=OUT/('obsolete-'+option[2:]);d.mkdir(exist_ok=True)
  r=subprocess.run([str(EXE),'--hidden','--user-dir',str(d),option,'90'],cwd=ROOT,capture_output=True,text=True,timeout=10)
  assert r.returncode!=0 and 'automatic' in (d/'port.log').read_text(),option
 report={'schema':1,'contract':'SP21 automatic DOS projection and internal fullscreen canvas','exe_sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'frames':results,'frame_count':len(results),'limitations':['Physical mixed-DPI and controller checks not performed']}
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 compact={'schema':1,'exe_sha256':report['exe_sha256'],'frame_count':len(results),'datasets':['old_gold','kremlin_pkr','kremlin_pke'],'locales':['ru','en'],'renderers':['software','opengl'],'aspects':['4:3','16:10','16:9','5:3','12:5'],'software_aspect':'4:3 with bars, including live fullscreen GL-to-software transition PASS','fullscreen':'internal canvas, bars, menu, F11 world preservation PASS','cli':'legacy FOV/aspect rejected','report':str((OUT/'report.json').relative_to(ROOT)),'limitations':report['limitations']}
 (ROOT/'analysis/source-port/sp21-video-summary.json').write_text(json.dumps(compact,indent=2)+'\n',encoding='utf-8')
 print(len(results),'video frames PASS',flush=True)
if __name__=='__main__':main()

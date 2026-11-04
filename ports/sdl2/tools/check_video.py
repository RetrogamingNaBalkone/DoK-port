"""SP08 real Windows GL/software matrix and isolated persistent settings checks."""
from pathlib import Path
import configparser, hashlib, json, subprocess, struct, zlib
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
 OUT.mkdir(parents=True,exist_ok=True)
 report={'gl_frames':[],'software_frames':[],'software_modes':[],'fov_cases':[],'aspect_cases':[],'weapons':[]}
 profiles=[('ru','originals/pkr/RUS/PKR.RES','LEVEL1'),('en','originals/pkr/ENG/PKE.RES','LEVEL1'),('og','originals/Old_Gold_3D/OLD_GOLD.RES','LEVEL4')]
 def run(name,args=(),script='',settings='',frames=1):
  d=OUT/name;d.mkdir(exist_ok=True)
  # Reproduce this case independently of any previous run.
  (d/'settings.ini').write_text('[settings]\nversion=1\n[interface]\nfps=0\n'+settings)
  (d/'script.txt').write_text(script)
  command=[str(EXE),'--user-dir',str(d),'--ui-script',str(d/'script.txt'),'--frames',str(frames),'--screenshot',str(d/'frame.png'),*args]
  result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=45)
  (d/'stdout.log').write_text(result.stdout);(d/'stderr.log').write_text(result.stderr)
  assert result.returncode==0,(name,result.stderr)
  log=(d/'port.log').read_text();assert 'OpenGL fallback:' not in log,(name,log)
  return d,Image.open(d/'frame.png')
 for lang,res,level in profiles:
  common=['--res',res,'--level',level,'--skip-intro']
  for w,h in [(800,600),(960,600),(960,540),(1000,600)]:
   for hud in ['classic','desktop']:
    images=[]
    for fxaa in ['off','on']:
     name=f'{lang}-{w}x{h}-{hud}-{fxaa}'
     d,img=run(name,common+['--renderer','gl','--hud',hud,'--fxaa',fxaa,'--size',f'{w}x{h}'])
     assert img.size==(w,h)
     if hud=='classic':
      fw=min(w,h*4//3);fh=fw*3//4;left=(w-fw)//2;top=(h-fh)//2;sh=fh*(170 if lang=='og' else 160)//200
      if left:assert img.crop((0,0,left,h)).getbbox() is None
      if top:assert img.crop((0,0,w,top)).getbbox() is None
      images.append(img.crop((left,top+sh,left+fw,top+fh)))
     else:images.append(img.crop((64,h-26,69,h-23)))
     report['gl_frames'].append({'name':name,'size':[w,h],'sha256':hashlib.sha256((d/'frame.png').read_bytes()).hexdigest()})
    assert ImageChops.difference(*images).getbbox() is None,(lang,w,h,hud,'FXAA reached HUD')
  for backend in ['gl','software']:
   for hud in ['classic','desktop']:
    script=''.join('0 key '+c+'\n' for c in 'IDKFA')
    common_view=common+['--renderer',backend,'--hud',hud,'--size','800x600']
    _,plain=run(f'{lang}-{backend}-weapon-{hud}-plain',common_view)
    _,weapon=run(f'{lang}-{backend}-weapon-{hud}-shown',common_view,script)
    _,zoomed=run(f'{lang}-{backend}-weapon-{hud}-fov100',common_view+['--fov','100'],script)
    sh=(510 if lang=='og' else 480) if hud=='classic' else 600
    box=(300,sh-100,500,sh)
    a,b,c=[image.crop(box).data for image in [plain,weapon,zoomed]]
    mw,mh=(320,170 if lang=='og' else 160) if hud=='classic' else (256,160)
    mask_path=OUT/f'{lang}-{hud}.mask'
    subprocess.run([str(ROOT/'build/sdl2/windows/dok_video_mask_probe.exe'),str(ROOT/res),str(mw),str(mh),str(mask_path)],check=True)
    source=mask_path.read_bytes();mask=[]
    for y in range(box[1],box[3]):
     for x in range(box[0],box[2]):
      sx=int((x+0.5)*mw/800);sy=int((y+0.5)*mh/sh)
      if 1<=sx<mw-1 and 1<=sy<mh-1 and all(source[(sy+dy)*mw+sx+dx] for dy in [-1,0,1] for dx in [-1,0,1]):mask.append(((y-box[1])*200+x-box[0])*3)
    assert len(mask)>10,(lang,backend,hud,'Weapon absent')
    assert any(a[i:i+3]!=b[i:i+3] for i in mask),(lang,backend,hud,'Weapon not composited')
    assert all(b[i:i+3]==c[i:i+3] for i in mask),(lang,backend,hud,'FOV stretched weapon')
    report['weapons'].append({'dataset':lang,'backend':backend,'hud':hud,'opaque_weapon_interior_pixels':len(mask),'fov_invariant':True})
    panel_images=[]
    for fov in ['original','60','100']:
     name=f'{lang}-{backend}-{hud}-fov{fov}'
     d,img=run(name,common_view+['--fov',fov])
     panel_images.append(img.crop((0,sh,800,600)) if hud=='classic' else img.crop((64,574,69,577)))
     report['fov_cases'].append({'name':name,'sha256':hashlib.sha256((d/'frame.png').read_bytes()).hexdigest()})
    assert all(ImageChops.difference(panel_images[0],image).getbbox() is None for image in panel_images[1:])
    _,forced=run(f'{lang}-{backend}-{hud}-forced43',common+['--renderer',backend,'--hud',hud,'--aspect','4:3','--size','1000x600'])
    assert forced.crop((0,0,100,600)).getbbox() is None
    _,reference=run(f'{lang}-{backend}-{hud}-reference43',common_view)
    crop=forced.crop((100,0,900,600));changed=sum(crop.data[i:i+3]!=reference.data[i:i+3] for i in range(0,len(crop.data),3))
    assert changed<=32,(lang,backend,hud,'Forced4:3 viewport changed projection',changed)
    report['aspect_cases'].append({'dataset':lang,'backend':backend,'hud':hud,'forced43_differing_pixels':changed})
  for w,h in [(800,600),(960,600),(960,540),(1000,600)]:
   for mode in ['320x200','640x400','640x480','800x600','1024x768']:
    for hud in ['classic','desktop']:
     name=f'{lang}-sw-{mode}-{w}x{h}-{hud}'
     d,img=run(name,common+['--renderer','software','--software-mode',mode,'--hud',hud,'--size',f'{w}x{h}'])
     assert img.size==(w,h)
     if hud=='classic':
      left=(w-min(w,h*4//3))//2
      if left:assert img.crop((0,0,left,h)).getbbox() is None
     report['software_modes'].append({'name':name,'sha256':hashlib.sha256((d/'frame.png').read_bytes()).hexdigest()})
  print(lang,'video matrix PASS',flush=True)
  for filtering in ['nearest','linear','sharp']:
   name=f'{lang}-software-{filtering}'
   d,img=run(name,common+['--renderer','software','--frame-filter',filtering,'--size','1000x600'])
   assert img.size==(1000,600)
   report['software_frames'].append({'name':name,'sha256':hashlib.sha256((d/'frame.png').read_bytes()).hexdigest()})
 # Edit independent FXAA/HUD via menu and verify process restart.
 d,_=run('settings-gl',frames=18,script='1 key Down\n2 key Return\n3 key Return\n4 key Down\n5 key Down\n6 key Down\n7 key Down\n8 key Down\n9 key Right\n10 key Down\n11 key Right\n13 state saved\n',settings='[video]\nrenderer=1\n')
 cfg=configparser.ConfigParser();cfg.read(d/'settings.ini');assert cfg['video']['fxaa']=='1' and cfg['video']['classic_hud']=='1'
 (d/'restart.txt').write_text('1 state restarted\n')
 restart=subprocess.run([str(EXE),'--user-dir',str(d),'--frames','3','--ui-script',str(d/'restart.txt')],cwd=ROOT,capture_output=True,text=True,timeout=30)
 assert restart.returncode==0
 assert 'fxaa=1 classic_hud=1' in (d/'port.log').read_text()
 d,_=run('settings-software',frames=18,script='1 key Down\n2 key Return\n3 key Return\n4 key Down\n5 key Down\n6 key Down\n7 key Down\n8 key Right\n9 key Right\n13 state saved\n',settings='[video]\nrenderer=0\n')
 cfg=configparser.ConfigParser();cfg.read(d/'settings.ini');assert cfg['video']['frame_filter']=='2'
 for i in range(0,len(report['fov_cases']),3):assert len({x['sha256'] for x in report['fov_cases'][i:i+3]})==3
 for i in range(0,len(report['software_frames']),3):assert len({x['sha256'] for x in report['software_frames'][i:i+3]})==3
 report['settings_restart']=True
 (OUT/'report.json').write_text(json.dumps(report,indent=2))
 print(f"{len(report['software_modes'])} software modes, {len(report['fov_cases'])} FOV, {len(report['aspect_cases'])} forced aspect, {len(report['gl_frames'])} GL frames, {len(report['software_frames'])} software frames, {len(report['weapons'])} weapon visibility cases, independent settings PASS")
if __name__=='__main__':main()

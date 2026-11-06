"""Verify all three original 101-entry AI angle tables in read-only live DOS RAM."""
import argparse,hashlib,json,os,secrets,shutil,statistics,struct,subprocess,time,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'research/evidence/source-port/sp12-native-complete/angle-ram'
EXE=Path(os.environ['LOCALAPPDATA'])/'Programs/DOSBox-Automation/0.85.1/dosbox-automation-0.85.1/dosbox.exe'
ANCHOR=b'OVERFLOW\0UNDERFLOW\0TLOSS\0PLOSS'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
class Api:
 def __init__(self,port,token):self.url=f'http://127.0.0.1:{port}/api/v1/';self.token=token
 def request(self,path,data=None,method='GET'):
  headers={'Authorization':'Bearer '+self.token}
  if data is not None:headers['Content-Type']='application/json';data=json.dumps(data).encode()
  req=urllib.request.Request(self.url+path,data=data,method=method,headers=headers)
  with urllib.request.urlopen(req,timeout=20)as response:return response.read()
 def key(self,name):
  self.request('input/sequence',{'events':[{'type':'key','key':'KBD_'+name,'pressed':True,'t':0},{'type':'key','key':'KBD_'+name,'pressed':False,'t':100}]},'POST');time.sleep(1.2)
 def mem(self,address,length):return self.request(f'memory/{address}/{length}')
def wait(operation,timeout=30):
 start=time.monotonic()
 while time.monotonic()-start<timeout:
  try:
   result=operation()
   if result:return result
  except OSError:pass
  time.sleep(.1)
 raise TimeoutError('DOS startup')

def trial(profile):
 source=ROOT/({'ru':'originals/pkr/RUS','en':'originals/pkr/ENG','old-gold':'originals/Old_Gold_3D'}[profile]);exe={'ru':'PKR.EXE','en':'PKE.EXE','old-gold':'OG.EXE'}[profile];out=OUT/profile;out.mkdir(parents=True,exist_ok=True);game=out/'game';game.mkdir(exist_ok=True)
 names=[p.name for p in source.iterdir()if p.is_file()];before={n:sha(source/n)for n in names}
 for name in names:shutil.copy2(source/name,game/name)
 port=8176;token=secrets.token_hex(32);api=Api(port,token)
 config=f"""[sdl]
fullscreen = off
output = texture
[dosbox]
machine = svga_s3
memsize = 32
[cpu]
core = normal
cputype = pentium
cpu_cycles = 60000
cpu_cycles_protected = 60000
[mixer]
nosound = true
[webserver]
webserver_enabled = on
webserver_bind_address = 127.0.0.1
webserver_port = {port}
webserver_osd = off
[autoexec]
mount -u c
mount c "{game}"
c:
{exe}
"""
 (out/'dosbox.conf').write_text(config,'ascii');env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',DOSBOX_API_TOKEN=token);proc=None
 try:
  with(out/'stdout.log').open('wb')as stdout,(out/'stderr.log').open('wb')as stderr:
   proc=subprocess.Popen([str(EXE),'--noprimaryconf','--nolocalconf','-conf',str(out/'dosbox.conf'),'-noconsole'],cwd=out,env=env,stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
   info=wait(lambda:json.loads(api.request('dosbox/info')))
   expected=list(map(int,__import__('re').findall(r'-?\d+',(ROOT/'ports/sdl2/src/ai_angles.inc').read_text().split('{{')[1].split('}}')[0])));expected_bytes=struct.pack('<101h',*expected)
   def locate():
    memory=api.mem(0,0x1000000)
    return (memory,memory.find(expected_bytes))if memory.count(expected_bytes)==1 else None
   memory,address=wait(locate);table=list(struct.unpack_from('<101h',memory,address));assert table==expected
   if profile=='old-gold':
    assert memory.count(ANCHOR)==1;data_base=memory.find(ANCHOR)-0x110e;assert address==data_base+0xbf02
   else:
    text=(ROOT/'ports/sdl2/src/trig.inc').read_text();trig=[]
    for section in text.split('= {{')[1:]:trig.extend(map(int,__import__('re').findall(r'-?\d+',section.split('}};')[0])))
    assert len(trig)==722;assert memory[address-0xb48:address]==struct.pack('<722i',*trig);data_base=address-0xb48
   (out/'angles.bin').write_bytes(expected_bytes)
   report=dict(profile=profile,address=address,data_base=data_base,original_data_offset='DS:bf02'if profile=='old-gold'else 'math data selector:0b48 after722PITtriglongs',angles=table,emulator=info,originals_before=before,originals_after={n:sha(source/n)for n in before},copies_after={n:sha(game/n)for n in before},status='PASS',address_system='live linear addresses; original offsets and layouts confirmed by Ghidra listings; no process memory writes')
   assert report['originals_before']==report['originals_after']==report['copies_after'];(out/'report.json').write_text(json.dumps(report,indent=2));print(profile,'native RAM atan101 matches frozen table PASS',flush=True);return report
 finally:
  if proc:proc.terminate();proc.wait(timeout=10)
def main():
 rows=[trial(p)for p in ('ru','en','old-gold')];(OUT/'summary.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':main()

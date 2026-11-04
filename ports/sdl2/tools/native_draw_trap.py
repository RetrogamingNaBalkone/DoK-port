"""Post-draw runtime trap; source EXE and original draw instructions are unchanged.
Only the two epilogue POP bytes are temporarily replaced by JMP $ and restored.
"""
import json,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def hold(api,memory,language,ldt):
 profile='old-gold' if language=='og' else language
 pattern=(ROOT/f'research/evidence/source-port/sp07-render/ghidra/{profile}/draw-tail.bin').read_bytes()
 if memory.count(pattern)!=1:raise RuntimeError('Draw-tail signature is not unique')
 linear=memory.find(pattern)+(7 if language=='og' else 16);original=api.read(linear,2)
 if original not in [bytes.fromhex('5f5e'),bytes.fromhex('5e5d')]:raise RuntimeError('Unexpected original epilogue')
 api.write(linear,bytes.fromhex('ebfe'))
 try:
  deadline=time.monotonic()+60
  while time.monotonic()<deadline:
   cpu=json.loads(api.get('cpu/state'))['registers'];cs=cpu['cs'];ip=cpu['eip']
   if language=='og':base=cs*16
   else:
    d=memory[ldt+(cs&~7):ldt+(cs&~7)+8]
    base=int.from_bytes(d[2:4],'little')+(d[4]<<16)+(d[7]<<24)
   if base+ip==linear:
    return dict(linear=linear,original_hex=original.hex(),runtime_cs=hex(cs),runtime_ip=hex(ip),method='post-draw epilogue POP pair replaced in guest RAM with JMP $; original renderer completed before hold')
   time.sleep(.01)
  raise TimeoutError(f'Original draw did not reach trap at {linear:#x}; last CS:IP={cs:#x}:{ip:#x}, linear={base+ip:#x}, patch={api.read(linear,2).hex()}')
 except BaseException:
  api.write(linear,original);raise

def release(api,trap):
 api.write(trap['linear'],bytes.fromhex(trap['original_hex']))
 if api.read(trap['linear'],2)!=bytes.fromhex(trap['original_hex']):raise RuntimeError('Trap restore failed')
 trap['restored']=True

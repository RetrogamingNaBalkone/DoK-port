"""N22 native AI origin-map overwrite after a respawn overlap.

Replay the retained full PKR/PKE active callback using N17's controlled
animation/heading/motion boundaries, but execute the actual map setter rather
than its old recording hook. Initial MMap has hero at the enemy's origin.
No whole-DOS-session or normal playthrough claim is made.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from audit_ai_combat import Original,models,trace_fields
from unicorn import UC_HOOK_MEM_WRITE
from unicorn import x86_const as R


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class OverlapOriginal(Original):
    def __init__(self,language):
        super().__init__(language)
        self.injected=False;self.writes=[];self.map_entries=0
        self.origin=4*64+5
        self.mmap_base=0x30000+(0x608a if self.ru else 0x613a)
        self.c.hook_add(UC_HOOK_MEM_WRITE,self.map_write)

    def map_write(self,c,access,address,size,value,context):
        if address==self.mmap_base+self.origin:self.writes.append(value&255)

    def hook(self,c,address,size,context):
        cs,ip=c.reg_read(R.UC_X86_REG_CS),c.reg_read(R.UC_X86_REG_IP)
        if cs==self.segment and ip==self.ids['active'] and not self.injected:
            # This is fixture setup, before executing the first instruction.
            # It never runs against an emulated NDS production playthrough.
            c.mem_write(0x50026,struct.pack('<iii',2640,-300,2160))
            c.mem_write(self.mmap_base,b' '*4096)
            c.mem_write(self.mmap_base+self.origin,b'M')
            self.injected=True
        if cs==self.segment and ip==self.ids['map']:
            self.map_entries+=1
            return # Execute the native setter, including unconditional write.
        super().hook(c,address,size,context)


def main():
    fields=trace_fields()
    transcript=ROOT/'research/evidence/nds/n17/callbacks.jsonl'
    rows=[dict(zip(fields,json.loads(line),strict=True)) for line in transcript.read_text().splitlines()]
    model=models();result=dict(task='N22',passed=False,scope=__doc__,languages={},transcript_sha256=sha(transcript))
    for language in (1,2):
        row=next(r for r in rows if r['language']==language and r['health_in']>0 and r['state_in']==1 and r['front_class']==0)
        native=OverlapOriginal(language)
        manifest=json.loads((native.path/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        sample=ROOT/manifest['sample'];before=sha(sample)
        assert before==manifest['sha256_before']==manifest['sha256_after']
        native.callback(row,model[language,row['level'],row['glyph']])
        assert native.injected and native.map_entries==2
        assert native.writes==[32,row['glyph']],native.writes
        assert native.c.mem_read(native.mmap_base+native.origin,1)[0]==row['glyph']
        after=sha(sample);assert after==before
        result['languages'][str(language)]=dict(sample=manifest['sample'],sha256_before=before,sha256_after=after,
            callback_entry=f'{native.segment:04x}:{native.ids["active"]:04x}',map_setter_entry=f'{native.segment:04x}:{native.ids["map"]:04x}',
            mmap_origin_cell=native.origin,initial_glyph=ord('M'),writes=native.writes,
            native_map_setter_calls=native.map_entries,full_callback_expected_state_matched=True,
            controlled=['front/sight results','animation accessors','heading/collision trial','N09 movement call','timer/audio/UI/allocation'],
            executed=['full native AI active callback control flow','native MMap setter with real coordinates/clamp/store','native copy helper'],
            actor_binary_sha256=sha(native.path/'actor.bin'),asm_sha256=sha(native.path/'evidence.asm'))
    result['passed']=True;result['script_sha256']=sha(__file__)
    output=ROOT/'research/evidence/nds/n22/original/retry-overlap.json'
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,writes={k:v['writes'] for k,v in result['languages'].items()})))

if __name__=='__main__':main()

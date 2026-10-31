"""Disassemble actual boot/IPC functions, respecting ARM ELF mapping symbols."""
import argparse,hashlib,json,struct,sys
from pathlib import Path
import capstone

class Elf:
    def __init__(self,path):
        self.path=path;self.data=path.read_bytes();d=self.data
        assert d[:6]==b'\x7fELF\x01\x01'
        at=struct.unpack_from('<I',d,32)[0];size,count=struct.unpack_from('<HH',d,46)
        self.sections=[struct.unpack_from('<10I',d,at+i*size) for i in range(count)]
        self.symbols=[]
        for s in self.sections:
            if s[1]!=2:continue
            strings=self.sections[s[6]];names=d[strings[4]:strings[4]+strings[5]]
            for at in range(s[4],s[4]+s[5],s[9] or 16):
                name,value,n,info,other,index=struct.unpack_from('<IIIBBH',d,at)
                end=names.find(b'\0',name)
                self.symbols.append((names[name:end].decode('ascii','replace'),value,n,info,index))

    def dump(self,name):
        try:s=next(s for s in self.symbols if s[0]==name)
        except StopIteration:
            try:s=next(s for s in self.symbols if s[0].startswith(name+'.') and s[3]&15==2)
            except StopIteration:return '',None
        _,value,n,_,index=s;base=value&~1;section=self.sections[index]
        if not n:
            ends=[v&~1 for _,v,_,info,i in self.symbols if i==index and info&15==2 and (v&~1)>base]
            n=min(ends+[base+512])-base
        end=base+n;maps=sorted((v,k.split('.')[0]) for k,v,_,_,i in self.symbols if i==index and k.split('.')[0] in ('$a','$t','$d'))
        points=sorted({base,end,*[v for v,_ in maps if base<v<end]})
        rows=[f'\n{s[0]}: runtime {value:#010x}, {n} bytes']
        for a,b in zip(points,points[1:]):
            kind=next((k for v,k in reversed(maps) if v<=a),'$t' if value&1 else '$a')
            at=section[4]+a-section[3];raw=self.data[at:at+b-a]
            if kind=='$d':rows.append(f'{a:08x} DATA {raw.hex()}');continue
            mode=capstone.CS_MODE_THUMB if kind=='$t' else capstone.CS_MODE_ARM
            for ins in capstone.Cs(capstone.CS_ARCH_ARM,mode).disasm(raw,a):
                rows.append(f'{ins.address:08x} {ins.bytes.hex():9} {ins.mnemonic:9} {ins.op_str}')
        return '\n'.join(rows),dict(address=hex(value),bytes=n)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();out=target/'boot-code';out.mkdir(exist_ok=True)
    report=dict(functions={},inputs=[])
    for cpu,path,names in [('arm7',target/'arm7/dok_audio_arm7.elf',('main','audio_irq_probe','IntrMain','fifo_buffer_wait_block','fifoInternalSend','inputGetAndSend','__wrap_fifoSendDatamsg')),
                           ('arm9',target/'dok_nds_gameplay.elf',('command','nds_audio_init','__wrap_scanKeys','setTransferInputData'))]:
        elf=Elf(path);rows=[]
        report['inputs'].append(dict(path=str(path),sha256=hashlib.sha256(elf.data).hexdigest()))
        for name in names:
            text,meta=elf.dump(name)
            if meta:rows.append(text);report['functions'][cpu+'/'+name]=meta
        (out/(cpu+'.txt')).write_text('\n'.join(rows)+'\n','utf8')
    report['scope']='Runtime addresses in current custom NDS ELFs; mapping-symbol DATA is not disassembled as instructions'
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))

if __name__=='__main__':main()

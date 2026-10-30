"""Execute original Ghidra-exported PKR/PKE selector x86; UI devices are sinks.

The external DOS keyboard/menu driver returns a chosen selection. Original
resource-name mutation, switch, far-pointer writes and returned flags execute.
This is not an NDS frontend implementation comparison.
"""
from pathlib import Path
import json
import struct
from unittest.mock import patch
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R
from extract_frontend import ROOT,EVIDENCE,EXE,sha,pin,write,native_constants

class OriginalSelector:
    def __init__(self,lang,selection):
        self.lang,self.selection = lang,selection
        self.ru = lang==1
        _,self.source_before=native_constants(lang)
        folder=EVIDENCE/'original'/EXE[lang][0]
        self.c=Uc(UC_ARCH_X86,UC_MODE_16)
        self.c.mem_map(0,0x100000)
        self.c.mem_write(0x10080,(folder/'1008.bin').read_bytes())
        self.c.mem_write(0x30000,(folder/f'{EXE[lang][4]}.bin').read_bytes())
        self.c.mem_write(0x40000,struct.pack('<H',0x800))
        self.c.mem_write(0x30830,struct.pack('<HH',0x100,0x6000))
        self.c.mem_write(0x30000+(0xb48c if self.ru else 0xb53c),struct.pack('<HH',0,0x4000))
        self.c.mem_write(0x40022,struct.pack('<HH',8,8))
        self.c.mem_write(0x40030,struct.pack('<II',320,200))
        self.labels=[];self.calls=[]
        self.c.hook_add(UC_HOOK_CODE,self.hook)
    def hook(self,c,address,size,user):
        cs=c.reg_read(R.UC_X86_REG_CS);ip=c.reg_read(R.UC_X86_REG_IP)
        if cs==0x7000:
            c.emu_stop();return
        if cs==0x1008:return
        sp=c.reg_read(R.UC_X86_REG_SP)
        args=bytes(c.mem_read(0x30000+sp+4,20))
        self.calls.append(f'{cs:04x}:{ip:04x}')
        if (cs,ip)==((0x1078,0x9b5) if self.ru else (0x1028,0xdb3)):
            c.reg_write(R.UC_X86_REG_AX,self.selection)
        elif (cs,ip)==((0x1078,0xb4) if self.ru else (0x1028,0x4a0)):
            # Leading color/style is a DWORD, then x/y WORD and far text.
            off,seg=struct.unpack_from('<HH',args,8)
            raw=bytes(c.mem_read(seg*16+off,40)).split(b'\0',1)[0]
            self.labels.append(raw.decode('cp866'))
            c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0x4000)
        elif (cs,ip)==(0x1038,0x8b0):
            c.reg_write(R.UC_X86_REG_AX,1);c.reg_write(R.UC_X86_REG_DX,0)
        # Return external cdecl16far call without interpreting device internals.
        retip,retcs=struct.unpack('<HH',bytes(c.mem_read(0x30000+sp,4)))
        c.reg_write(R.UC_X86_REG_SP,sp+4)
        c.reg_write(R.UC_X86_REG_CS,retcs);c.reg_write(R.UC_X86_REG_IP,retip)
    def run(self):
        c=self.c
        for reg,value in ((R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_ES,0x4000),
                          (R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xe000),(R.UC_X86_REG_CS,0x1008)):
            c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<HH',0,0x7000))
        start=0x2f7 if self.ru else 0x44b
        c.emu_start(0x10080+start,0xfffff,count=10000)
        if c.reg_read(R.UC_X86_REG_CS)!=0x7000:raise AssertionError('Native selector did not return')
        pointers=0x1f82 if self.ru else 0x2032
        names=[]
        for i in range(6):
            off,seg=struct.unpack('<HH',bytes(c.mem_read(0x30000+pointers+i*4,4)))
            names.append(bytes(c.mem_read(seg*16+off,16)).split(b'\0',1)[0].decode('ascii'))
        ax,dx=c.reg_read(R.UC_X86_REG_AX),c.reg_read(R.UC_X86_REG_DX)
        if self.selection in (0,4):assert ax==0x4000 and dx==0,(self.selection,ax,dx)
        elif 1<=self.selection<=3:
            expected=[f'LEVEL{self.selection}',f'BINTRO{self.selection}',f'EINTRO{self.selection}',f'BSPR{self.selection}',f'ESPR{self.selection}',f'GAMEP{self.selection}']
            assert names==expected,(names,expected)
            assert (ax,dx)==(0,0x20),(ax,dx)
        else:assert (ax,dx)==(0,0)
        assert c.reg_read(R.UC_X86_REG_SP)==0xf004
        expected_labels = (['ХОРОМЫ НЕЧИСТИ','ПОДЗЕМНАЯ РЕКА','ПОДВАЛЫ КРЕМЛЯ','В Ы Х О Д'] if self.ru else
                           ['PALACE OF EVIL','SACRED RIVER','UNDERGROUND KREMLIN','E X I T'])
        assert self.labels==expected_labels,(self.labels,expected_labels)
        return dict(language=self.lang,selection=self.selection,resource_names=names,return_ax=ax,return_dx=dx,
                    labels=self.labels,external_calls=self.calls,far_return_sp=0xf004)

class OriginalTitle(OriginalSelector):
    def __init__(self,lang):
        super().__init__(lang,0)
        for slot in (0,0x30,0x3c):
            self.c.mem_write(0x30800+slot,struct.pack('<HH',0x100,0x6000))
        self.reached_readme=False
    def hook(self,c,address,size,user):
        cs,ip=c.reg_read(R.UC_X86_REG_CS),c.reg_read(R.UC_X86_REG_IP)
        if cs==0x1008 and ip==(0xb3 if self.ru else 0xd7):
            self.reached_readme=True;c.emu_stop();return
        if (cs,ip) in (((0x1060,0),(0x1060,0x145)) if self.ru else ((0x1018,0x1770),(0x1018,0x18d9))):
            c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0x4000)
        return super().hook(c,address,size,user)
    def run(self):
        c=self.c
        for reg,value in ((R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_ES,0x4000),
                          (R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xe000),(R.UC_X86_REG_CS,0x1008)):
            c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<HH',0,0x7000))
        c.emu_start(0x10080+(0x1ba if self.ru else 0x23e),0xfffff,count=10000)
        once='1108:01f7' if self.ru else '1060:173d'
        stop='1108:026d' if self.ru else '1060:17b3'
        loop='1108:0232' if self.ru else '1060:1778'
        assert self.reached_readme and self.calls.count(once)==self.calls.count(stop)==1 and loop not in self.calls
        assert self.calls.index(once)<self.calls.index(stop)
        return dict(language=self.lang,once_calls=1,stop_calls=1,loop_calls=0,entered_readme_after_title=True,external_calls=self.calls)

class OriginalReadme(OriginalTitle):
    def hook(self,c,address,size,user):
        cs,ip=c.reg_read(R.UC_X86_REG_CS),c.reg_read(R.UC_X86_REG_IP)
        if (cs,ip)==((0x1060,0x145) if self.ru else (0x1018,0x18d9)):
            c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0x4000)
        return OriginalSelector.hook(self,c,address,size,user)
    def run(self):
        c=self.c
        for reg,value in ((R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_ES,0x4000),
                          (R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xe000),(R.UC_X86_REG_CS,0x1008)):
            c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<HH',0,0x7000))
        c.emu_start(0x10080+(0xb3 if self.ru else 0xd7),0xfffff,count=10000)
        once='1108:01f7' if self.ru else '1060:173d'
        stop='1108:026d' if self.ru else '1060:17b3'
        loop='1108:0232' if self.ru else '1060:1778'
        assert c.reg_read(R.UC_X86_REG_CS)==0x7000
        assert self.calls.count(once)==self.calls.count(stop)==1 and loop not in self.calls
        assert self.calls.index(once)<self.calls.index(stop)
        assert c.reg_read(R.UC_X86_REG_SP)==0xf004
        return dict(language=self.lang,once_calls=1,stop_calls=1,loop_calls=0,far_return_sp=0xf004,external_calls=self.calls)

def main():
    cases=[];sources=[]
    for lang in (1,2):
        _,source=native_constants(lang);sources.append(source)
        for selection in (0,1,2,3,4,5,65535):cases.append(OriginalSelector(lang,selection).run())
    titles=[OriginalTitle(lang).run() for lang in (1,2)]
    readmes=[OriginalReadme(lang).run() for lang in (1,2)]
    after=[native_constants(lang)[1] for lang in (1,2)]
    if after != sources:raise ValueError('Executed original exports, samples or manifests changed during fixture execution')
    rejected=[]
    original_read=Path.read_bytes
    for lang in (1,2):
        folder=EVIDENCE/'original'/EXE[lang][0]
        for name in ('1008.bin',f'{EXE[lang][4]}.bin','evidence.asm'):
            target=folder/name
            def changed_read(path, *,target=target):
                data=original_read(path)
                if path==target:data=bytes([data[0]^1])+data[1:]
                return data
            with patch.object(Path,'read_bytes',changed_read):
                try:native_constants(lang)
                except ValueError as error:
                    if str(error)!='Changed Ghidra export':raise
                    rejected.append(dict(language=lang,mutated_export=name,rejected=True,physical_file_unchanged=True))
                else:raise AssertionError('Mutated executed export accepted')
    result=dict(task='N21',passed=True,script=pin(Path(__file__)),cases=cases,count=len(cases),titles=titles,readmes=readmes,originals=sources,
                originals_after=after,executed_source_pins_unchanged=True,rejected_source_mutations=rejected,
                scope='Unchanged original selector switch/resource name mutation and far-pointer writes; external graphics/menu/keyboard/runtime sinks',
                limitations=['DOS keyboard/menu driver is an external fixture returning a chosen selection; NDS controls separately tested',
                             'No new DOS process capture or physical DS claim'])
    write(EVIDENCE/'original-selector.json',(json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
    print(f'N21 original selector x86: {len(cases)} cases passed')

if __name__=='__main__':main()

"""SP05 boundary/random arithmetic: original Ghidra instruction slices vs C++."""
import hashlib,json,random,struct,subprocess
from pathlib import Path
from unicorn import Uc,UcError,UC_ARCH_X86,UC_MODE_16,UC_ERR_EXCEPTION
from unicorn import x86_const as R
ROOT=Path(__file__).resolve().parents[3]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    folder=ROOT/'research/evidence/source-port/sp05-scheduler/old-gold'
    manifest=json.loads((folder/'manifest.json').read_text());sample=ROOT/manifest['sample'];before=sha(sample)
    assert before==manifest['sha256_before']
    rng=random.Random(0x53503035);boundary=[0,1,2,31,32,63,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff]
    pairs=[(a,b) for a in boundary for b in boundary]+[(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(1000)]
    vectors=[(op,a,b) for op in ('neg','add','sub','mul','sar','div') for a,b in pairs]
    run=subprocess.run([str(ROOT/'build/sdl2/windows/dok_arithmetic_probe.exe')],input=''.join(f'{op} {a} {b}\n' for op,a,b in vectors),capture_output=True,text=True,check=True)
    expected=run.stdout.splitlines();assert len(expected)==len(vectors);codes={}
    for op in ('neg','add','sub','mul','sar','div'):
        path=folder/(op+'.bin');assert sha(path)==manifest['bytes_sha256'][op];codes[op]=path.read_bytes()
    results={};faults=0
    # One machine per operation prevents stale translated code between slices.
    machines={}
    for op,code in codes.items():
        c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000);c.mem_write(0x10000,code);machines[op]=c
    for (op,a,b),wanted in zip(vectors,expected):
        if op=='div':
            c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000);c.mem_write(0x10000,codes[op]);machines[op]=c
        c=machines[op];c.reg_write(R.UC_X86_REG_CS,0x1000);c.reg_write(R.UC_X86_REG_SS,0x3000)
        c.reg_write(R.UC_X86_REG_BP,0x100);c.mem_write(0x300fc,struct.pack('<I',b))
        for reg,v in ((R.UC_X86_REG_EAX,a),(R.UC_X86_REG_EDX,b),(R.UC_X86_REG_EBX,b),(R.UC_X86_REG_ECX,b),(R.UC_X86_REG_EFLAGS,2)):c.reg_write(reg,v)
        try:
            c.emu_start(0x10000,0x10000+len(codes[op]),count=3)
            value=c.reg_read(R.UC_X86_REG_EAX)
            if op=='neg':value=value&65535;value=value if value<32768 else (value-65536)&0xffffffff
            actual=str(value)
        except UcError as e:
            if e.errno!=UC_ERR_EXCEPTION or op!='div':raise
            actual='fault';faults+=1
        if actual!=wanted:raise AssertionError((op,a,b,actual,wanted))
        results[op]=results.get(op,0)+1
    assert sha(sample)==before
    out=ROOT/'build/sdl2/checks/arithmetic-oracle.json';out.write_text(json.dumps(dict(passed=True,vectors=results,idiv_faults=faults,sample_sha256=before,scope='Shared arithmetic primitives; original OG instruction slices, not complete gameplay functions'),indent=2)+'\n')
    print(sum(results.values()),'original x86/C++ arithmetic vectors;',faults,'IDIV faults matched')
if __name__=='__main__':main()

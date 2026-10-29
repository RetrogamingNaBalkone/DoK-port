"""Match naturally observed DOS phases to actual ARM9 transformed image bytes."""
import hashlib,json,struct
from pathlib import Path
from audit_effects import Original,resources,ROOT,OUT
def main():
    r=json.loads((ROOT/'build/nds/n13/phase-pair/emulator/result.json').read_text('utf8'));assert r['passed']
    source=resources('ru');native=Original('ru');report=dict(passed=False,levels=[])
    for case in r['cases']:
        level=case['level'];directory=OUT/f'dos/ru/level{level}-phase-pair'
        phase=json.loads((directory/'phase-pair.water-phase.json').read_text('utf8'))
        assert phase['passed'] and phase['two_frame_and_phase_observations_identical']
        pixels=directory/'phase-pair.idx';dac=directory/'phase-pair.vga6'
        pose=phase['pose'];sin_cos=phase['sin_cos'];headers=phase['headers']
        rows=[row for row in r['history_records'] if row['case_id']==case['id'] and row['flags']&8];assert len(rows)==2
        row=rows[-1];signed=lambda v:(v+2**31)%2**32-2**31
        assert [signed(row[k]) for k in ('camera_x','camera_y','camera_z')]==pose
        assert [signed(row[k]) for k in ('sin_q15','cos_q15')]==sin_cos
        assert dac.read_bytes()==bytes(v*99//100 for v in source[20+level*2])
        paired=[]
        for demand in case['demand']:
            code=(demand['key']>>43)&15
            if not code:continue
            flag={2:0x408,4:0x808,6:0x1008,8:0x2008}[code];header=next(h for h in headers if h['flags']==flag)
            phase_index=((demand['key']>>33)&31)-1;assert phase_index==header['phase']
            raw=source[(demand['id']>>18)&4095];w,h=struct.unpack_from('<2H',raw);sw=1<<max(3,(w-1).bit_length());sh=1<<max(3,(h-1).bit_length())
            actual=native.crop(w,h,flag,header['divisor'],phase_index,raw[18:])
            padded=b''.join(actual[y*w:(y+1)*w]+bytes(sw-w) for y in range(h))+bytes(sw*(sh-h))
            assert demand['pixels_sha256']==hashlib.sha256(padded).hexdigest()
            paired.append(dict(resource=(demand['id']>>18)&4095,flags=flag,phase=phase_index,pixels=w*h,gpu_bytes=len(padded),sha256=demand['pixels_sha256']))
        if level==2:assert paired
        report['levels'].append(dict(level=level,pose=pose,sin_cos=sin_cos,palette_percentage=99,
            native_frame_sha256=hashlib.sha256(pixels.read_bytes()).hexdigest(),native_phase_headers=headers,
            matched_shifted_images=paired,scope='Native popup frame and matching DS pose/phases; menu overlay/HUD differ. The fresh clean PLAY frame is a separate N06-style visual reference.'))
    report['passed']=True;(OUT/'phase-pair-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print('N13 DOS/DS pose, VGA6 and phase tuples: actual native/ARM9 image bytes PASS')
if __name__=='__main__':main()

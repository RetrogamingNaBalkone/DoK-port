"""Original Old Gold HUD, monster occupancy and actual wide GL rotation regression."""
from pathlib import Path
import hashlib,json,os,re,subprocess
from check_video import Image
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/og-graphics'
RES=ROOT/'originals/Old_Gold_3D/OLD_GOLD.RES'
EXE=ROOT/'build/sdl2/windows/dok_port.exe'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    samples=[RES,ROOT/'originals/Old_Gold_3D/OG.EXE']
    # Inventory owns the actual EXE path; do not assume a renamed sample.
    profile=next(p for p in json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles'] if p['id']=='old-gold')
    samples[1]=ROOT/profile['sample'];before={str(p.relative_to(ROOT)):sha(p) for p in samples}
    env=dict(os.environ,SDL_AUDIODRIVER='dummy')
    focused=subprocess.run([str(ROOT/'build/sdl2/windows/dok_old_gold_graphics_probe.exe'),str(RES),str(OUT/'hud.png')],cwd=ROOT,capture_output=True,text=True,check=True)
    (OUT/'focused.log').write_text(focused.stdout,encoding='utf-8')
    gl=subprocess.run([str(ROOT/'build/sdl2/windows/dok_gl_probe.exe')],cwd=ROOT,capture_output=True,text=True,check=True)
    (OUT/'gl.log').write_text(gl.stdout,encoding='utf-8')
    results=[]
    # Freeze the diagnostic frame clock while allowing the ordinary keyboard path.
    for hud in ('classic','desktop'):
        out=OUT/hud;out.mkdir(exist_ok=True)
        replay=out/'clock.txt';replay.write_text('DOK_INPUT_REPLAY 1 old_gold LEVEL4 1\n416 0 0 0 0 0 0 0 0 -1 0 0\n')
        lines=['0 keydown Right']
        for f in range(0,181,15):lines.extend([f'{f} state angle{f}',f'{f} shot {out/f"turn-{f}.png"}'])
        lines.extend(['181 keyup Right','182 key Tab',f'183 shot {out/"map.png"}'])
        script=out/'script.txt';script.write_text('\n'.join(lines)+'\n')
        command=[str(EXE),'--res',str(RES),'--level','LEVEL4','--skip-intro','--hidden','--renderer','gl','--hud',hud,'--size','960x540','--user-dir',str(out/'user'),'--ui-script',str(script),'--input-replay',str(replay),'--replay-live-input','--frames','185']
        subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,check=True,timeout=60)
        log=(out/'user/port.log').read_text(encoding='utf-8')
        headings=[int(m[1]) for m in re.finditer(r'^STATE angle\d+ .*?pose=[^,]+,[^,]+,[^,]+,(\d+)',log,re.M)]
        assert len(set(headings))>=10,headings
        assert 'OpenGL fallback:' not in log and 'Clean shutdown;' in log
        for f in range(0,181,15):
            path=out/f'turn-{f}.png';frame=Image.open(path);assert frame.size==(960,540)
        frame=Image.open(out/'map.png')
        black=sum(frame.data[i:i+3]==b'\0\0\0' for i in range(0,len(frame.data),3))
        assert black>0,'White player marker remains invisible on white map'
        results.append(dict(hud=hud,headings=headings,frames=13,map_black_pixels=black))
    after={str(p.relative_to(ROOT)):sha(p) for p in samples};assert before==after
    report=dict(exe_sha256=sha(EXE),original_sha256_before=before,original_sha256_after=after,focused=focused.stdout.strip(),gl=gl.stdout.strip(),rotations=results,limitations='Hidden Windows GL; synthetic billboard wall span and original monster center occupancy. No claim of full DOS/AI parity or full map playthrough.')
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(focused.stdout.strip());print('PASS 26 actual GL turn frames, two HUD modes; visible map marker; original hashes unchanged')
if __name__=='__main__':main()

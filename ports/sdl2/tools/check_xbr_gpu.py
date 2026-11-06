"""Compare actual software-world GPU xBR with its CPU fallback at fixed replay times."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_video import Image
from compiler_cache import ROOT
from experiment_cleanup import temporary_workspace

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,default=ROOT/'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--out',type=Path,default=ROOT/'research/evidence/source-port/sp-xbr-gpu-process')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True);checks=[]
    samples=[('old_gold','originals/Old_Gold_3D/OLD_GOLD.RES',4),('kremlin_pkr','originals/pkr/RUS/PKR.RES',1),('kremlin_pke','originals/pkr/ENG/PKE.RES',1)]
    originals={r:hashlib.sha256((ROOT/r).read_bytes()).hexdigest() for _,r,_ in samples}
    with temporary_workspace(args.out.resolve(),'run-') as work:
        for dataset,res,level in samples:
            replay=work/(dataset+'.replay');replay.write_text(f'DOK_INPUT_REPLAY 1 {dataset} LEVEL{level} 1\n'+''.join(f'{416*t} 0 0 0 0 0 0 0 0 -1 0 0\n' for t in range(1,129)),encoding='utf-8')
            def run(name,mode,size,env_extra=None,events='3 state applied\n'):
                profile=work/name;profile.mkdir();(profile/'settings.ini').write_text('[settings]\nversion=1\n[interface]\nfps=0\n',encoding='utf-8')
                script=work/(name+'.txt');script.write_text(events,encoding='utf-8');shot=work/(name+'.png')
                command=[str(args.app.resolve()),'--hidden','--verbose', '--user-dir',str(profile),'--res',str(ROOT/res),'--level','LEVEL'+str(level),'--skip-intro',
                         '--renderer','software','--software-mode',mode,'--size',size,'--frame-filter','xbr','--hud','classic','--input-replay',str(replay),
                         '--ui-script',str(script),'--screenshot',str(shot),'--frames','4']
                env=dict(os.environ,SDL_AUDIODRIVER='dummy');env.update(env_extra or {})
                proc=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,env=env,timeout=90)
                log=(profile/'port.log').read_text(encoding='utf-8');assert proc.returncode==0,(name,proc.stderr,log)
                assert 'renderer=0 ' in log,(name,'World unexpectedly changed to GL',log)
                checks.append(dict(name=name,command=command,environment=env_extra or {},states=[s for s in log.splitlines() if s.startswith('STATE ')]))
                return Image.open(shot),log,shot
            for mode,size in [('320x200','853x641'),('640x480','1280x960'),('1024x768','1920x1080')]:
                name=dataset+'-'+mode;gpu,glog,gpath=run(name+'-gpu',mode,size)
                cpu,clog,cpath=run(name+'-cpu',mode,size,{'DOK_DISABLE_GPU_XBR':'1'})
                assert 'gpu_presentation=1 gpu_xbr=1' in glog,(name,glog)
                assert 'gpu_presentation=1 gpu_xbr=0' in clog,(name,clog)
                maximum=max(abs(a-b) for a,b in zip(gpu.data,cpu.data));assert maximum<=1,(name,maximum)
                checks[-1]['gpu_cpu_max_channel_delta']=maximum
                if dataset=='kremlin_pkr' and mode=='640x480':
                    (args.out/'pkr-cpu.png').write_bytes(cpath.read_bytes());(args.out/'pkr-gpu.png').write_bytes(gpath.read_bytes())
            _,log,_=run(dataset+'-native','640x480','640x480');assert 'gpu_xbr=0' in log
            _,log,_=run(dataset+'-sdl-fallback','320x200','853x641',{'DOK_SOFTWARE_SDL_PRESENTATION':'1'});assert 'gpu_presentation=0 gpu_xbr=0' in log
            if dataset=='kremlin_pkr':
                _,log,_=run(dataset+'-no-gl-driver','320x200','853x641',{'SDL_VIDEODRIVER':'dummy'});assert 'gpu_presentation=0 gpu_xbr=0' in log
            print(dataset,'GPU/CPU pixels, software world, native bypass, SDL fallback PASS',flush=True)
        assert originals=={r:hashlib.sha256((ROOT/r).read_bytes()).hexdigest() for _,r,_ in samples}
    (args.out/'report.json').write_text(json.dumps(dict(status='PASS',app_sha256=hashlib.sha256(args.app.read_bytes()).hexdigest(),checks=checks,originals_sha256=originals),indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()

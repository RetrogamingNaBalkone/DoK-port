"""Frozen real-process comparison, legacy filter aliases, and the two-choice F4 cycle."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_video import Image
from compiler_cache import ROOT
from experiment_cleanup import temporary_workspace

def delta(a,b):return sum(abs(x-y) for x,y in zip(a,b))/len(a)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,default=ROOT/'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--out',type=Path,default=ROOT/'research/evidence/source-port/sp-filters-ux')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True);rows=[]
    samples=[('old_gold','originals/Old_Gold_3D/OLD_GOLD.RES',4),('kremlin_pkr','originals/pkr/RUS/PKR.RES',1),('kremlin_pke','originals/pkr/ENG/PKE.RES',1)]
    originals={res:hashlib.sha256((ROOT/res).read_bytes()).hexdigest() for _,res,_ in samples}
    with temporary_workspace(args.out.resolve(),'run-') as work:
        for dataset,res,level in samples:
            replay=work/(dataset+'.replay')
            replay.write_text(f'DOK_INPUT_REPLAY 1 {dataset} LEVEL{level} 1\n'+''.join(f'{416*t} 0 0 0 0 0 0 0 0 -1 0 0\n' for t in range(1,129)),encoding='utf-8')
            turning=work/(dataset+'-turn.replay')
            turning.write_text(f'DOK_INPUT_REPLAY 1 {dataset} LEVEL{level} 1\n'+''.join(f'{416*t} {min(80,t*5)} 0 0 0 0 0 0 0 -1 0 0\n' for t in range(1,129)),encoding='utf-8')
            def run(backend,filter,events='',frames=4,turn=False):
                name=dataset+'-'+backend+'-'+filter+('-cycle' if events else '')+('-turn' if turn else '')
                profile=work/name;profile.mkdir(exist_ok=True)
                (profile/'settings.ini').write_text('[settings]\nversion=1\n[interface]\nfps=0\n',encoding='utf-8')
                script=work/(name+'.txt');script.write_text(events,encoding='utf-8');shot=work/(name+'.png')
                command=[str(args.app.resolve()),'--hidden','--res',str(ROOT/res),'--user-dir',str(profile),'--renderer',backend,
                         '--filter' if backend=='gl' else '--frame-filter',filter,'--size','853x641','--software-mode','320x200',
                         '--hud','classic','--level','LEVEL'+str(level),'--skip-intro','--frames',str(frames),
                         '--input-replay',str(turning if turn else replay),'--ui-script',str(script),'--screenshot',str(shot)]
                result=subprocess.run(command,cwd=ROOT,capture_output=True,env=dict(os.environ,SDL_AUDIODRIVER='dummy'),timeout=60)
                assert result.returncode==0,(name,result.stderr)
                frame=Image.open(shot);rows.append(dict(name=name,command=command,sha256=hashlib.sha256(shot.read_bytes()).hexdigest()))
                return frame,profile,shot
            nearest,_,_=run('gl','nearest');smooth,_,_=run('gl','smooth')
            assert nearest.data!=smooth.data,'Visible nearest/Smooth choices became identical'
            for alias in ('linear','mipmaps','trilinear','aniso2','aniso4','aniso8'):
                frame,_,_=run('gl',alias);assert frame.data==smooth.data,(dataset,alias,'Legacy alias differs from Smooth')
            _,profile,_=run('gl','nearest','1 key F4\n',frames=4)
            config=configparser.ConfigParser();config.read(profile/'settings.ini');assert config['video']['mip_filter']=='5'
            _,profile,_=run('gl','nearest','1 key F4\n2 key F4\n',frames=4)
            config.read(profile/'settings.ini');assert config['video']['mip_filter']=='0' and config['video']['linear']=='0'
            software={}
            for filter in ('nearest','linear','sharp','xbr'):
                frame,_,shot=run('software',filter)
                # Scene only: avoid letterbox bars and the independent HUD.
                software[filter]=frame.crop((0,0,853,510 if level==4 else 480)).data
                if dataset=='kremlin_pkr':(args.out/('software-'+filter+'.png')).write_bytes(shot.read_bytes())
            pairs={a+'/'+b:delta(software[a],software[b]) for a,b in [('nearest','linear'),('linear','sharp'),('sharp','xbr')]}
            assert all(value>1 for value in pairs.values()),(dataset,pairs)
            for backend,filters in [('gl',('nearest','smooth')),('software',('nearest','linear','sharp','xbr'))]:
                for filter in filters:run(backend,filter,frames=24,turn=True)
            rows.append(dict(dataset=dataset,software_mean_channel_deltas=pairs))
            print(dataset,'aliases/F4 and distinct software output PASS',pairs,flush=True)
        assert originals=={res:hashlib.sha256((ROOT/res).read_bytes()).hexdigest() for _,res,_ in samples}
    (args.out/'choices.json').write_text(json.dumps(dict(status='PASS',app_sha256=hashlib.sha256(args.app.read_bytes()).hexdigest(),checks=rows,originals_sha256=originals),indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()

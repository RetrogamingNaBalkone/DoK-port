"""N22-only fresh regression of the 28KiB EVENTS -> ASSETS rebalance.

Reuse N03/N08 C tests and pure observation verifiers. Build/output manifests
are new N22 evidence; historical accepted N03/N08 gates are never weakened or
rewritten. Emulator transport loops below are ported from their pinned runners.
"""
import argparse
from fractions import Fraction
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import traceback
from types import FunctionType

from check_host import ROOT, pin, compiler_environment
sys.path.insert(0, str(ROOT/'ports/nds/tools'))
import run_memory_emulator as memory
import run_time_emulator as time_probe
import run_diag_emulator as transport
from run_frontend_emulator import CORE

OUT = ROOT/'build/nds/n22/rebalance'
EVIDENCE = ROOT/'research/evidence/nds/n22/rebalance'
EXPECTED = (393216, 36864, 131072, 262144, 1077248, 262144, 131072)


def artifact(path):
    path = Path(path).resolve()
    return dict(path=path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
                bytes=path.stat().st_size, sha256=transport.sha(path))


def preserved():
    return [artifact(path) for base in ('build/nds/n03', 'build/nds/n08',
            'research/evidence/nds/n03', 'research/evidence/nds/n08')
            for path in sorted((ROOT/base).rglob('*')) if path.is_file()]


def assert_budget():
    source = (ROOT/'ports/nds/include/dok/memory_budget.h').read_text('utf8')
    names = ('WORLD', 'EVENTS', 'SCRATCH', 'SCENE', 'ASSETS', 'AUDIO', 'IO')
    actual = tuple(int(re.search(r'#define DOK_NDS_'+name+r'_BYTES UINT32_C\((\d+)\)', source)[1]) for name in names)
    assert actual == EXPECTED and sum(actual) == 2293760
    assert EXPECTED[1] == 36864 and EXPECTED[4]-1048576 == 65536-EXPECTED[1] == 28672
    return dict(capacities=list(actual), total=sum(actual), reserve=655360,
                sdk_heap_min=196608, moved_bytes=28672)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, MSYSTEM='UCRT64', CHERE_INVOKING='1', MSYS2_PATH_TYPE='minimal')
    repo = '/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    script = "set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\n"
    script += 'make -B -C ports/nds -f Makefile.n03 OUTDIR=../../build/nds/n22/rebalance/memory\n'
    script += 'make -B -C ports/nds -f Makefile.n08 OUTDIR=../../build/nds/n22/rebalance/time\n'
    result = subprocess.run([str(Path.home()/'.local/msys64/usr/bin/bash.exe'), '--login', '-s'],
        input=script, cwd=ROOT, env=env, capture_output=True, text=True, encoding='utf8')
    (OUT/'build.log').write_text(result.stdout+result.stderr, 'utf8')
    assert not result.returncode, result.stdout+result.stderr
    lock = json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf-8-sig'))
    assert all(key+' '+value in result.stdout for key, value in lock['packages'].items())
    arm7 = Path.home()/'.local/msys64/opt/wonderful/thirdparty/blocksds/core/sys/arm7/main_core/arm7_maxmod.elf'
    return dict(script=script, packages=lock['packages'], arm7=artifact(arm7),
        artifacts=[artifact(OUT/f'{folder}/dok_nds_{name}.{suffix}')
            for folder, name in (('memory', 'memory'), ('time', 'time')) for suffix in ('nds', 'elf', 'map')],
        log=artifact(OUT/'build.log'))


def host():
    zig = Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    recipes = {
        'memory': ['ports/nds/core/memory.c', 'ports/nds/tests/test_memory.c'],
        'contracts': ['ports/nds/core/contracts.c', 'ports/nds/tests/test_contracts.c'],
        'time': ['ports/nds/core/contracts.c', 'ports/nds/core/memory.c', 'ports/nds/core/time.c',
                 'ports/nds/core/events.c', 'ports/nds/tests/time_cases.c', 'ports/nds/tests/test_time.c']}
    result = {}
    for name, sources in recipes.items():
        binary = OUT/(name+'-host.exe')
        command = [str(zig), 'cc', '-std=c11', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
                   '-O2', '-Iports/nds/include', '-Iports/nds/tests', *sources, '-o', str(binary)]
        compiled = subprocess.run(command, cwd=ROOT, env=compiler_environment(ROOT), capture_output=True, text=True)
        assert not compiled.returncode, compiled.stdout+compiled.stderr
        run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
        assert not run.returncode, run.stdout+run.stderr
        result[name] = dict(command=command, binary=artifact(binary), stdout=run.stdout,
                            inputs=[artifact(ROOT/source) for source in sources])
        if name == 'time':
            shared = json.loads(run.stdout)
            assert shared['bank_bytes'] == EXPECTED[1] and shared['failures'] == 0
            assert shared['checks'] >= 1000 and shared['clock_cases'] >= 3 and shared['event_cases'] >= 5
            result[name]['shared_suite'] = shared
    # Borrow a compiler recipe, never old acceptance or executables. Inputs
    # are freshly compiled and pinned under this N22 report, including the
    # intended budget/assert changes. Historical host evidence stays intact.
    recipe=json.loads((ROOT/'research/evidence/nds/n22/host/check.json').read_text('utf8'))
    for name,test in (('flow','test_flow_host.c'),('retry-overlap','test_retry_overlap.c')):
        command=recipe['command'][:]
        command[command.index('ports/nds/tools/flow/test_flow_host.c')]='ports/nds/tools/flow/'+test
        binary=OUT/(name+'-host.exe');command[-1]=str(binary)
        compiled=subprocess.run(command,cwd=ROOT,env=compiler_environment(ROOT),capture_output=True,text=True)
        assert not compiled.returncode,compiled.stdout+compiled.stderr
        invocation=[str(binary)]
        if name=='retry-overlap':invocation.append(str(ROOT/'build/nds/assets/dok-assets.ndspak'))
        run=subprocess.run(invocation,cwd=ROOT,capture_output=True,text=True)
        assert not run.returncode,run.stdout+run.stderr
        observed=json.loads(run.stdout)
        if name=='retry-overlap':
            assert observed['shared_actors']==3 and observed['corpse_variants']==2 and observed['repeated_retry_count']==2
            assert not any(observed[key] for key in ('callback_status','game_fault','ai_fault'))
        else:assert observed.get('checks',0)>=655000
        source_paths=[ROOT/value for value in command if str(value).endswith('.c')]
        result[name]=dict(command=command,binary=artifact(binary),observed=observed,
                         inputs=[artifact(path) for path in source_paths])
    return result


def tick(frontend):
    if frontend.frame_time and frontend.frame_time.callback:
        frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
    frontend.core.retro_run();frontend.frames += 1
    assert not frontend.callback_error and not frontend.shutdown, frontend.callback_error


def memory_emulator(core):
    directory = OUT/'memory/emulator';directory.mkdir(parents=True, exist_ok=True)
    elf = OUT/'memory/dok_nds_memory.elf';rom = elf.with_suffix('.nds')
    names = memory.abi_fields();address,size = transport.elf_symbol(elf, 'g_nds_memory_probe')
    assert len(names)*4 == size
    frontend = transport.Frontend(core, directory, 'missing-media')
    report = dict(passed=False, observations=[])
    try:
        frontend.load(rom)
        for _ in range(600):
            tick(frontend)
            state = memory.read_observation(frontend, address, names)
            if state and state['magic'] == 0x334d4b44 and state['status']:
                assert state['abi_version'] == 1 and state['size_bytes'] == size
                report['observations'].append(state)
                if len(report['observations']) == 120:break
        assert len(report['observations']) == 120
        first,last = report['observations'][0],report['observations'][-1]
        assert last['frame'] > first['frame'] and last['sequence'] > first['sequence']
        assert all(s['status'] == 1 and not s['error_bits'] for s in report['observations'])
        # Same pure verifier, exact new literal capacities; do not mutate its
        # module globals or call its historical accepted-output entry point.
        assert memory.CAPACITIES == (393216,65536,131072,262144,1048576,262144,131072)
        verify = FunctionType(memory.verify.__code__, dict(memory.verify.__globals__, CAPACITIES=EXPECTED),
                              name='verify_n22_exact_rebalanced_capacities')
        verify(frontend,last,elf)
        assert all(check['passed'] for check in frontend.checks), frontend.checks
        assert frontend.last_video and frontend.last_video[:2] == (256,384)
        report.update(passed=True, last_observation=last, images=frontend.capture('complete'))
    except Exception:
        report['error'] = traceback.format_exc()
        if frontend.last_video:report['failure_images']=frontend.capture('failure')
    finally:
        report.update(checks=frontend.checks, frontend_frames=frontend.frames)
        frontend.close()
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    assert report['passed'], report.get('error')
    return report


def time_emulator(core, shared):
    directory = OUT/'time/emulator';directory.mkdir(parents=True, exist_ok=True)
    elf = OUT/'time/dok_nds_time.elf';rom = elf.with_suffix('.nds')
    header = ROOT/'ports/nds/platform/time_probe/time_probe.h'
    names = time_probe.fields(header,'NdsTimeProbe');trace_names=time_probe.fields(header,'NdsTimeTraceRecord')
    address,size=transport.elf_symbol(elf,'g_nds_time_probe');assert size == len(names)*4
    frontend=transport.Frontend(core,directory,'missing-media')
    report=dict(passed=False);records=[];observations=[];inputs=[]
    live_start=pause_start=resume_start=None;press_pause=press_resume=False;completed=0;final=None
    try:
        frontend.load(rom)
        for frame in range(2400):
            joypad=0
            if press_pause and pause_start is None:joypad=1<<3
            if pause_start is not None and resume_start is None:
                if frame-pause_start<120:joypad=1<<3
                elif frame-pause_start<125:joypad=0
                else:joypad=1<<3;press_resume=True
            if resume_start is not None:joypad=0
            frontend.joypad=joypad
            if not inputs or inputs[-1]['joypad']!=joypad:inputs.append(dict(frontend_frame=frame,joypad=joypad))
            tick(frontend)
            probe=time_probe.observe(frontend,address,names,0x38544b44)
            if probe is None:continue
            report['last_observation']=probe
            assert probe['status']!=6 and not probe['error_bits'] and not probe['failures']
            assert probe['trace_capacity']==512 and probe['trace_record_bytes']==len(trace_names)*4
            while len(records)<probe['trace_total']:
                ordinal=len(records)+1
                row=time_probe.observe(frontend,probe['trace_address']+((ordinal-1)%512)*len(trace_names)*4,trace_names,0x38564b44)
                if row is None:break
                assert row['ordinal']==ordinal
                records.append(row)
            if probe['status']>=2:
                assert (probe['counter_hz_numerator'],probe['counter_hz_denominator'])==(33513982,64)
                assert frontend.core.retro_get_memory_size(2)==4194304 and not probe['is_dsi']
                observations.append(probe)
            if probe['status']==2:
                if live_start is None:live_start=frame
                if frame-live_start>=90:press_pause=True
            if probe['status']==3:
                if pause_start is None:pause_start=frame
                elif not press_resume:assert (probe['native_now'],probe['callback_total'])==(observations[-2]['native_now'],observations[-2]['callback_total'])
                if frame-pause_start>=3 and 'paused_images' not in report:report['paused_images']=frontend.capture('paused')
            if probe['status']==4 and resume_start is None:
                assert press_resume
                resume_start=frame
            if probe['status']==5:
                final=probe;completed+=1
                if completed==3:break
        assert final and pause_start is not None and resume_start is not None and len(records)==final['trace_total']
        report['trace_checks']=time_probe.verify_traces(records)
        report['memory']=time_probe.verify_memory(final,elf)
        assert final['events_bank_bytes']==EXPECTED[1]
        for field in ('checks','failures','first_failed_line','clock_cases','event_cases'):assert final[field]==shared[field]
        raw=[time_probe.u64(row,'raw_counter') for row in observations]
        assert all(b>=a for a,b in zip(raw,raw[1:]))
        first,last=observations[0],observations[-1];vblanks=last['vblank_count']-first['vblank_count']
        expected=Fraction(vblanks*560190,64);actual=time_probe.u64(last,'raw_counter')-time_probe.u64(first,'raw_counter')
        assert vblanks>=120 and abs(actual-expected)<=expected/50
        report.update(passed=True,traces=records,observations=observations,inputs=inputs,
            counter_cadence=dict(vblanks=vblanks,counts=actual,expected_numerator=expected.numerator,
                                 expected_denominator=expected.denominator,tolerance='1/50'),
            images=frontend.capture('complete'))
    except Exception:
        report['error']=traceback.format_exc()
        if frontend.last_video:report['failure_images']=frontend.capture('failure')
    finally:
        report['frontend_frames']=frontend.frames;frontend.close()
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    assert report['passed'],report.get('error')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,default=CORE)
    parser.add_argument('--skip-build',action='store_true',help='Reuse only this isolated build after verifying its manifest pins')
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    old=preserved();budget=assert_budget()
    assert transport.sha(args.core)==time_probe.CORE_SHA
    sources=[Path(__file__).resolve(),ROOT/'ports/nds/Makefile.n03',ROOT/'ports/nds/Makefile.n08',
             ROOT/'ports/nds/tools/run_memory_emulator.py',ROOT/'ports/nds/tools/run_time_emulator.py',
             ROOT/'ports/nds/tools/run_diag_emulator.py',ROOT/'ports/nds/toolchain_versions.json']
    for folder in ('include','platform/memory','platform/time','platform/time_probe'):
        sources += sorted(p for p in (ROOT/'ports/nds'/folder).rglob('*') if p.suffix in ('.c','.h'))
    for name in ('memory','contracts','time','events'):sources.append(ROOT/f'ports/nds/core/{name}.c')
    for name in ('test_memory','test_contracts','contracts_compile','time_cases','test_time','time_vectors','events_vectors','time_compile'):
        sources.append(ROOT/f'ports/nds/tests/{name}.c')
    sources.append(ROOT/'ports/nds/tests/time_cases.h')
    recipe=json.loads((ROOT/'research/evidence/nds/n22/host/check.json').read_text('utf8'))
    sources += [ROOT/value for value in recipe['command'] if str(value).endswith('.c')]
    sources += [ROOT/'ports/nds/tools/flow/test_retry_overlap.c',
                ROOT/'ports/nds/platform/gameplay_probe/gameplay.h',
                ROOT/'ports/nds/platform/assets/file_reader.h',
                ROOT/'build/nds/n07/generated/render_trig.inc']
    sources=sorted(set(sources))
    pins=[artifact(p) for p in sources]
    if args.skip_build:
        built=json.loads((OUT/'build-manifest.json').read_text('utf8'))
        assert built['inputs']==pins
        for item in built['artifacts']:assert artifact(ROOT/item['path'])==item
    else:
        built=build();built['inputs']=pins
        (OUT/'build-manifest.json').write_text(json.dumps(built,indent=2)+'\n','utf8')
    report=dict(task='N22',passed=False,scope='Fresh isolated N03/N08 memory/time regression for N22 rebalance; no ordinary-playthrough acceptance',
                budget=budget,build=built,inputs=pins,core=artifact(args.core),preserved_before=old)
    try:
        shared=host();report['host']=shared
        report['memory_emulator']=memory_emulator(args.core)
        report['time_emulator']=time_emulator(args.core,shared['time']['shared_suite'])
        assert pins==[artifact(p) for p in sources]
        assert old==preserved()
        report.update(passed=True,preserved_unchanged=True)
    except Exception:report['error']=traceback.format_exc()
    finally:
        snapshots=[]
        for source in sources:
            destination=EVIDENCE/'inputs'/source.relative_to(ROOT);destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,destination)
            original,snapshot=artifact(source),artifact(destination)
            assert original['sha256']==snapshot['sha256']
            snapshots.append(dict(original=original,snapshot=snapshot))
        report['source_snapshots']=snapshots
        (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
        (EVIDENCE/'report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=report['passed'],error=report.get('error'),report=artifact(EVIDENCE/'report.json'))))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())

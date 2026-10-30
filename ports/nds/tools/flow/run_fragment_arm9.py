"""Read-only SDK-fragmentation fixture observer; no allocator/RAM writes."""
import argparse,json,struct,sys,traceback,shutil
from pathlib import Path
from check_host import ROOT,pin
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
import run_diag_emulator as transport
import run_renderer_emulator as render
import run_frontend_emulator as frontend
from frontend_fat_oracle import volume_files,audit_changes

def main():
    parser=argparse.ArgumentParser();parser.add_argument('variant',choices=('baseline','partial'));args=parser.parse_args()
    target=ROOT/f'build/nds/n22/fragment-{args.variant}-01';evidence=ROOT/f'research/evidence/nds/n22/fragment-{args.variant}-01'
    directory=target/'emulator';directory.mkdir(parents=True,exist_ok=True);evidence.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((target/'build-manifest.json').read_text(encoding='utf-8'))
    for row in manifest['inputs']:
        assert pin(ROOT/row['current']['path'])==row['current'];assert pin(ROOT/row['snapshot']['path'])==row['snapshot']
    assert transport.sha(frontend.CORE)==render.CORE_SHA
    address,size=transport.elf_symbol(target/'dok_nds_fragment.elf','g_nds_fragment_probe');assert size==256
    observer=None;report=dict(task='N22',variant=args.variant,passed=False,driver=pin(Path(__file__).resolve()),
        build_manifest=pin(target/'build-manifest.json'),core=dict(path=str(frontend.CORE),sha256=transport.sha(frontend.CORE)),
        scope='Public SDK/backend calls reconstruct exact physical allocation layout; generated fixture texels; no ordinary-playthrough or physical-DS claim')
    try:
        observer=transport.Frontend(frontend.CORE,directory,'happy')
        image=observer.save/'melonDS DS/dldi_sd_card.bin';frontend.write_fixture(image)
        observer.load(target/'dok_nds_fragment.nds');transport.flush_native_stdio();before=volume_files(image)
        report['options']=observer.options.copy();report['states']=[]
        for _ in range(18000):
            if observer.frame_time and observer.frame_time.callback:observer.frame_time.callback(round(1000000/observer.av.timing.fps))
            observer.core.retro_run();observer.frames+=1;observer.pcm.clear()
            assert not observer.callback_error and not observer.shutdown
            p=struct.unpack('<64I',render.ram(observer,address,256))
            if p[0]!=0x22464b44 or p[2]&1:continue
            if not report['states'] or report['states'][-1]['stage']!=p[7]:report['states'].append(dict(frame=observer.frames,stage=p[7],probe=list(p)))
            report['last_probe']=list(p)
            assert p[3]!=3,('Actual ARM9 assertion line',p[4],list(p))
            if p[3]!=2:continue
            report['frames']=observer.frames;report['actual_probe']=list(p)
            raw=render.ram(observer,p[28],p[29]*p[30]);records=[dict(zip(('phase','index','key_lo','key_hi','address','bytes','sdk_id','first_byte'),struct.unpack_from('<8I',raw,offset)))for offset in range(0,len(raw),32)]
            for row in records:row['key']=row['key_lo']|(row['key_hi']<<32)
            report['records']=records
            assert len(records)==56 and [sum(r['phase']==phase for r in records)for phase in(0,1,2)]==[21,16,19]
            for phase,name in ((0,'prefix'),(1,'previous')):
                for row,want in zip((r for r in records if r['phase']==phase),manifest['layout'][name],strict=True):
                    assert (row['key'],row['address'],row['bytes'])==(want['key'],want['gpu'],want['bytes'])
            snapshot=render.ram(observer,p[38],393216);palette=render.ram(observer,p[39],8192)
            order={row['key']:n for n,row in enumerate(manifest['layout']['prefix'])}
            byte_checks=0
            for row,want in zip((r for r in records if r['phase']==2),manifest['layout']['final'],strict=True):
                assert row['key']==want['key'] and row['bytes']==want['bytes']
                ordinal=order.get(row['key'],21+row['index'])
                expected=bytes((ordinal*11+17+i*37+(i>>8)*13)%254+1 for i in range(row['bytes']))
                offset=row['address']-0x06800000 if row['address']<0x06840000 else 262144+row['address']-0x06860000
                assert snapshot[offset:offset+row['bytes']]==expected,(row['key'],hex(row['address']))
                assert row['first_byte']==expected[0];byte_checks+=row['bytes']
            expected_palette=b''.join(struct.pack('<H',((i*3)&31)|(((i*5)&31)<<5)|(((i*7)&31)<<10))for i in range(256))*16
            assert palette==expected_palette and p[24]==p[26]==p[27]==p[35]==p[37]==1
            assert p[34]==6 and p[40]==0 and p[17]==19 and p[16]==358656
            if args.variant=='partial':
                assert p[10]==67840 and p[9]==0 and p[23]==1 and p[11]==5 and p[12]==14
                moved=[row for row in records if row['phase']==2 and row['key'] in order and row['address']!=manifest['layout']['prefix'][order[row['key']]]['gpu']]
                assert len(moved)==1 and moved[0]['key']==5398598656 and moved[0]['bytes']==16384,moved
                report['relocated']=moved
                width,height,rgb=observer.rgb();pixels=[]
                for row in (r for r in records if r['phase']==2):
                    x=24+(row['index']%5)*52;y=18+(row['index']//5)*37;c=row['first_byte']
                    want=tuple(v*255//31 for v in ((c*3)&31,(c*5)&31,(c*7)&31));got=tuple(rgb[(y*width+x)*3:(y*width+x)*3+3])
                    pixels.append(dict(index=row['index'],xy=[x,y],expected=want,actual=got))
                report['pixels']=pixels
                assert all(max(abs(a-b)for a,b in zip(row['expected'],row['actual']))<=4 for row in pixels),pixels
            else:assert p[10]==367360 and p[9]==6 and p[23]==0
            report['actual']=dict(checks=p[5],uploads=p[11],upload_bytes=p[10],commit_result=p[9],commit_start=p[18],commit_end=p[19],
                map_start=p[20],map_end=p[21],dma_epoch_ticks=p[22],deadline_pass=bool(p[23]),resident_bytes=p[16],resident_count=p[17],
                pixel_bytes_verified=byte_checks,palette_bytes_verified=len(palette),old_source_poisoned=p[32],current_source_verified=p[33],
                mapping_unchanged=bool(p[27]),planning_payload_unchanged=bool(p[24]),active_guard_result=p[34],active_guard_payload_unchanged=bool(p[35]),
                palette_upload_bytes=p[10]-sum(row['bytes']for row in records if row['phase']==2) if args.variant=='baseline' else 0)
            (evidence/'vram.bin').write_bytes(snapshot);(evidence/'palette.bin').write_bytes(palette);(evidence/'records.bin').write_bytes(raw)
            report['snapshots']=[pin(evidence/name)for name in('vram.bin','palette.bin','records.bin')]
            report['screens']=[]
            for capture in observer.capture('actual'):
                source=Path(capture['path']);destination=evidence/source.name;shutil.copyfile(source,destination);report['screens'].append(pin(destination))
            report['passed']=True;break
        else:raise AssertionError('Fixture deadline expired')
    except Exception:
        report['error']=traceback.format_exc()
        if observer and observer.last_video:report['failure_screens']=observer.capture('failed')
    finally:
        if observer:
            observer.close();transport.flush_native_stdio()
            if 'before' in locals():report['disk']=audit_changes(before,volume_files(image),[])
        report['observer_inputs']=[]
        loaded={Path(module.__file__).resolve() for module in tuple(sys.modules.values())
                if getattr(module,'__file__',None) and str(module.__file__).endswith('.py')}
        for source in sorted(loaded):
            if not source.is_relative_to(ROOT):continue
            destination=evidence/'observer-inputs'/source.relative_to(ROOT);destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,destination);current,snapshot=pin(source),pin(destination)
            assert current['sha256']==snapshot['sha256'];report['observer_inputs'].append(dict(current=current,snapshot=snapshot))
        (evidence/'emulator.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items()if k in('passed','actual','error','last_probe')}))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())

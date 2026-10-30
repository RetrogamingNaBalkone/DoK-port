"""Pinned-ELF read-only AI sampler for ordinary-input navigation assistance.

`sample(read_ram, mmap=published_mmap)` never executes target code or writes
emulated state. It rejects inconsistent model-RAM snapshots. Physical ARM9
cache coherence is not certified by these checks and is not a hardware claim.
"""
import json,struct
from pathlib import Path
from check_host import ROOT,pin
import sys
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from run_diag_emulator import elf_symbol

class LiveAiReader:
    def __init__(self,elf,layout=None):
        self.elf=Path(elf).resolve()
        digest=pin(self.elf)['sha256']
        if layout is None:
            available=[ROOT/'research/evidence/nds/n22'/name for name in (
                'candidate01-ai-layout.json','candidate01-en-ai-layout.json',
                'candidate02-ai-layout.json','candidate02-en-ai-layout.json')]
            candidates=[json.loads(path.read_text(encoding='utf-8'))for path in available if path.exists()]
            matches=[item for item in candidates if item['elf']['sha256']==digest]
            assert len(matches)==1,'No unique pinned layout; derive DWARF again for this ELF'
            self.layout=matches[0]
        else:self.layout=json.loads(Path(layout).read_text(encoding='utf-8'))
        assert digest==self.layout['elf']['sha256'],'Derive DWARF layout again for this different ELF'
        self.flow_address,size=elf_symbol(self.elf,'g_nds_flow_probe');assert size==128

    def sample(self,read_ram,*,mmap=None,width=64,side=480):
        layout=self.layout
        def read(address,size):
            if not 0x02000000<=address<=0x02400000-size:raise ValueError('Pointer outside DS main RAM')
            data=read_ram(address,size)
            if len(data)!=size:raise ValueError('Short RAM read')
            return data
        u32=lambda data,offset:struct.unpack_from('<I',data,offset)[0]
        i32=lambda data,offset:struct.unpack_from('<i',data,offset)[0]
        u16=lambda data,offset:struct.unpack_from('<H',data,offset)[0]
        i16=lambda data,offset:struct.unpack_from('<h',data,offset)[0]
        def denied(reason):return dict(accepted=False,reason=reason,enemies=[])
        try:
            flow=struct.unpack('<32I',read(self.flow_address,128))
            if flow[0]!=0x22504b44 or flow[2]&1 or not flow[3] or flow[27] or flow[28]:return denied('No stable live flow boundary')
            storage=u32(read(layout['storage_symbol']['address'],4),0)
            game=storage+layout['sprite_world']['members']['game'];ai=game+layout['game']['members']['ai']
            a=read(ai,layout['ai']['bytes']);am=layout['ai']['members'];world=u32(a,am['world'])
            if u32(a,am['busy']) or u32(a,am['fault']):return denied('AI busy/fault')
            if u32(a,am['epoch'])!=flow[29] or u32(a,am['callbacks'])!=flow[16]:return denied('AI epoch/callbacks differ from published flow')
            scheduler=read(u32(a,am['scheduler']),layout['scheduler']['bytes']);sm=layout['scheduler']['members']
            if u32(scheduler,sm['_dispatching']) or u32(scheduler,sm['_in_callback']):return denied('Actual scheduler inside dispatch')
            hero=read(game+layout['game']['members']['hero'],layout['motion']['bytes']);mm=layout['motion']['members']
            if (i32(hero,mm['x']),i32(hero,mm['y']),i32(hero,mm['z']),i16(hero,mm['heading']))!=tuple((v if v<0x80000000 else v-0x100000000) for v in flow[8:12]):
                return denied('Private hero pose differs from published flow')
            w=read(world,layout['world']['bytes']);wm=layout['world']['members'];count=u16(w,wm['object_count'])
            if not 0<count<=256 or count!=flow[19] or u32(w,wm['epoch'])!=flow[29]:return denied('World identity differs from published flow')
            enemy_count=u32(a,am['count'])
            if enemy_count>128:return denied('AI count outside pool')
            definition=u32(w,0);dm=layout['definition']['members'];od=layout['object_definition'];om=od['members']
            result=[];em=layout['enemy']['members'];seedm=layout['seed']['members']
            for n in range(enemy_count):
                offset=am['enemies']+n*layout['enemy']['bytes'];enemy=a[offset:offset+layout['enemy']['bytes']]
                glyph=enemy[em['glyph']]
                if u16(a,am['glyph_index']+glyph*2)!=n+1:return denied('AI glyph-index mismatch')
                seed_index=u16(w,wm['glyph_index']+glyph*2)
                if not 0<seed_index<=count:return denied('World glyph-index mismatch')
                seed_offset=wm['objects']+(seed_index-1)*layout['seed']['bytes'];seed=w[seed_offset:seed_offset+layout['seed']['bytes']]
                reference=struct.unpack_from('<III',seed,seedm['reference'])
                if reference[0]!=flow[29] or not reference[1] or not reference[2] or u16(seed,seedm['class_id'])!=9:return denied('Actor reference/class mismatch')
                if not u16(seed,seedm['pose_known']):continue
                x,y,z=(i32(enemy,em['motion']+mm[name])for name in ('x','y','z'))
                if (x,y,z)!=tuple(i32(seed,seedm[name])for name in ('x','y','z')):return denied('AI and published seed pose disagree')
                definition_index=u16(seed,seedm['definition_index'])
                raw=read(definition+dm['objects']+definition_index*od['bytes'],od['bytes'])
                if raw[om['glyph']]!=glyph or u16(raw,om['class_id'])!=9:return denied('Definition identity mismatch')
                health=i16(enemy,em['health']);state=enemy[em['state']];corpse=enemy[em['corpse']]
                if state>4 or corpse>1:return denied('Actor state outside defined domain')
                cell=(z//side)*width+x//side
                result.append(dict(index=n,glyph=glyph,reference=list(reference),x=x,y=y,z=z,
                    heading=i16(enemy,em['motion']+mm['heading']),angle_parameter=i16(enemy,em['motion']+mm['angle_parameter']),
                    health=health,state=state,corpse=bool(corpse),last_motion_ticks=u32(enemy,em['last_motion_ticks']),
                    burst_started=u32(enemy,em['burst_started']),height_parameter=i16(raw,om['height_parameter']),
                    definition_flags=u16(raw,om['flags']),upper_y=i16(seed,seedm['upper_y']),lower_y=i16(seed,seedm['lower_y']),
                    cell=cell,cell_xy=[cell%width,cell//width],occupancy_match=(None if mmap is None else 0<=cell<len(mmap) and mmap[cell]==glyph)))
            after=struct.unpack('<32I',read(self.flow_address,128))
            if after!=flow:return denied('Published flow changed during read')
            return dict(accepted=True,enemies=result,storage=storage,ai_address=ai,world_address=world,epoch=flow[29],
                        callbacks=flow[16],presented=flow[30],flow_sequence=flow[2],
                        scope='Consistent read-only emulator model-RAM snapshot; physical ARM9 cache coherence is unproved')
        except (ValueError,IndexError,struct.error) as error:return denied(str(error))

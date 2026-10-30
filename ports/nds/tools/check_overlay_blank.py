"""Extract actual old/new overlay blocks; compare VRAM across state transitions."""
import json,os,subprocess
from pathlib import Path
from check_strip_equality import ROOT,E,pin
OUT=ROOT/'build/nds/n21p/overlay-blank'

def block(source):
    a=source.index('    /* Only a confirmed blank') if '    /* Only a confirmed blank' in source else source.index('    unsigned char *overlay=')
    return source[a:source.index('    /* Weapon admission',a)]

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=E/'pre-overlay-blank/pins.json';rows=json.loads(archive.read_text('utf8'))['source_pins']
    for row in rows:assert pin(ROOT/row['archive'])['sha256']==row['sha256']
    old=(ROOT/rows[0]['archive']).read_text('utf8');path=ROOT/rows[0]['path'];new=path.read_text('utf8')
    restored=new.replace(block(new),block(old)).replace('#if GAMEPLAY_UI\n    ui->overlay_blank=0;\n#endif\n','',1)
    assert restored==old,'Unscoped main change'
    header=ROOT/'ports/nds/platform/ui/runtime.h';oldheader=(ROOT/rows[1]['archive']).read_text('utf8')
    assert header.read_text('utf8').replace('    uint16_t overlay_blank;\n','',1)==oldheader
    front=ROOT/'ports/nds/platform/frontend/loop.inc';oldfront=(ROOT/rows[2]['archive']).read_text('utf8')
    assert front.read_text('utf8').replace('ui->dirty=1;ui->overlay_blank=0;','ui->dirty=1;',1)==oldfront
    (OUT/'old-runtime.h').write_text(oldheader.replace('NDS_UI_RUNTIME_H','OLD_UI_RUNTIME_H').replace('NdsUiRuntime','OldUiRuntime').replace('nds_ui_','old_nds_ui_'),'utf8')
    generated=[]
    for name,source in [('old',old),('new',new)]:
        text='''#include "runtime.h"
#include "dok/weapon_overlay.h"
#include "dok/assets.h"
#include <string.h>
#define GAMEPLAY_UI 1
#define GAMEPLAY_FAST_OVERLAY 1
#define SPRITE_AUDIT (audit&1u)
#define N21P_AUDIT (audit&2u)
static unsigned char *target;
static unsigned uploads;
static uint32_t g_nds_effect_probe[32];
static void DC_FlushRange(const void *p,unsigned n){(void)p;(void)n;}
static void *bgGetGfxPtr(int id){(void)id;return target;}
static void dmaCopyWords(int id,const void *p,void *q,unsigned n){(void)id;memcpy(q,p,n);++uploads;}
'''
        text+='int '+name+'_overlay(const dok_weapon_view *v,const unsigned char *pixels,uint16_t *blank,unsigned audit,unsigned char *scratch,unsigned char *vram,unsigned *count){\n'
        text+='NdsUiRuntime state={0},*ui=&state;ui->overlay_blank=*blank;dok_weapon_view weapon=*v;const unsigned char *weapon_pixels=pixels;dok_asset_entry entry={0};entry.storage_width=8;entry.bytes=64;const dok_asset_entry *weapon_entry=v->visible?&entry:NULL;int hud_bg=0;target=vram;uploads=0;(void)ui;(void)audit;\n'
        text+=block(source)
        text+='*blank=ui->overlay_blank;*count=uploads;return 1;}\n'
        p=OUT/(name+'.c');p.write_text(text,'utf8');generated.append(p)
    sources=[path,header,front,ROOT/'ports/nds/tests/overlay_blank_equivalence.c',ROOT/'ports/nds/render/weapon_overlay.c',ROOT/'ports/nds/render/weapon_overlay_fast.c',Path(__file__)]
    before=[pin(p) for p in sources]
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(OUT/'zig-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';exe=OUT/'overlay-blank.exe'
    cmd=[str(zig),'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O2','-Iports/nds/include','-Iports/nds/platform/ui','-I'+str(OUT),*[str(p) for p in generated],'ports/nds/tests/overlay_blank_equivalence.c','ports/nds/render/weapon_overlay.c','ports/nds/render/weapon_overlay_fast.c','-o',str(exe)]
    subprocess.run(cmd,cwd=ROOT,env=env,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=ROOT,text=True));assert result['passed']
    assert before==[pin(p) for p in sources]
    report=dict(task='N21P hidden overlay blank validity',passed=True,numeric=result,source_pins=before,archive=pin(archive),generated=[pin(p) for p in generated],binary=pin(exe),command=cmd,scope='Actual extracted old/new overlay blocks with real overlay renderers and mocked synchronous flush/DMA; all VRAM bytes and HUD tail compared. Whole-source restoration proves only blank flag, UI-level invalidation and bounded block admission changed. Public renderers unchanged; audit always full path.')
    (E/'overlay-blank-equivalence.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(result))
if __name__=='__main__':main()

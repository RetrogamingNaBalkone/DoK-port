"""Actual camera-plan arithmetic versus integer GE translations, including fallback."""
import json
import os
from pathlib import Path
import subprocess
from audit_pipeline import ROOT,pin

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'map'))
from compiler_cache import compiler_environment


def main():
    source=ROOT/'ports/nds/platform/render_probe/render_backend.c';text=source.read_text()
    def extract(signature):
        start=text.index(signature);end=text.index('{',start)+1;depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}');end+=1
        return text[start:end]
    out=ROOT/'build/nds/n23/relative20-math';out.mkdir(exist_ok=True)
    code='#include "render_backend.h"\n#include <stdio.h>\n#include <string.h>\n#include <limits.h>\n#define DOK_NDS_HOT_CODE\ntypedef struct {int m[16];} m4x4;\ntypedef struct {int m[12];} m4x3;\n#include "render_trig.inc"\n'
    a=text.index('static const int32_t projection[16]');b=text.index('};',a)+2;code+=text[a:b]+'\n'
    code+=extract('static int64_t floor_div(')+'\n'+extract('static int32_t nearest_div(')+'\n'
    code+=extract('DOK_NDS_HOT_CODE static dok_nds_render_result modelview(')+'\n'
    a=text.index('typedef struct CameraPlan');b=text.index('} CameraPlan;',a)+13;code+=text[a:b]+'\n'
    code+=extract('DOK_NDS_HOT_CODE static dok_nds_render_result camera_plan(')+'\n'
    code+=extract('DOK_NDS_HOT_CODE static dok_nds_render_result planned_cell_clip(')+'\n'
    code+=r'''
static uint32_t rng=0x234D12u,translated,fallbacks,rejected,checks;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int step(const CameraPlan *plan,const dok_nds_render_cell *previous,
    const dok_nds_render_cell *next,m4x4 *matrix){
    m4x4 expected;int64_t delta[3]={((int64_t)next->origin_x-previous->origin_x)*8,
        ((int64_t)next->origin_y-previous->origin_y)*8,((int64_t)next->origin_z-previous->origin_z)*8};
    if(planned_cell_clip(plan,next,&expected)!=DOK_NDS_RENDER_OK){++rejected;return 2;}
    if(delta[0]<INT32_MIN||delta[0]>INT32_MAX||delta[1]<INT32_MIN||delta[1]>INT32_MAX||
       delta[2]<INT32_MIN||delta[2]>INT32_MAX){*matrix=expected;++fallbacks;}
    else{
        for(uint32_t row=0;row<4u;++row){
            int64_t sum=(int64_t)matrix->m[12u+row]*4096;
            for(uint32_t k=0;k<3u;++k)sum+=(int64_t)matrix->m[k*4u+row]*delta[k];
            if(sum%4096)return 0;
            sum/=4096;if(sum<INT32_MIN||sum>INT32_MAX)return 0;
            matrix->m[12u+row]=(int)sum;
        }
        ++translated;
    }
    ++checks;return !memcmp(matrix,&expected,sizeof(expected));
}
int main(void){
    for(uint32_t heading=0;heading<=360u;++heading)for(uint32_t pose=0;pose<16u;++pose){
        dok_nds_render_camera camera={(int32_t)(random32()%1000001u)-500000,
            (int32_t)(random32()%1000001u)-500000,(int32_t)(random32()%1000001u)-500000,
            dok_render_sine[heading],dok_render_cosine[heading]};
        CameraPlan plan;if(camera_plan(&camera,&plan)!=DOK_NDS_RENDER_OK)return 1;
        dok_nds_render_cell previous={plan.anchor_x,plan.anchor_y,plan.anchor_z,NULL,0};
        m4x4 matrix;if(planned_cell_clip(&plan,&previous,&matrix)!=DOK_NDS_RENDER_OK)return 2;
        for(uint32_t n=0;n<128u;++n){
            dok_nds_render_cell next={plan.anchor_x+((int32_t)(random32()%129u)-64)*512,
                plan.anchor_y+((int32_t)(random32()%17u)-8)*512,
                plan.anchor_z+((int32_t)(random32()%129u)-64)*512,NULL,0};
            if(n%13u==0u)next.origin_x=INT32_MAX-511;
            int result=step(&plan,&previous,&next,&matrix);if(!result)return 3;
            if(result==1)previous=next;
        }
    }
    dok_nds_render_camera camera={0,0,0,dok_render_sine[45],dok_render_cosine[45]};CameraPlan plan;
    dok_nds_render_cell a={-146800640,0,-146800640,NULL,0},b={146800640,0,146800640,NULL,0};m4x4 matrix;
    if(camera_plan(&camera,&plan)!=DOK_NDS_RENDER_OK||planned_cell_clip(&plan,&a,&matrix)!=DOK_NDS_RENDER_OK)return 4;
    if(step(&plan,&a,&b,&matrix)!=1||!fallbacks||!translated||!rejected)return 5;
    printf("{\"passed\":true,\"matrices\":%u,\"translations\":%u,\"fallbacks\":%u,\"range_rejections\":%u}\n",checks,translated,fallbacks,rejected);return 0;
}
'''
    program=out/'relative_math.c';program.write_text(code)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    command=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Iports/nds/include',
        '-Iports/nds/platform/render_probe','-Ibuild/nds/n07/generated',str(program),'-o',str(out/'relative_math.exe')]
    env=compiler_environment(ROOT)
    subprocess.run(command,cwd=ROOT,env=env,capture_output=True,check=True)
    run=subprocess.run([out/'relative_math.exe'],capture_output=True,text=True,check=True);report=json.loads(run.stdout)
    report.update(task='N23',scope=__doc__,driver=pin(Path(__file__)),backend=pin(source),generated=pin(program),command=command)
    destination=ROOT/'research/evidence/nds/n23/relative20-math.json';assert not destination.exists()
    destination.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()

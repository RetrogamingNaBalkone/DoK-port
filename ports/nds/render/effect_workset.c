#include "dok/effect_workset.h"
#include <limits.h>

dok_status dok_effect_workset_bound(const dok_render_world *w,uint32_t *out,
    dok_effect_workset_record records[DOK_RENDER_MATERIALS],uint32_t *out_count)
{
    static const uint16_t pairs[4]={0x0440,0x0880,0x1100,0x2200};
    uint32_t sum=0,count=0;
    if(!w||!out||w->material_count>DOK_RENDER_MATERIALS||w->effective.object_count>DOK_LEVEL_MAX_OBJECTS)
        return DOK_STATUS_INVALID_ARGUMENT;
    for(uint32_t m=0;m<w->material_count;++m){uint32_t water=0,flags=0,code=0,bytes=0;
        for(uint32_t i=0;i<w->effective.object_count;++i){const dok_level_object *o=&w->effective.objects[i];
            if(o->class_id==12u){const dok_geometry_image *image=&w->bindings[o->glyph].images[DOK_GEOMETRY_FLOOR];
                if(image->material==m+1u){water=1;flags=image->flags;}}}
        if(!water)continue;
        const dok_asset_entry *e=&w->materials[m].entry;
        for(uint32_t j=0;j<4u;++j)if(flags&pairs[j]){code=j*2u+((flags&(pairs[j]&0xff00u))?2u:1u);break;}
        if(code){
            bytes=!(code&1u)&&e->width==96u&&e->height==96u?32768u:e->bytes;
            if(!bytes||bytes>UINT32_MAX-sum)return DOK_STATUS_LIMIT_REACHED;
            sum+=bytes;
        }
        if(records)records[count]=(dok_effect_workset_record){e->id,m,flags,code,bytes};
        ++count;
    }
    *out=sum;if(out_count)*out_count=count;
    return DOK_STATUS_OK;
}

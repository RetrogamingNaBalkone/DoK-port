#include "items_cases.h"
#include <stdio.h>
static dok_items_cases_workspace workspace;
int main(void)
{
    dok_items *s=&workspace.items;int c,v,h,e,hm,em,k,score;uint32_t a;
    dok_initial_world *w=&workspace.world.initial;dok_level_definition *d=&workspace.world.effective;
    d->object_count=w->object_count=2;d->language=1;d->settings[0]=d->settings[1]=d->settings[2]=1;d->settings[3]=480;
    d->objects[0].glyph='M';d->objects[0].class_id=1;d->glyph_index['M']=w->glyph_index['M']=1;
    w->definition=d;w->epoch=1;w->hero=(dok_object_ref){1,1,1};w->objects[0].reference=w->hero;w->objects[0].class_id=1;
    d->objects[1].glyph=' ';d->objects[1].class_id=2;d->objects[1].flags=16;
    d->glyph_index[' ']=w->glyph_index[' ']=2;w->objects[1].reference=(dok_object_ref){1,2,1};
    w->objects[1].definition_index=1;w->objects[1].class_id=2;
    w->maps[0][0]=w->maps[1][0]=w->maps[2][0]=' ';
    if(dok_collision_bind(&workspace.collision,w)!=DOK_STATUS_OK||dok_items_bind(s,w,&workspace.collision,NULL,NULL)!=DOK_STATUS_OK)return 2;
    while(scanf("%d%d%d%d%d%d%d%d",&c,&v,&h,&e,&hm,&em,&k,&score)==8){
        s->inventory.health=(int16_t)h;s->inventory.charge=(int16_t)e;s->inventory.max_health=(int16_t)hm;s->inventory.max_charge=(int16_t)em;
        s->inventory.keys=(uint16_t)k;s->inventory.score=(int16_t)score;s->inventory.map_available=0;
        if(dok_items_stat(s,(int16_t)c,(int16_t)v,&a)!=DOK_STATUS_OK)return 3;
        printf("%lu %d %d %d %d %u %d %u\n",(unsigned long)a,s->inventory.health,s->inventory.charge,s->inventory.max_health,
         s->inventory.max_charge,s->inventory.keys,s->inventory.score,s->inventory.map_available);
    }return 0;
}

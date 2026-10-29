/* N11 original world fixture wire. Strict LE, no expected geometry. */
#include "dok/interactions_move.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "render_trig.inc"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
static dok_level_definition definition;
static dok_initial_world world;
static dok_collision_state collision;
static dok_interactions_state interactions;
static dok_event_bank bank;
static dok_event_scheduler scheduler;
static unsigned char maps[3][4096],known[256],hero;
static uint16_t flags[256],classes[256];
static int32_t categories[256],values[256],events[256],targets[256];
static uint32_t finishes,last_reason;
static dok_status move_spy(void *context,dok_object_ref actor,dok_motion_state *motion,int32_t x,int32_t z)
{dok_collision_report report;dok_status status=dok_collision_displace(context,actor,motion,x,z,&report);
 if(status==DOK_STATUS_OK)last_reason=report.reason_mask;return status;}
static void sink(void *context,const dok_interaction_output *out)
{(void)context;if(out->kind==DOK_INTERACTION_FINISH)++finishes;}
static int32_t width,length,side,radius;
static uint32_t u32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int32_t i32(const unsigned char *p)
{uint32_t n=u32(p);return n<=INT32_MAX?(int32_t)n:(int32_t)((int64_t)n-INT64_C(4294967296));}
static int bytes(void *p,size_t n){return fread(p,1,n,stdin)==n;}
static int emit(uint32_t n){unsigned char b[4]={(unsigned char)n,(unsigned char)(n>>8),(unsigned char)(n>>16),(unsigned char)(n>>24)};return fwrite(b,1,4,stdout)==4;}
static dok_status clock_read(void *context,uint32_t *out){(void)context;*out=0;return DOK_STATUS_OK;}
static void callback(void *context,dok_event_scheduler *s,dok_object_ref ref,uint16_t id,uint32_t p)
{(void)context;(void)s;(void)ref;(void)id;(void)p;}
static int trig(void *context,uint32_t heading,int32_t *sine,int32_t *cosine)
{(void)context;if(heading>360)return 0;*sine=dok_render_sine[heading];*cosine=dok_render_cosine[heading];return 1;}
static int init(int projection)
{
    uint32_t g,index=0;dok_event_config config;dok_interaction_services services;
    memset(&definition,0,sizeof definition);memset(&world,0,sizeof world);
    memset(&collision,0,sizeof collision);memset(&interactions,0,sizeof interactions);
    memset(&config,0,sizeof config);memset(&services,0,sizeof services);
    definition.language=1;definition.settings[0]=(int16_t)width;definition.settings[1]=1;
    definition.settings[2]=(int16_t)length;definition.settings[3]=(int16_t)side;definition.settings[7]=(int16_t)radius;
    world.definition=&definition;world.epoch=1;
    for(g=0;g<256;++g)if(known[g]){
        uint16_t cls=projection?(g==hero?1:7):classes[g];
        definition.objects[index].glyph=(unsigned char)g;definition.objects[index].flags=flags[g];definition.objects[index].class_id=cls;
        definition.glyph_index[g]=(uint16_t)(index+1);world.glyph_index[g]=(uint16_t)(index+1);
        world.objects[index].reference=(dok_object_ref){1,index+1,1};world.objects[index].definition_index=(uint16_t)index;
        world.objects[index].class_id=cls;world.objects[index].pose_known=1;world.objects[index].placements=1;
        if(classes[g]==12||classes[g]==13){
            dok_level_object *o=&definition.objects[index];
            o->field_count=2;o->fields[0].native_offset=0x66;o->fields[0].value=classes[g]==12?categories[g]:targets[g];
            o->fields[1].native_offset=classes[g]==12?0x68:0x67;o->fields[1].value=classes[g]==12?values[g]:events[g];
        }
        if(g==hero)world.hero=world.objects[index].reference;++index;
    }
    world.object_count=definition.object_count=(uint16_t)index;
    memcpy(world.maps,maps,sizeof maps);
    config.world_epoch=1;config.read_ticks=clock_read;config.callback=callback;
    services.trig=trig;services.move=move_spy;services.move_context=&collision;services.sink=sink;finishes=0;last_reason=0;
    return dok_collision_bind(&collision,&world)==DOK_STATUS_OK&&
        dok_events_init(&scheduler,&bank,&config)==DOK_STATUS_OK&&
        dok_interactions_bind(&interactions,&world,&collision,&scheduler,&services)==DOK_STATUS_OK&&
        dok_interactions_register(&interactions)==DOK_STATUS_OK;
}
int main(void)
{
    unsigned char raw[512];size_t i,cells;
#ifdef _WIN32
    if(_setmode(_fileno(stdin),_O_BINARY)==-1||_setmode(_fileno(stdout),_O_BINARY)==-1)return 2;
#endif
    if(!bytes(raw,20))return 2;width=i32(raw);length=i32(raw+4);side=i32(raw+8);radius=i32(raw+12);hero=(unsigned char)i32(raw+16);
    if(width!=64||length!=64||side!=480||radius!=238)return 2;cells=(size_t)width*(size_t)length;
    if(!bytes(raw,512))return 2;for(i=0;i<256;++i)flags[i]=(uint16_t)((uint16_t)raw[i*2]|((uint16_t)raw[i*2+1]<<8));
    if(!bytes(raw,512))return 2;for(i=0;i<256;++i)classes[i]=(uint16_t)((uint16_t)raw[i*2]|((uint16_t)raw[i*2+1]<<8));
    if(!bytes(known,256))return 2;for(i=0;i<3;++i)if(!bytes(maps[i],cells))return 2;
    for(i=0;i<256;++i){if(!bytes(raw,16))return 2;categories[i]=i32(raw);values[i]=i32(raw+4);targets[i]=i32(raw+8);events[i]=i32(raw+12);}
    for(;;){
        size_t n=fread(raw,1,20,stdin);int32_t op,x,z,nx,nz;dok_motion_state motion;dok_status status;uint32_t target=UINT32_MAX;
        if(n==0&&feof(stdin))return fflush(stdout)==0?0:2;if(n!=20)return 2;
        op=i32(raw);x=i32(raw+4);z=i32(raw+8);nx=i32(raw+12);nz=i32(raw+16);
        if(!init(op==7))return 3;memset(&motion,0,sizeof motion);motion.x=x;motion.y=-300;motion.z=z;
        if(op==7){
            uint32_t bits=0x800;motion.heading=(int16_t)nx;
            status=dok_interactions_use(&interactions,world.hero,&motion,&bits,0,30);
            for(i=0;i<256;++i)if(interactions.doors[i].extent==30){if(target!=UINT32_MAX)return 3;target=(uint32_t)i;}
            if(bits!=0)return 3;
        }else if(op==8){
            if(x<0||z<0||x>=width*side||z>=length*side)return 2;
            world.maps[0][(size_t)(z/side)*(size_t)width+(size_t)(x/side)]=hero;
            status=dok_interactions_move(&collision,world.hero,&motion,nx,nz);
        }else if(op==9||op==10){
            uint16_t previous=nz<0?0:world.glyph_index[(uint8_t)nz];
            if(previous!=0)interactions.previous_floor=world.objects[previous-1u].reference;
            world.maps[0][(size_t)(z/side)*(size_t)width+(size_t)(x/side)]=hero;
            status=dok_interactions_floor(&interactions,world.hero,&motion,op==9?0:1,(uint32_t)nx);
            for(i=0;i<256;++i)if(world.glyph_index[i]!=0&&
                world.objects[world.glyph_index[i]-1u].reference.object_id==interactions.previous_floor.object_id)target=(uint32_t)i;
        }else return 2;
        if(!emit((uint32_t)status)||!emit((uint32_t)motion.x)||!emit((uint32_t)motion.z)||!emit(target))return 2;
        if(fwrite(world.maps[0],1,cells,stdout)!=cells)return 2;
        for(i=0;i<256;++i){uint16_t v=collision.door_count[i];unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};if(fwrite(b,1,2,stdout)!=2)return 2;}
        if(!emit(motion.flags)||!emit(finishes)||!emit(scheduler._event_count)||!emit(last_reason))return 2;
    }
}

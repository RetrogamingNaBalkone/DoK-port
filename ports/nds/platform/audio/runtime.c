#include <nds.h>
#include <string.h>
#include "arm9.h"
#include "runtime.h"
#include "dok/audio_codec.h"

/* No pointer into the mutable world crosses IPC. Sound identity is a binding,
 * not a PCM resource; projectile instance generations have separate identities. */
_Alignas(32) volatile uint32_t g_nds_audio_probe[64];
static const dok_asset_session *session;
static const dok_level_presentation *presentation;
static unsigned char *cache,*fill;
typedef struct AudioResident {uint32_t pcm,rate,samples,bytes;unsigned char *packed;} AudioResident;
static AudioResident resident[22],*current;
static dok_asset_entry chunks[8];
static uint32_t chunk_count,position,samples,rate,looping;
static uint32_t stream_filling,stream_part,stream_bytes,packed_used,cold_max;
static uint32_t pending_id,pending_pcm,pending_loop,active_id,epoch,retired;
static uint32_t starts,rejects,stops,completed,requests,read_bytes,read_calls,service_max,fault;
static uint32_t catalog_index,catalog_wait,catalog_phase;
static const uint32_t indices[23]={86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,110};
static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void publish(void)
{
    NdsAudioStatus s=nds_audio_status();uint32_t a[64]={0x18504b44u,1,catalog_phase,catalog_index,
        s.playing,s.identity,s.consumed_parts,s.consumed_samples,s.underruns,s.stale_commands,
        s.command_ack,s.irq_count,s.timer_period,epoch,starts,rejects,stops,completed,requests,
        read_bytes,read_calls,service_max,fault,rate,samples,position,looping,pending_id,
        (uint32_t)(uintptr_t)cache,(uint32_t)(uintptr_t)fill,425984u,indices[catalog_index<23?catalog_index:22],cold_max,packed_used,stream_filling,stream_bytes,
        s.producer_samples,s.preparing,s.consumer_started};
    uint32_t sequence=g_nds_audio_probe[63]+1u;g_nds_audio_probe[63]=sequence;
    DC_FlushRange((const void *)&g_nds_audio_probe[56],32);
    memcpy((void *)g_nds_audio_probe,a,63u*4u);DC_FlushRange((const void *)g_nds_audio_probe,sizeof(a));
    g_nds_audio_probe[63]=sequence+1u;DC_FlushRange((const void *)&g_nds_audio_probe[56],32);
}
int nds_audio_runtime_init(const dok_asset_session *s,unsigned char *backing,uint32_t bytes,unsigned char *extra,uint32_t extra_bytes)
{
    if(!s||bytes<262144u||!extra||extra_bytes<163840u)return 0;
    session=s;cache=backing;fill=backing+65536;
    unsigned char *banks[2]={backing+98304,extra};uint32_t used[2]={0,0};
    unsigned order[22];dok_asset_view view;unsigned char descriptor[128];
    for(uint32_t i=0;i<22u;++i){
        AudioResident *r=&resident[i];r->pcm=(SPRITE_LANGUAGE<<30)|(indices[i]<<18)|(DOK_ASSET_PCM_CLIP<<10);
        if(dok_assets_load(s,r->pcm,descriptor,sizeof(descriptor),&view)!=DOK_ASSET_OK)return 0;
        r->rate=le32(descriptor);r->samples=le32(descriptor+4);
        if(le32(descriptor+8)!=1u||r->samples>65536u ||
           dok_assets_load(s,le32(descriptor+24),cache,65536,&view)!=DOK_ASSET_OK ||
           dok_audio_pack(cache,r->samples,fill,32768,&r->bytes)!=DOK_STATUS_OK)return 0;
        order[i]=i;packed_used+=r->bytes;
    }
    for(uint32_t i=0;i<22u;++i)for(uint32_t j=i+1;j<22u;++j)
        if(resident[order[j]].bytes>resident[order[i]].bytes){unsigned n=order[i];order[i]=order[j];order[j]=n;}
    for(uint32_t k=0;k<22u;++k){
        AudioResident *r=&resident[order[k]];uint32_t b=used[0]<=used[1]?0:1;
        if(used[b]+r->bytes>163840u)b^=1u;if(used[b]+r->bytes>163840u)return 0;
        r->packed=banks[b]+used[b];used[b]+=r->bytes;
        if(dok_assets_load(s,(r->pcm&~0x3fc00u)|(DOK_ASSET_PCM_CHUNK<<10),cache,65536,&view)!=DOK_ASSET_OK ||
           dok_audio_pack(cache,r->samples,r->packed,r->bytes,&r->bytes)!=DOK_STATUS_OK)return 0;
    }
    /* ARM7 borrows immutable compressed bytes. ARM9 never writes this bank
     * again and must not flush its PCM ring while the ARM7 producer owns it. */
    DC_FlushRange(banks[0],used[0]);DC_FlushRange(banks[1],used[1]);
    uint32_t title=(SPRITE_LANGUAGE<<30)|(110u<<18)|(DOK_ASSET_PCM_CLIP<<10);
    if(dok_assets_load(s,title,descriptor,sizeof(descriptor),&view)!=DOK_ASSET_OK)return 0;
    chunk_count=le32(descriptor+8);if(chunk_count!=8u)return 0;
    for(uint32_t i=0;i<chunk_count;++i){
        if(dok_assets_find(s,le32(descriptor+24+i*12),&chunks[i])!=DOK_ASSET_OK ||
           dok_assets_load(s,chunks[i].id,cache,65536,&view)!=DOK_ASSET_OK)return 0;
    }
    /* Every title chunk has been verified at the immutable package boundary.
     * Runtime reads bounded slices directly, without rescanning/hash stalls. */
    if(!nds_audio_init(backing,bytes))return 0;catalog_phase=1;publish();return 1;
}
int nds_audio_runtime_clear(void)
{int ok=1;if(session){ok=nds_audio_stop(0);++stops;}active_id=pending_id=stream_filling=0;presentation=NULL;publish();
 NdsAudioStatus s=nds_audio_status();return ok&&!s.playing&&!s.preparing&&!s.underruns&&!s.stale_commands;}
void nds_audio_runtime_world(const dok_level_presentation *p,uint32_t e)
{nds_audio_runtime_clear();presentation=p;epoch=e;publish();}
static uint32_t identity(dok_object_ref object,uint16_t slot)
{return object.object_id<=256u?(object.object_id<<16)|slot:0x40000000u|(object.object_id-256u)<<12|(object.generation&4095u);}
static void stop(uint32_t id)
{
    if(!id||pending_id==id)pending_id=0;
    if(active_id&&(!id||active_id==id)){(void)nds_audio_stop(id);active_id=0;++stops;}
}
static void request(uint32_t pcm,uint32_t id,uint32_t loop,uint32_t force)
{
    ++requests;if(force)stop(0);if(!pcm||!id)return;
    if(active_id&&!nds_audio_status().playing){active_id=0;++completed;}
    if(active_id||pending_id){++rejects;return;}
    pending_pcm=(pcm&~0x3fc00u)|(DOK_ASSET_PCM_CLIP<<10);pending_id=id;pending_loop=loop;
}
static void binding(dok_object_ref object,uint8_t glyph,uint16_t slot,uint32_t loop,uint32_t force)
{
    dok_presentation_value v;if(!presentation||object.world_epoch!=epoch)return;
    /* Native pickup/death global stop precedes even an empty binding. */
    if(force)stop(0);
    if(dok_presentation_get(presentation,glyph,slot,&v)==DOK_BALANCE_OK&&v.asset_id)
        request(v.asset_id,identity(object,slot),loop,0);
}
static uint8_t glyph(const dok_initial_world *w,dok_object_ref object)
{return object.object_id&&object.object_id<=w->object_count?w->definition->objects[object.object_id-1u].glyph:0;}
void nds_audio_runtime_ai(const dok_ai *s,const dok_ai_output *o)
{
    if(!presentation||o->object.world_epoch!=epoch)return;
    uint16_t slot=DOK_PRESENT_SOUND;uint32_t force=0;
    if(o->kind==DOK_AI_WAKE)slot=DOK_PRESENT_SOUND;
    else if(o->kind==DOK_AI_CONTACT||o->kind==DOK_AI_RANGED)slot=0xf0;
    else if(o->kind==DOK_AI_DYING){slot=0xf4;force=1;}
    else if(o->kind==DOK_AI_DEAD){stop(identity(o->object,0xf4));return;}
    else return;
    binding(o->object,glyph(s->world,o->object),slot,0,force);
}
void nds_audio_runtime_item(const dok_item_output *o)
{if(o->kind==DOK_ITEM_PICKUP&&presentation&&o->object.world_epoch==epoch)binding(o->object,presentation->effective->objects[o->object.object_id-1u].glyph,DOK_PRESENT_SOUND,0,1);}
void nds_audio_runtime_interaction(const dok_interaction_output *o)
{
    if(!presentation||o->object.world_epoch!=epoch)return;
    if(o->kind==DOK_INTERACTION_SOUND_STOP){stop(identity(o->object,DOK_PRESENT_SOUND));return;}
    if(o->kind==DOK_INTERACTION_SOUND_ENSURE||o->kind==DOK_INTERACTION_LOCKED)
        binding(o->object,presentation->effective->objects[o->object.object_id-1u].glyph,DOK_PRESENT_SOUND,o->kind==DOK_INTERACTION_SOUND_ENSURE,0);
}
void nds_audio_runtime_combat(const dok_combat *s,const dok_combat_output *o)
{
    if(!presentation||o->object.world_epoch!=epoch)return;
    if(o->kind==DOK_COMBAT_DAMAGE
#if !GAMEPLAY_FLOW
       ||o->kind==DOK_COMBAT_HERO_DEAD
#endif
      )
        binding(o->object,glyph(s->items->world,o->object),0xa6,0,0);
    else if(o->kind==DOK_COMBAT_SHOT&&o->source.object_id==s->items->world->hero.object_id){
        dok_object_ref weapon=s->items->inventory.weapons[s->items->inventory.selected];
        binding(weapon,glyph(s->items->world,weapon),DOK_PRESENT_SOUND,0,0);
    }else if(o->kind==DOK_COMBAT_IMPACT||o->kind==DOK_COMBAT_MISS)
        binding(o->object,(uint8_t)o->template_glyph,DOK_PRESENT_SOUND,0,0);
    else if(o->kind==DOK_COMBAT_REMOVED)stop(identity(o->object,DOK_PRESENT_SOUND));
}
void nds_audio_runtime_hero_death(dok_object_ref object,uint8_t actor_glyph)
{binding(object,actor_glyph,0xa6,0,1);}
static int source_read(unsigned char *out,uint32_t take)
{
    if(current)return 0; /* Resident PCM is produced only by ARM7. */
    uint32_t index=position/65536u,off=position%65536u,actual=0;
    if(index>=chunk_count||take>chunks[index].bytes-off)return 0;
    if(session->_reader.read_at(session->_reader.context,chunks[index].offset+off,out,take,&actual)!=DOK_ASSET_OK||actual!=take)return 0;
    read_bytes+=take;++read_calls;return 1;
}
static int fill_part(uint32_t part)
{
    uint32_t n=0;while(n<NDS_AUDIO_PART_BYTES){
        if(position==samples){if(!looping)break;position=0;}
        uint32_t take=NDS_AUDIO_PART_BYTES-n;if(take>samples-position)take=samples-position;
        if(!current&&take>65536u-position%65536u)take=65536u-position%65536u;
        if(!take||!source_read(fill+n,take))return 0;position+=take;n+=take;
    }
    return nds_audio_write(part,(const int8_t *)fill,n);
}
static int start_pending(void)
{
    /* Original RU/EN PCM/header pairs are all byte-identical. Banks remain
     * process-lifetime immutable across a localized presentation rebind. */
    current=NULL;for(uint32_t i=0;i<22u;++i)if((resident[i].pcm&0x3fffffffu)==(pending_pcm&0x3fffffffu)){current=&resident[i];break;}
    if(current){rate=current->rate;samples=current->samples;}
    else if(((pending_pcm>>18)&4095u)==110u){rate=11025;samples=474361;}
    else return 0;
    looping=pending_loop;position=retired=stream_filling=0;
    if(current){
        if(!nds_audio_resident(current->packed,current->bytes,samples,rate,looping,pending_id))return 0;
        active_id=pending_id;pending_id=0;++starts;return 1;
    }
    if(!fill_part(0)||!fill_part(1)||!nds_audio_begin(rate,samples,looping,pending_id))return 0;
    active_id=pending_id;pending_id=0;++starts;return 1;
}
int nds_audio_runtime_service(void)
{
    uint32_t start=cpuGetTiming();NdsAudioStatus s=nds_audio_status();
    if(active_id&&!s.playing){active_id=0;++completed;}
    if(pending_id){uint32_t cold=(pending_pcm>>18&4095u)==110u;
        if(!start_pending())fault=1;
        if(cold){uint32_t dt=cpuGetTiming()-start;if(dt>cold_max)cold_max=dt;start=cpuGetTiming();}}
    s=nds_audio_status();
    if(s.playing&&!current&&s.consumed_parts!=retired&&!stream_filling){
        if(s.consumed_parts-retired!=1u)fault=2;
        stream_filling=1;stream_part=(s.consumed_parts-1u)&1u;stream_bytes=0;
    }
    if(s.playing&&stream_filling){
        if(s.consumed_parts-retired!=1u)fault=2;
        if(position==samples&&looping)position=0;
        uint32_t take=512u;if(take>samples-position)take=samples-position;
        if(take>32768u-stream_bytes)take=32768u-stream_bytes;
        if(take>65536u-position%65536u)take=65536u-position%65536u;
        if(take&&!source_read(fill+stream_bytes,take))fault=2;
        position+=take;stream_bytes+=take;
        if(stream_bytes==32768u||(!looping&&position==samples)){
            if(!nds_audio_write(stream_part,(const int8_t *)fill,stream_bytes))fault=2;
            retired=s.consumed_parts;stream_filling=0;
        }
    }
    uint32_t elapsed=cpuGetTiming()-start;if(elapsed>service_max)service_max=elapsed;
    publish();return !fault&&!s.underruns&&!s.stale_commands;
}
int nds_audio_runtime_catalog(void)
{
    if(!nds_audio_runtime_service())return -1;
    if(catalog_phase==3u){
        if(nds_audio_status().consumed_samples>=4u*NDS_AUDIO_PART_BYTES){stop(0);catalog_phase=2;publish();return 1;}
        return 0;
    }
    if(active_id||pending_id)return 0;
    if(catalog_wait++<8)return 0;catalog_wait=0;
    if(catalog_index==23u){
        catalog_phase=3;
        request((SPRITE_LANGUAGE<<30)|(89u<<18)|(DOK_ASSET_PCM_CLIP<<10),0x80000059u,1,0);
        if(!nds_audio_runtime_service())return -1;
        /* Rejected once must not alter a playing loop; unrelated stop is a
         * legal identity no-op. Audio continues during the paused fixture. */
        request((SPRITE_LANGUAGE<<30)|(86u<<18)|(DOK_ASSET_PCM_CLIP<<10),0x80000056u,0,0);
        stop(0x80000056u);publish();return 0;
    }
    uint32_t id=(SPRITE_LANGUAGE<<30)|(indices[catalog_index]<<18)|(DOK_ASSET_PCM_CLIP<<10);
    request(id,0x80000000u|indices[catalog_index],0,0);++catalog_index;publish();return 0;
}
int nds_audio_runtime_title(void)
{request((SPRITE_LANGUAGE<<30)|(110u<<18)|(DOK_ASSET_PCM_CLIP<<10),0x8000006eu,0,0);return nds_audio_runtime_service();}

#include "dok/sprites.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c) do { ++checks;if(!(c)) { ++failures;fprintf(stderr,"sprites:%d\n",__LINE__); } } while(0)
static unsigned char metadata[56]={3,0,0,0,0,0,0,0,
    1,0,0,0,100,0,1,0,1,0,0,0,30,0,45,0,
    2,0,0,0,240,255,2,0,3,0,0,0,40,0,50,0,
    3,0,0,0,25,0,4,0,5,0,0,0,20,0,30,0};
typedef struct Test {
    dok_event_scheduler scheduler;dok_event_bank bank;
    dok_sprite_player player;
    uint32_t now,callbacks,draws;
} Test;
static Test a,b;
static dok_status clock_read(void *p,uint32_t *ticks)
{ *ticks=((Test *)p)->now;return DOK_STATUS_OK; }
static void callback(void *context,dok_event_scheduler *scheduler,dok_object_ref owner,uint16_t id,uint32_t parameter)
{
    Test *t=context;uint32_t interval,cancel;(void)owner;(void)id;(void)parameter;++t->callbacks;
    CHECK(dok_sprite_player_step(&t->player,&interval,&cancel)==DOK_STATUS_OK);
    CHECK(cancel?dok_events_cancel_current(scheduler)==DOK_STATUS_OK:
        dok_events_set_interval(scheduler,interval)==DOK_STATUS_OK);
}
static void timeline(Test *t,uint32_t draw_interval)
{
    dok_asset_view view={0};dok_event_config config={t,clock_read,t,callback,1};
    dok_object_ref owner={1,1,1};uint32_t i;
    view.data=metadata;view.entry.representation=DOK_ASSET_ANIM_META;view.entry.bytes=sizeof(metadata);
    CHECK(dok_sprite_animation_open(&view,&t->player.animation)==DOK_STATUS_OK);
    t->player.cursor=-1;t->player.remaining=80;
    CHECK(dok_events_init(&t->scheduler,&t->bank,&config)==DOK_STATUS_OK);
    CHECK(dok_events_register(&t->scheduler,owner,2)==DOK_STATUS_OK);
    CHECK(dok_events_schedule(&t->scheduler,owner,12,0,0)==DOK_STATUS_OK);
    for(i=0;i<12000u;++i) {
        dok_event_dispatch_report r;
        t->now=20000u+i;
        CHECK(dok_events_dispatch(&t->scheduler,&r)==DOK_STATUS_OK);
        if(i%draw_interval==0u) {
            dok_sprite_frame f;CHECK(dok_sprite_animation_get(&t->player.animation,t->player.cursor,&f)==DOK_STATUS_OK);
            ++t->draws;
        }
    }
}
int main(void)
{
    dok_sprite_animation out,old;dok_sprite_frame first,last,f;
    dok_asset_view view={0};uint32_t interval=123,cancel=456;dok_sprite_player player={0},backup;
    timeline(&a,167);timeline(&b,333);
    CHECK(a.draws>b.draws && a.callbacks==b.callbacks && a.callbacks==81);
    CHECK(memcmp(&a.player,&b.player,sizeof(a.player))==0);
    CHECK(!a.scheduler._event_count && !b.scheduler._event_count);
    CHECK(dok_sprite_animation_get(&a.player.animation,0,&first)==DOK_STATUS_OK);
    CHECK(dok_sprite_animation_get(&a.player.animation,-32768,&f)==DOK_STATUS_OK && memcmp(&f,&first,sizeof(f))==0);
    CHECK(dok_sprite_animation_get(&a.player.animation,2,&last)==DOK_STATUS_OK);
    CHECK(dok_sprite_animation_get(&a.player.animation,32767,&f)==DOK_STATUS_OK && memcmp(&f,&last,sizeof(f))==0);
    view.data=metadata;view.entry.representation=DOK_ASSET_ANIM_META;view.entry.bytes=sizeof(metadata)-1;
    memset(&out,0xa5,sizeof(out));old=out;
    CHECK(dok_sprite_animation_open(&view,&out)==DOK_STATUS_INVALID_STATE && memcmp(&out,&old,sizeof(out))==0);
    player.animation=a.player.animation;player.cursor=-2;player.remaining=1;backup=player;
    CHECK(dok_sprite_player_step(&player,&interval,&cancel)==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&player,&backup,sizeof(player))==0 && interval==123 && cancel==456);
    printf("{\"checks\":%u,\"failures\":%u,\"callbacks\":%lu,\"draws_60\":%lu,\"draws_30\":%lu}\n",
        checks,failures,(unsigned long)a.callbacks,(unsigned long)a.draws,(unsigned long)b.draws);
    return failures?1:0;
}

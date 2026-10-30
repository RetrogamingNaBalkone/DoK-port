#include "dok/frontend.h"
#include <string.h>

_Static_assert(sizeof(dok_frontend)<512,"frontend bounded state");

static uint8_t safe_language(uint8_t language)
{
    return language==2u?2u:1u;
}
int dok_frontend_text_index(const dok_frontend *s)
{
    if(s->state==DOK_FRONTEND_STORY)return 0;
    if(s->state==DOK_FRONTEND_INTRO)return 2*(int)s->level-1;
    if(s->state==DOK_FRONTEND_OUTRO)return 2*(int)s->level;
    if(s->state==DOK_FRONTEND_ENDING)return 7;
    return -1;
}
static void enter(dok_frontend *s,uint8_t state,uint8_t selection)
{
    s->state=state;s->selection=selection;s->page=0;s->page_count=1;
    int index=dok_frontend_text_index(s);
    if(index>=0){s->page_count=s->pages[s->language-1u][index];
        if(!s->page_count)s->page_count=1;}
    s->blocked=1;++s->revision;++s->transitions;
}
static void child(dok_frontend *s,uint8_t state)
{
    s->return_state=s->state;s->return_selection=s->selection;
    enter(s,state,state==DOK_FRONTEND_LANGUAGE?s->language-1u:0u);
}
static void back(dok_frontend *s)
{
    enter(s,s->return_state,s->return_selection);
}
void dok_frontend_init(dok_frontend *s,uint8_t language,
                       const uint16_t pages[2][8])
{
    memset(s,0,sizeof(*s));s->language=safe_language(language);s->level=1;
    if(pages)memcpy(s->pages,pages,sizeof(s->pages));
    s->state=DOK_FRONTEND_TITLE;s->page_count=1;s->revision=1;
}
int dok_frontend_game_input(const dok_frontend *s)
{
    return s->state==DOK_FRONTEND_PLAY&&!s->blocked;
}
static void move(dok_frontend *s,uint32_t pressed,uint8_t count)
{
    uint32_t direction=pressed&(DOK_FRONTEND_KEY_UP|DOK_FRONTEND_KEY_DOWN);
    if(direction==DOK_FRONTEND_KEY_UP){
        s->selection=s->selection?(uint8_t)(s->selection-1u):(uint8_t)(count-1u);
        ++s->revision;
    }else if(direction==DOK_FRONTEND_KEY_DOWN){
        s->selection=(uint8_t)((s->selection+1u)%count);++s->revision;
    }
}
uint32_t dok_frontend_step(dok_frontend *s,uint32_t keys,
                           uint8_t touch,uint8_t ready)
{
    keys&=DOK_FRONTEND_KEY_MASK;
    uint32_t pressed=keys&~s->previous_keys;s->previous_keys=keys;
    if(s->blocked){if(!keys&&!touch){s->blocked=0;s->previous_keys=0;}
        return 0;}
    if(s->state==DOK_FRONTEND_TITLE){
        if(pressed&(DOK_FRONTEND_KEY_A|DOK_FRONTEND_KEY_START))s->title_pending=1;
        if(ready&&s->title_pending){s->title_pending=0;
            enter(s,DOK_FRONTEND_MAIN,0);return DOK_FRONTEND_STOP_TITLE;}
        return 0;
    }
    if(s->state==DOK_FRONTEND_PLAY){
        if(pressed&DOK_FRONTEND_KEY_START){enter(s,DOK_FRONTEND_PAUSE,0);
            return DOK_FRONTEND_PAUSE_GAME;}
        return 0;
    }
    if(s->state==DOK_FRONTEND_EXIT)return 0;
    if(s->state==DOK_FRONTEND_PAUSE&&
       (pressed&(DOK_FRONTEND_KEY_START|DOK_FRONTEND_KEY_B))){
        enter(s,DOK_FRONTEND_PLAY,0);return DOK_FRONTEND_RESUME_GAME;
    }
    int text=dok_frontend_text_index(s);
    if(text>=0){
        if(pressed&DOK_FRONTEND_KEY_B){
            if(s->page){--s->page;++s->revision;}
            else if(s->state==DOK_FRONTEND_OUTRO||s->state==DOK_FRONTEND_ENDING){
                uint8_t outro=s->state==DOK_FRONTEND_OUTRO;
                enter(s,outro?DOK_FRONTEND_LEVELS:DOK_FRONTEND_MAIN,
                      outro?s->level-1u:0u);return DOK_FRONTEND_STOP_GAME;
            }else back(s);
        }else if(pressed&DOK_FRONTEND_KEY_A){
            if(s->page+1u<s->page_count){++s->page;++s->revision;}
            else if(s->state==DOK_FRONTEND_INTRO){
                enter(s,DOK_FRONTEND_PLAY,0);return DOK_FRONTEND_NEW_LEVEL;
            }else if(s->state==DOK_FRONTEND_OUTRO||s->state==DOK_FRONTEND_ENDING){
                uint8_t outro=s->state==DOK_FRONTEND_OUTRO;
                enter(s,outro?DOK_FRONTEND_LEVELS:DOK_FRONTEND_EXIT,
                      outro?s->level-1u:0u);return DOK_FRONTEND_STOP_GAME;
            }else back(s);
        }
        return 0;
    }
    if(s->state==DOK_FRONTEND_CONTROLS){
        if(pressed&(DOK_FRONTEND_KEY_A|DOK_FRONTEND_KEY_B))back(s);
        return 0;
    }
    if(s->state==DOK_FRONTEND_MAIN){
        move(s,pressed,5);
        if(pressed&DOK_FRONTEND_KEY_A){
            if(s->selection==0)enter(s,DOK_FRONTEND_LEVELS,s->level-1u);
            else if(s->selection==1)child(s,DOK_FRONTEND_LANGUAGE);
            else if(s->selection==2)child(s,DOK_FRONTEND_CONTROLS);
            else if(s->selection==3)child(s,DOK_FRONTEND_STORY);
            else enter(s,DOK_FRONTEND_ENDING,0);
        }
    }else if(s->state==DOK_FRONTEND_LEVELS){
        move(s,pressed,3);
        if(pressed&DOK_FRONTEND_KEY_B)enter(s,DOK_FRONTEND_MAIN,0);
        else if(pressed&DOK_FRONTEND_KEY_A){s->level=s->selection+1u;
            s->return_state=DOK_FRONTEND_LEVELS;s->return_selection=s->selection;
            enter(s,DOK_FRONTEND_INTRO,0);}
    }else if(s->state==DOK_FRONTEND_LANGUAGE){
        move(s,pressed,2);
        if(pressed&DOK_FRONTEND_KEY_B)back(s);
        else if(pressed&DOK_FRONTEND_KEY_A){
            uint8_t old=s->language;s->language=s->selection+1u;back(s);
            return old==s->language?0u:DOK_FRONTEND_LANGUAGE_CHANGED;
        }
    }else if(s->state==DOK_FRONTEND_PAUSE){
        move(s,pressed,4);
        if(pressed&DOK_FRONTEND_KEY_A){
            if(s->selection==0){enter(s,DOK_FRONTEND_PLAY,0);
                return DOK_FRONTEND_RESUME_GAME;}
            if(s->selection==1)child(s,DOK_FRONTEND_LANGUAGE);
            else if(s->selection==2)child(s,DOK_FRONTEND_CONTROLS);
            else {enter(s,DOK_FRONTEND_MAIN,0);return DOK_FRONTEND_STOP_GAME;}
        }
    }
    return 0;
}
static uint32_t show_fixture(dok_frontend *s,uint8_t state)
{
    uint32_t action=s->state==DOK_FRONTEND_PLAY?DOK_FRONTEND_PAUSE_GAME:0u;
    s->return_state=DOK_FRONTEND_MAIN;s->return_selection=0;
    enter(s,state,0);return action;
}
uint32_t dok_frontend_show_outro(dok_frontend *s,uint8_t level)
{
    s->level=level>=1u&&level<=3u?level:1u;
    return show_fixture(s,DOK_FRONTEND_OUTRO);
}
uint32_t dok_frontend_show_ending(dok_frontend *s)
{
    return show_fixture(s,DOK_FRONTEND_ENDING);
}
uint32_t dok_frontend_game_over(dok_frontend *s)
{
    uint32_t action=s->state==DOK_FRONTEND_PLAY?DOK_FRONTEND_PAUSE_GAME:0u;
    enter(s,DOK_FRONTEND_LEVELS,s->level-1u);
    return action|DOK_FRONTEND_STOP_GAME;
}
static uint32_t checksum(const uint8_t *p)
{
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<12u;++i)hash=(hash^p[i])*16777619u;
    return hash;
}
void dok_settings_encode(uint8_t out[DOK_SETTINGS_BYTES],uint8_t language)
{
    memset(out,0,DOK_SETTINGS_BYTES);memcpy(out,"DOKS",4);
    out[4]=1;out[6]=DOK_SETTINGS_BYTES;out[8]=safe_language(language);
    uint32_t sum=checksum(out);
    for(unsigned i=0;i<4u;++i)out[12u+i]=(uint8_t)(sum>>(8u*i));
}
int dok_settings_decode(const void *input,size_t bytes,uint8_t *language)
{
    const uint8_t *p=input;*language=1;
    if(!p||bytes!=DOK_SETTINGS_BYTES)return 0;
    if(memcmp(p,"DOKS",4)||p[4]!=1||p[5]||p[6]!=DOK_SETTINGS_BYTES||p[7]||
       (p[8]!=1u&&p[8]!=2u)||p[9]||p[10]||p[11])return 0;
    uint32_t sum=0;
    for(unsigned i=0;i<4u;++i)sum|=(uint32_t)p[12u+i]<<(8u*i);
    if(sum!=checksum(p))return 0;
    *language=p[8];return 1;
}

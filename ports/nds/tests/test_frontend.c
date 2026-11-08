#include "dok/frontend.h"
#include "dok/input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"frontend check failed line %d: %s\n",__LINE__,#x);exit(1); \
} } while(0)
static const uint16_t pages[2][8]={{3,2,4,3,2,4,3,5},{2,3,2,4,3,2,4,3}};
static void release(dok_frontend *s)
{
    CHECK(dok_frontend_step(s,0,0,1)==0);
}
static uint32_t press(dok_frontend *s,uint32_t key)
{
    release(s);return dok_frontend_step(s,key,0,1);
}
static void main_menu(dok_frontend *s,unsigned language)
{
    dok_frontend_init(s,(uint8_t)language,pages);
    CHECK(dok_frontend_step(s,DOK_FRONTEND_KEY_A,0,1)==DOK_FRONTEND_OPEN_MAIN);
    CHECK(s->state==DOK_FRONTEND_MAIN&&s->blocked);
    release(s);
}
static void choose(dok_frontend *s,unsigned selection)
{
    while(s->selection!=selection)CHECK(press(s,DOK_FRONTEND_KEY_DOWN)==0);
}
static void play(dok_frontend *s,unsigned language,unsigned level)
{
    main_menu(s,language);CHECK(press(s,DOK_FRONTEND_KEY_A)==0);
    CHECK(s->state==DOK_FRONTEND_LEVELS);choose(s,level-1u);
    CHECK(press(s,DOK_FRONTEND_KEY_A)==0);
    CHECK(s->state==DOK_FRONTEND_INTRO&&s->level==level);
    unsigned count=pages[language-1u][2u*level-1u];
    for(unsigned i=0;i<count;++i){
        CHECK(s->page==i&&s->page_count==count);
        CHECK(press(s,DOK_FRONTEND_KEY_A)==(i+1u==count?DOK_FRONTEND_NEW_LEVEL:0u));
    }
    CHECK(s->state==DOK_FRONTEND_PLAY&&!dok_frontend_game_input(s));
    release(s);CHECK(dok_frontend_game_input(s));
}
static void title_cases(void)
{
    for(unsigned key=0;key<2;++key)for(unsigned release_before_ready=0;release_before_ready<2;++release_before_ready){
        dok_frontend s;dok_frontend_init(&s,1,pages);
        uint32_t button=key?DOK_FRONTEND_KEY_START:DOK_FRONTEND_KEY_A;
        CHECK(dok_frontend_step(&s,button,0,0)==0);
        CHECK(s.state==DOK_FRONTEND_TITLE&&s.title_pending);
        if(release_before_ready)CHECK(dok_frontend_step(&s,0,0,0)==0);
        CHECK(dok_frontend_step(&s,release_before_ready?0:button,0,1)==DOK_FRONTEND_OPEN_MAIN);
        CHECK(s.state==DOK_FRONTEND_MAIN&&s.selection==0);
        for(unsigned i=0;i<30;++i)CHECK(dok_frontend_step(&s,button,0,1)==0);
        CHECK(s.state==DOK_FRONTEND_MAIN&&s.selection==0);
        release(&s);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        CHECK(s.state==DOK_FRONTEND_LEVELS);
    }
    dok_frontend s;dok_frontend_init(&s,99,0);
    CHECK(s.language==1&&s.state==DOK_FRONTEND_TITLE);
    for(unsigned i=0;i<100;++i)CHECK(dok_frontend_step(&s,0,0,1)==0);
    CHECK(s.state==DOK_FRONTEND_TITLE);
}
static void input_cases(void)
{
    /* Every native key combination plus stylus must release together. */
    for(unsigned held=1;held<=DOK_FRONTEND_KEY_MASK;++held){
        dok_frontend s;main_menu(&s,1);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);CHECK(s.state==DOK_FRONTEND_LEVELS);
        CHECK(dok_frontend_step(&s,held,1,1)==0&&s.blocked);
        CHECK(dok_frontend_step(&s,0,1,1)==0&&s.blocked);
        CHECK(dok_frontend_step(&s,held,0,1)==0&&s.blocked);
        CHECK(s.state==DOK_FRONTEND_LEVELS&&s.selection==0);
        release(&s);CHECK(!s.blocked);
        CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_DOWN,0,1)==0);
        CHECK(s.selection==1);
        for(unsigned i=0;i<10;++i)CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_DOWN,0,1)==0);
        CHECK(s.selection==1);
    }
    dok_frontend s;main_menu(&s,1);
    CHECK(press(&s,DOK_FRONTEND_KEY_UP)==0&&s.selection==4);
    CHECK(press(&s,DOK_FRONTEND_KEY_DOWN)==0&&s.selection==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_UP|DOK_FRONTEND_KEY_DOWN)==0&&s.selection==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.state==DOK_FRONTEND_MAIN);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.state==DOK_FRONTEND_MAIN);
    choose(&s,4);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
    CHECK(s.state==DOK_FRONTEND_ENDING&&dok_frontend_text_index(&s)==7);
    for(unsigned page=0;page<1u;++page){
        CHECK(s.page==page&&s.page_count==1u);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==
              (page+1u==1u?DOK_FRONTEND_STOP_GAME:0u));
    }
    CHECK(s.state==DOK_FRONTEND_MAIN);
    release(&s);CHECK(press(&s,DOK_FRONTEND_KEY_START)==0&&s.state==DOK_FRONTEND_MAIN);
}
static void menus_and_text(void)
{
    for(unsigned language=1;language<=2;++language){
        dok_frontend s;main_menu(&s,language);choose(&s,1);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.state==DOK_FRONTEND_LANGUAGE);
        CHECK(s.selection==language-1u);choose(&s,2u-language);
        CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.state==DOK_FRONTEND_MAIN);
        CHECK(s.language==language&&s.selection==1);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);choose(&s,2u-language);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_LANGUAGE_CHANGED);
        CHECK(s.language==3u-language&&s.state==DOK_FRONTEND_MAIN&&s.selection==1);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0); /* same choice: no write requested */
        CHECK(s.state==DOK_FRONTEND_MAIN);
        choose(&s,2);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        CHECK(s.state==DOK_FRONTEND_CONTROLS&&dok_frontend_text_index(&s)==-1);
        CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.selection==2&&s.state==DOK_FRONTEND_MAIN);
        choose(&s,3);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        CHECK(s.state==DOK_FRONTEND_STORY&&dok_frontend_text_index(&s)==0);
        unsigned count=pages[s.language-1u][0];
        for(unsigned page=0;page<count;++page){
            CHECK(s.page==page&&s.page_count==count);
            CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        }
        CHECK(s.state==DOK_FRONTEND_MAIN&&s.selection==3);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.page==1);
        CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.page==0);
        CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.state==DOK_FRONTEND_MAIN);
        for(unsigned level=1;level<=3;++level){
            play(&s,language,level);
            CHECK(press(&s,DOK_FRONTEND_KEY_START)==DOK_FRONTEND_PAUSE_GAME);
            CHECK(s.state==DOK_FRONTEND_PAUSE&&!dok_frontend_game_input(&s));
            CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_START,1,1)==0);
            CHECK(s.state==DOK_FRONTEND_PAUSE);
            release(&s);choose(&s,1);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
            CHECK(s.state==DOK_FRONTEND_LANGUAGE);choose(&s,2u-language);
            CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_LANGUAGE_CHANGED);
            CHECK(s.state==DOK_FRONTEND_PAUSE&&s.selection==1);
            choose(&s,2);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
            CHECK(s.state==DOK_FRONTEND_CONTROLS);
            CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.state==DOK_FRONTEND_PAUSE&&s.selection==2);
            CHECK(press(&s,DOK_FRONTEND_KEY_START)==DOK_FRONTEND_RESUME_GAME);
            CHECK(s.state==DOK_FRONTEND_PLAY);release(&s);
            CHECK(dok_frontend_show_outro(&s,(uint8_t)level)==DOK_FRONTEND_PAUSE_GAME);
            CHECK(s.state==DOK_FRONTEND_OUTRO&&dok_frontend_text_index(&s)==(int)(2u*level));
            count=pages[s.language-1u][2u*level];
            for(unsigned page=0;page<count;++page){
                CHECK(s.page==page&&s.page_count==count);
                CHECK(press(&s,DOK_FRONTEND_KEY_A)==(page+1u==count?DOK_FRONTEND_STOP_GAME:0u));
            }
            CHECK(s.state==DOK_FRONTEND_LEVELS&&s.selection==level-1u);
            play(&s,language,level);
            CHECK(press(&s,DOK_FRONTEND_KEY_START)==DOK_FRONTEND_PAUSE_GAME);
            CHECK(dok_frontend_show_ending(&s)==0); /* no double pause */
            CHECK(s.state==DOK_FRONTEND_ENDING&&dok_frontend_text_index(&s)==7);
            count=1;
            for(unsigned page=0;page<count;++page){
                CHECK(s.page==page&&s.page_count==count);
                CHECK(press(&s,DOK_FRONTEND_KEY_A)==(page+1u==count?DOK_FRONTEND_STOP_GAME:0u));
            }
            CHECK(s.state==DOK_FRONTEND_MAIN);
        }
    }
    dok_frontend s;play(&s,1,1);
    CHECK(press(&s,DOK_FRONTEND_KEY_START)==DOK_FRONTEND_PAUSE_GAME);
    CHECK(press(&s,DOK_FRONTEND_KEY_B)==DOK_FRONTEND_RESUME_GAME);
    CHECK(press(&s,DOK_FRONTEND_KEY_START)==DOK_FRONTEND_PAUSE_GAME);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_RESUME_GAME);
    CHECK(press(&s,DOK_FRONTEND_KEY_START)==DOK_FRONTEND_PAUSE_GAME);
    choose(&s,3);CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_STOP_GAME);
    CHECK(s.state==DOK_FRONTEND_MAIN);
    play(&s,1,2);CHECK(dok_frontend_show_outro(&s,0)==DOK_FRONTEND_PAUSE_GAME);
    CHECK(s.level==1);CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.state==DOK_FRONTEND_OUTRO);
    CHECK(dok_frontend_show_ending(&s)==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_B)==DOK_FRONTEND_STOP_GAME&&s.state==DOK_FRONTEND_MAIN);
    main_menu(&s,1);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_DOWN)==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.level==2);
    CHECK(press(&s,DOK_FRONTEND_KEY_B)==DOK_FRONTEND_STOP_GAME&&s.state==DOK_FRONTEND_MAIN&&s.selection==0);
    dok_frontend_init(&s,1,0);CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_OPEN_MAIN);
    choose(&s,3);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.page_count==1);
}
static void scheme_cases(void)
{
    const uint32_t physical[]={DOK_BUTTON_UP,DOK_BUTTON_DOWN,DOK_BUTTON_LEFT,DOK_BUTTON_RIGHT,
        DOK_BUTTON_L,DOK_BUTTON_R,DOK_BUTTON_Y,DOK_BUTTON_B,DOK_BUTTON_X,DOK_BUTTON_A,DOK_BUTTON_START};
    const uint32_t modern[]={DOK_BUTTON_UP,DOK_BUTTON_DOWN,DOK_BUTTON_Y,DOK_BUTTON_A,
        DOK_BUTTON_LEFT,DOK_BUTTON_RIGHT,DOK_BUTTON_R,DOK_BUTTON_L,DOK_BUTTON_X,DOK_BUTTON_SELECT,DOK_BUTTON_START};
    for(unsigned keys=0;keys<4096u;++keys){
        uint32_t expected=0;
        for(unsigned i=0;i<11u;++i)if(keys&physical[i])expected|=modern[i];
        CHECK(dok_frontend_game_buttons(keys,0)==((keys&~(DOK_BUTTON_B|DOK_BUTTON_SELECT))|
            ((keys&DOK_BUTTON_B)?DOK_BUTTON_SELECT:0u)));
        CHECK(dok_frontend_game_buttons(keys,1)==expected);
    }
    dok_frontend s;main_menu(&s,1);choose(&s,2);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.selection==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_DOWN)==0&&s.selection==1);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_CONTROLS_CHANGED&&s.control_scheme==1);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.selection==1);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.control_scheme==1);
    CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_UP)==0&&s.selection==0);
    CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.control_scheme==1);
}
static void fps_cases(void)
{
    for(unsigned scheme=0;scheme<2u;++scheme){
        dok_frontend s;play(&s,1,1);s.control_scheme=(uint8_t)scheme;
        CHECK(s.show_fps==1);
        CHECK(press(&s,DOK_BUTTON_SELECT)==DOK_FRONTEND_FPS_CHANGED&&!s.show_fps);
        for(unsigned n=0;n<30u;++n)CHECK(dok_frontend_step(&s,DOK_BUTTON_SELECT,0,1)==0&&!s.show_fps);
        CHECK(press(&s,DOK_BUTTON_SELECT)==DOK_FRONTEND_FPS_CHANGED&&s.show_fps);
        CHECK(press(&s,DOK_BUTTON_START|DOK_BUTTON_SELECT)==DOK_FRONTEND_PAUSE_GAME&&s.show_fps);
        CHECK(press(&s,DOK_BUTTON_SELECT)==0&&s.show_fps);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==DOK_FRONTEND_RESUME_GAME);
        CHECK(dok_frontend_step(&s,DOK_BUTTON_SELECT,0,1)==0&&s.show_fps);
        release(&s);
        CHECK(press(&s,DOK_BUTTON_SELECT)==DOK_FRONTEND_FPS_CHANGED&&!s.show_fps);
        CHECK(dok_frontend_show_death(&s,2)==DOK_FRONTEND_PAUSE_GAME);
        CHECK(press(&s,DOK_BUTTON_SELECT)==0&&!s.show_fps);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==(DOK_FRONTEND_RETRY_GAME|DOK_FRONTEND_RESUME_GAME));
        CHECK(!s.show_fps);
        release(&s);
        uint32_t weapon=scheme?DOK_BUTTON_A:DOK_BUTTON_B;
        CHECK(dok_frontend_game_buttons(weapon,(uint8_t)scheme)==DOK_BUTTON_SELECT);
        CHECK(dok_frontend_game_buttons(DOK_BUTTON_SELECT,(uint8_t)scheme)==0);
        CHECK(dok_frontend_game_buttons(DOK_BUTTON_X,(uint8_t)scheme)==DOK_BUTTON_X);
    }
}
static void settings_cases(void)
{
    /* Version 2: language and control scheme covered by FNV-1a. */
    const uint8_t golden[16]={'D','O','K','S',2,0,16,0,2,0,0,0,0xca,0xe0,0x23,0x29};
    uint8_t out[17],language=99,scheme=99;
    dok_settings_encode(out,2,0);CHECK(!memcmp(out,golden,16));
    CHECK(dok_settings_decode(golden,16,&language,&scheme)&&language==2);
    for(unsigned lang=0;lang<=255;++lang){
        dok_settings_encode(out,(uint8_t)lang,0);
        CHECK(dok_settings_decode(out,16,&language,&scheme));CHECK(language==(lang==2?2u:1u));
        for(unsigned i=0;i<16;++i)for(unsigned bit=0;bit<8;++bit){
            out[i]^=(uint8_t)(1u<<bit);language=99;
            CHECK(!dok_settings_decode(out,16,&language,&scheme)&&language==1);
            out[i]^=(uint8_t)(1u<<bit);
        }
    }
    for(unsigned bytes=0;bytes<=17;++bytes)if(bytes!=16){
        language=99;CHECK(!dok_settings_decode(out,bytes,&language,&scheme)&&language==1);
    }
    /* Recompute checksums after semantic corruption: a valid hash must not
     * permit a future version, extra flags, invalid language or other magic. */
    for(unsigned offset=0;offset<12;++offset)for(unsigned value=0;value<256;++value){
        memcpy(out,golden,16);out[offset]=(uint8_t)value;
        uint32_t hash=2166136261u;
        for(unsigned i=0;i<12;++i)hash=(hash^out[i])*16777619u;
        for(unsigned i=0;i<4;++i)out[12u+i]=(uint8_t)(hash>>(8u*i));
        int valid=value==golden[offset]||(offset==8u&&value==1u)||(offset==9u&&value==1u);
        CHECK(dok_settings_decode(out,16,&language,&scheme)==valid);
        CHECK(language==(valid?out[8]:1u));
    }
    CHECK(!dok_settings_decode(0,16,&language,&scheme)&&language==1&&scheme==0);
    for(unsigned control=0;control<2u;++control)for(unsigned lang=1;lang<=2u;++lang){
        dok_settings_encode(out,(uint8_t)lang,(uint8_t)control);
        CHECK(dok_settings_decode(out,16,&language,&scheme)&&language==lang&&scheme==control);
    }
}
static void death_cases(void)
{
    for(unsigned lang=1;lang<=2u;++lang)for(int attempts=0;attempts<=2;++attempts)
    for(unsigned key=0;key<2u;++key){
        dok_frontend s;play(&s,(uint8_t)lang,2);
        CHECK(dok_frontend_show_death(&s,(int16_t)attempts)==
              (DOK_FRONTEND_PAUSE_GAME|(attempts?0u:DOK_FRONTEND_STOP_GAME)));
        CHECK(s.state==DOK_FRONTEND_DEATH&&!dok_frontend_game_input(&s));
        CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_A,0,1)==0&&s.state==DOK_FRONTEND_DEATH);
        CHECK(dok_frontend_step(&s,0,1,1)==0&&s.blocked);
        CHECK(dok_frontend_step(&s,0,0,1)==0&&!s.blocked);
        CHECK(dok_frontend_step(&s,key?DOK_FRONTEND_KEY_START:DOK_FRONTEND_KEY_A,0,1)==
              (attempts?(DOK_FRONTEND_RETRY_GAME|DOK_FRONTEND_RESUME_GAME):0u));
        CHECK(s.state==(attempts?DOK_FRONTEND_PLAY:DOK_FRONTEND_LEVELS)&&s.level==2&&s.blocked);
        CHECK(!dok_frontend_game_input(&s));
        if(attempts){
            CHECK(dok_frontend_show_death(&s,(int16_t)attempts)==DOK_FRONTEND_PAUSE_GAME);
            CHECK(press(&s,DOK_FRONTEND_KEY_B)==DOK_FRONTEND_STOP_GAME);
            CHECK(s.state==DOK_FRONTEND_LEVELS);
        }
    }
}
int main(void)
{
    {unsigned char text[]="  Forces of evil,\r\n  mutant bats.\r\n\r\n\r\nThe digger.\r\n";
     unsigned n=dok_frontend_text_prepare(text,sizeof(text),0);
     const char *expected="Forces of evil, mutant bats.\n\nThe digger.";
     CHECK(n==strlen(expected)&&!memcmp(text,expected,n));}
    {unsigned char text[]="Completed\r\n\r\nwith honor!\r\n\r\n\r\nNow safe.";
     unsigned n=dok_frontend_text_prepare(text,sizeof(text),6);
     const char *expected="Completed with honor!\n\nNow safe.";
     CHECK(n==strlen(expected)&&!memcmp(text,expected,n));}
    {dok_frontend s;dok_frontend_init(&s,1,pages);
     CHECK(press(&s,DOK_FRONTEND_KEY_B)==DOK_FRONTEND_OPEN_MAIN&&s.state==DOK_FRONTEND_MAIN);}
    for(unsigned language=1;language<=2;++language)for(unsigned level=1;level<=3;++level){
        dok_frontend s;main_menu(&s,language);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        choose(&s,level-1u);CHECK(press(&s,DOK_FRONTEND_KEY_A)==0);
        CHECK(press(&s,DOK_FRONTEND_KEY_A)==0&&s.page==1);
        CHECK(press(&s,DOK_FRONTEND_KEY_B)==0&&s.state==DOK_FRONTEND_INTRO&&s.page==0);
        CHECK(press(&s,DOK_FRONTEND_KEY_B)==DOK_FRONTEND_STOP_GAME&&s.state==DOK_FRONTEND_MAIN);
        CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_B,0,1)==0&&s.state==DOK_FRONTEND_MAIN);
    }
    title_cases();input_cases();menus_and_text();scheme_cases();settings_cases();death_cases();fps_cases();
    printf("frontend checks=%u state_bytes=%u\n",checks,(unsigned)sizeof(dok_frontend));
    return 0;
}

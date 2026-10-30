/* Production N21 shell. FINISH/death campaign consumption remains N22.
 * FRONTEND_FIXTURES alone exposes explicit real-button EINTRO/ENDTXT hooks. */
static void frontend_game_probe(void)
{
    uint32_t p[16]={0};
    if(world_serial){
        p[0]=storage->effects.clock._ticks;p[1]=storage->game.hero_callbacks;
        p[2]=storage->game.ai.callbacks;p[3]=storage->game.combat.callbacks;
        p[4]=world_serial;p[5]=world->initial.epoch;
        p[6]=(uint32_t)storage->game.hero.x;p[7]=(uint32_t)storage->game.hero.z;
        p[8]=(uint32_t)(int32_t)storage->game.items.inventory.health;
        p[9]=(uint32_t)(int32_t)storage->game.items.inventory.charge;
        p[10]=storage->game.items.inventory.keys;p[11]=storage->game.items.inventory.selected;
        p[12]=address(ui->language_before);p[13]=address(ui->language_after);
        p[14]=address(ui->simulation_before);p[15]=address(ui->simulation_after);
        /* These four immutable-since-bind hashes are flushed at their write
         * boundary. Publishing their addresses does not dirty the UI bank. */
    }
    nds_frontend_publish(frontend,p);publish();
}
static int frontend_actions(uint32_t action)
{
    frontend->actions=action;
    if(action&DOK_FRONTEND_PAUSE_GAME){
        if(dok_world_clock_pause(&storage->effects.clock)!=DOK_STATUS_OK)return 0;
        map_ui->paused=1;++map_ui->pauses;
        storage->game.input_held=0;
        if(dok_controls_initialize(&storage->game.controls)!=DOK_STATUS_OK)return 0;
        dok_map_control_step(&map_ui->control,(dok_map_touch){0,0,0,1},
            (int16_t)(storage->game.hero.x/480),(int16_t)(storage->game.hero.z/480));
    }
    if(action&DOK_FRONTEND_RESUME_GAME){
        if(dok_world_clock_resume(&storage->effects.clock)!=DOK_STATUS_OK)return 0;
        map_ui->paused=0;++map_ui->resumes;
    }
    if(action&DOK_FRONTEND_LANGUAGE_CHANGED){
        if(world_serial&&!nds_ui_language(ui,world,scratch,frontend->state.language))return 0;
        /* Failed writes never roll back the user's in-memory selection. */
        (void)nds_frontend_settings_write(frontend);
    }
    if(action&(DOK_FRONTEND_STOP_GAME|DOK_FRONTEND_STOP_TITLE))nds_audio_runtime_clear();
    return 1;
}
static void frontend_restore_game(void)
{
    bgShow(0);ui->dirty=1;ui->overlay_blank=0;frontend->drawn_revision=0;
    frontend->visual_flushed_asset=0;frontend->visual_flushed_pixels=0;frontend->visual_flushed_bytes=0;
    nds_map_invalidate(map_ui);
    for(uint32_t n=0;n<256u;++n){BG_PALETTE[n]=storage->effects.palette[n];
        const unsigned char *rgb=storage->effects.rgb6+n*3u;
        BG_PALETTE_SUB[n]=RGB15(rgb[0]>>1,rgb[1]>>1,rgb[2]>>1);}
    /* Next accepted presentation reuploads the entire lower map and dirty HUD.
     * Reset interval accounting so UI reading time is not called gameplay FPS. */
    previous_present_ticks=0;previous_present_vblank=vblank_counter;
}
static int frontend_loop(void)
{
    dok_render_case play={0};uint32_t ordinal=0,shown=0,new_level=0;
    if(!nds_frontend_initialize(frontend,&session,scratch,ui->bank))return 0;
    bgHide(0);
    if(!nds_frontend_draw(frontend,bgGetGfxPtr(hud_bg),bgGetGfxPtr(map_bg))||!nds_audio_runtime_title())return 0;
    frontend->ready=1;
    if(!frontend_actions(dok_frontend_step(&frontend->state,0,0,1)))return 0;
    obs.status=NDS_RENDER_RUNNING;
    for(;;){
        if(!nds_audio_runtime_service())return 0;
        scanKeys();uint32_t keys=keysHeld()&4095u,touching=(keysHeld()&KEY_TOUCH)!=0;
        uint32_t edge_keys=keys|nds_frontend_take_edges();
        touchPosition pen={0};if(touching)touchRead(&pen);
        uint32_t before=frontend->state.state,revision=frontend->state.revision,action=0;
#if FRONTEND_FIXTURES
        uint32_t visual_chord=(keys&(KEY_SELECT|KEY_Y))==(KEY_SELECT|KEY_Y);
        if(before==DOK_FRONTEND_PLAY&&!frontend->state.blocked&&visual_chord&&!frontend->visual_chord)
            frontend->visual_fixture=!frontend->visual_fixture;
        frontend->visual_chord=visual_chord;
        /* Chords take priority over ordinary Start and can never toggle twice. */
        if(before==DOK_FRONTEND_PLAY&&!frontend->state.blocked&&
           (keys&(KEY_SELECT|KEY_START))==(KEY_SELECT|KEY_START))
            action=dok_frontend_show_outro(&frontend->state,frontend->state.level);
        else if(before==DOK_FRONTEND_PLAY&&!frontend->state.blocked&&
           (keys&(KEY_SELECT|KEY_X))==(KEY_SELECT|KEY_X))
            action=dok_frontend_show_ending(&frontend->state);
        else
#endif
            action=dok_frontend_step(&frontend->state,edge_keys,(uint8_t)touching,1);
        /* A published PLAY descriptor never exposes the borrowed pixel buffers.
         * Keep it readable during the next heavy render preparation. Only a
         * transition from a displayed frontend frame invalidates that frame. */
        if(frontend->state.revision!=revision){
            if(!(g_nds_frontend_probe[2]&1u))++g_nds_frontend_probe[2];
            DC_FlushRange((void *)g_nds_frontend_probe,32);
        }
        if(!frontend_actions(action))return 0;
        if(before!=DOK_FRONTEND_STORY&&frontend->state.state==DOK_FRONTEND_STORY){
            if(!nds_audio_runtime_title())return 0;
        }else if(before==DOK_FRONTEND_STORY&&frontend->state.state!=DOK_FRONTEND_STORY)nds_audio_runtime_clear();
        if(action&DOK_FRONTEND_NEW_LEVEL)new_level=1;
        if(frontend->state.state==DOK_FRONTEND_PLAY){
            if(new_level){
                if(scene->leased&&(dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK))return 0;
                if(!level_load(frontend->state.level))return 0;
                if(ui->language!=frontend->state.language&&!nds_ui_language(ui,world,scratch,frontend->state.language))return 0;
                play=(dok_render_case){0};play.id=1;play.level=frontend->state.level;play.source_kind=22;
                play.x=storage->game.hero.x;play.y=storage->game.hero.y;play.z=storage->game.hero.z;play.heading=storage->game.hero.heading;
                storage->game.case_kind=22;
                if(dok_world_clock_resume(&storage->effects.clock)!=DOK_STATUS_OK)return 0;
                map_ui->paused=0;new_level=0;ordinal=0;shown=0;
                if(!memory_check())return 0;
            }
            if(!shown){frontend_restore_game();shown=1;}
            frontend_keys=dok_frontend_game_input(&frontend->state)?keys&~KEY_START:0;
#if FRONTEND_FIXTURES
            if(frontend->visual_chord)frontend_keys&=~(KEY_SELECT|KEY_Y);
#endif
            frontend_touch=dok_frontend_game_input(&frontend->state)?touching:0;
            frontend_x=(int16_t)pen.px;frontend_y=(int16_t)pen.py;
            if(!render_one(&play,ordinal++))return 0;
        }else{
            shown=0;bgHide(0);
            if(!nds_frontend_draw(frontend,bgGetGfxPtr(hud_bg),bgGetGfxPtr(map_bg)))return 0;
            swiWaitForVBlank();
        }
        ++frontend->frames;frontend_game_probe();
    }
}

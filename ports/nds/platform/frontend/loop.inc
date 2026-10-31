/* Production shell. N22 consumes FINISH/death only after complete dispatch.
 * FRONTEND_FIXTURES alone exposes explicit real-button EINTRO/ENDTXT hooks. */
#if GAMEPLAY_FLOW
_Alignas(32) volatile uint32_t g_nds_flow_probe[32];
static uint32_t flow_result;
static void frontend_flow_probe(void)
{
    NdsGameplay *g=&storage->game;
    uint32_t p[32]={0x22504b44u,1,g_nds_flow_probe[2]|1u,world_active,world_serial,
        frontend->state.level,(uint32_t)(int32_t)g->items.inventory.health,
        (uint32_t)(int32_t)g->lives,(uint32_t)g->hero.x,(uint32_t)g->hero.y,
        (uint32_t)g->hero.z,(uint32_t)(int32_t)g->hero.heading,g->finish_pending,
        g->deaths,g->retries,g->hero_callbacks,g->ai.callbacks,g->combat.callbacks,
        storage->effects.scheduler._event_count,world->initial.object_count,
        world_active?address(world->initial.maps[DOK_WORLD_MMAP]):0,
        world_active?address(world->initial.maps[DOK_WORLD_TMAP]):0,
        world_active?address(world->initial.maps[DOK_WORLD_FCMAP]):0,
        world_active?address(world->initial.maps[DOK_WORLD_VISITED]):0,
        g->items.inventory.keys,g->items.inventory.charge,flow_result,
        storage->effects.scheduler._dispatching,g->collision._active!=NULL,
        storage->effects.scheduler._config.world_epoch,obs.presented_frame_id,frontend->frames};
    g_nds_flow_probe[2]=p[2];DC_FlushRange((void *)g_nds_flow_probe,32);
    memcpy((void *)g_nds_flow_probe,p,sizeof(p));DC_FlushRange((void *)g_nds_flow_probe,sizeof(p));
    ++g_nds_flow_probe[2];DC_FlushRange((void *)g_nds_flow_probe,32);
}
static int frontend_stop_world(void)
{
    if(!world_active)return nds_audio_runtime_clear();
    if(storage->effects.scheduler._dispatching||storage->game.collision._active)return GAMEPLAY_FAILURE();
    if(scene->leased&&dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK)return GAMEPLAY_FAILURE();
    if(!nds_audio_runtime_clear()||dok_combat_clear(&storage->game.combat)!=DOK_STATUS_OK||
       dok_ai_clear(&storage->game.ai)!=DOK_STATUS_OK||
       dok_events_reset(&storage->effects.scheduler,world_serial+1u)!=DOK_STATUS_OK)return GAMEPLAY_FAILURE();
    storage->effects.game=NULL;storage->effects.simulation_list=NULL;
    storage->effects.world=NULL;storage->effects.sprites=NULL;
    memset(&storage->game,0,sizeof(storage->game));memset(&storage->list,0,sizeof(storage->list));
    memset(demand,0,sizeof(demand));demand_count=0;stream_ready=0;mask_identity_count=0;
    frontend->visual_descriptor_bytes=0;frontend->visual_asset=0;frontend->visual_pixels=0;
    frontend->visual_bytes=0;frontend->visual_references=0;
    ui->message_kind=0;ui->message_bytes=0;ui->message_address=0;ui->dirty=1;
    nds_map_begin();map_ui->prepared=0;memset(&map_ui->snapshot,0,sizeof(map_ui->snapshot));
#if N23_STATIC_GEOMETRY
    dok_static_geometry_reset();
#endif
    world_active=0;return memory_check();
}
#endif
static void frontend_game_probe(void)
{
    uint32_t p[16]={0};
    if(world_active){
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
#if GAMEPLAY_FLOW
    frontend_flow_probe();
#endif
}
static int frontend_actions(uint32_t action)
{
    frontend->actions=action;
    if(action&DOK_FRONTEND_PAUSE_GAME){
        if(dok_world_clock_pause(&storage->effects.clock)!=DOK_STATUS_OK)return GAMEPLAY_FAILURE();
        map_ui->paused=1;++map_ui->pauses;
        storage->game.input_held=0;
        if(dok_controls_initialize(&storage->game.controls)!=DOK_STATUS_OK)return GAMEPLAY_FAILURE();
        dok_map_control_step(&map_ui->control,(dok_map_touch){0,0,0,1},
            (int16_t)(storage->game.hero.x/480),(int16_t)(storage->game.hero.z/480));
    }
    if(action&DOK_FRONTEND_RESUME_GAME){
        if(dok_world_clock_resume(&storage->effects.clock)!=DOK_STATUS_OK)return GAMEPLAY_FAILURE();
        map_ui->paused=0;++map_ui->resumes;
    }
    if(action&DOK_FRONTEND_LANGUAGE_CHANGED){
        if(world_active&&!nds_ui_language(ui,world,scratch,frontend->state.language))return GAMEPLAY_FAILURE();
        /* Failed writes never roll back the user's in-memory selection. */
        (void)nds_frontend_settings_write(frontend);
    }
    if(action&DOK_FRONTEND_STOP_TITLE)if(!nds_audio_runtime_clear())return GAMEPLAY_FAILURE();
    if(action&DOK_FRONTEND_STOP_GAME){
#if GAMEPLAY_FLOW
        if(!frontend_stop_world())return GAMEPLAY_FAILURE();
#else
        nds_audio_runtime_clear();
#endif
    }
    return 1;
}
static void frontend_restore_game(void)
{
    bgShow(0);ui->dirty=1;ui->overlay_blank=0;ui->overlay_valid=0;frontend->drawn_revision=0;
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
    if(!nds_frontend_initialize(frontend,&session,scratch,ui->bank))return GAMEPLAY_FAILURE();
    bgHide(0);
    if(!nds_frontend_draw(frontend,bgGetGfxPtr(hud_bg),bgGetGfxPtr(map_bg))||!nds_audio_runtime_title())return GAMEPLAY_FAILURE();
    frontend->ready=1;
    if(!frontend_actions(dok_frontend_step(&frontend->state,0,0,1)))return GAMEPLAY_FAILURE();
    obs.status=NDS_RENDER_RUNNING;
    for(;;){
        if(!nds_audio_runtime_service())return GAMEPLAY_FAILURE();
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
        if(!frontend_actions(action))return GAMEPLAY_FAILURE();
        if(before!=DOK_FRONTEND_STORY&&frontend->state.state==DOK_FRONTEND_STORY){
            if(!nds_audio_runtime_title())return GAMEPLAY_FAILURE();
        }else if(before==DOK_FRONTEND_STORY&&frontend->state.state!=DOK_FRONTEND_STORY)nds_audio_runtime_clear();
        if(action&DOK_FRONTEND_NEW_LEVEL)new_level=1;
        if(frontend->state.state==DOK_FRONTEND_PLAY){
            if(new_level){
                if(scene->leased&&(dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK))return GAMEPLAY_FAILURE();
                if(!level_load(frontend->state.level))return GAMEPLAY_FAILURE();
#if GAMEPLAY_FLOW
                flow_result=0;
#endif
                if(ui->language!=frontend->state.language&&!nds_ui_language(ui,world,scratch,frontend->state.language))return GAMEPLAY_FAILURE();
                play=(dok_render_case){0};play.id=1;play.level=frontend->state.level;play.source_kind=22;
                play.x=storage->game.hero.x;play.y=storage->game.hero.y;play.z=storage->game.hero.z;play.heading=storage->game.hero.heading;
                storage->game.case_kind=22;
                if(dok_world_clock_resume(&storage->effects.clock)!=DOK_STATUS_OK)return GAMEPLAY_FAILURE();
                map_ui->paused=0;new_level=0;ordinal=0;shown=0;
                if(!memory_check())return GAMEPLAY_FAILURE();
            }
            if(!shown){frontend_restore_game();shown=1;}
            frontend_keys=dok_frontend_game_input(&frontend->state)?keys&~KEY_START:0;
#if FRONTEND_FIXTURES
            if(frontend->visual_chord)frontend_keys&=~(KEY_SELECT|KEY_Y);
#endif
            frontend_touch=dok_frontend_game_input(&frontend->state)?touching:0;
            frontend_x=(int16_t)pen.px;frontend_y=(int16_t)pen.py;
            if(!render_one(&play,ordinal++))return GAMEPLAY_FAILURE();
#if GAMEPLAY_FLOW
            /* Dispatch/output sinks have returned. Render-one has completed
             * the display boundary, so teardown cannot invalidate a borrower. */
            NdsGameplay *g=&storage->game;
            if(g->finish_pending)g->items.inventory.health=-1;
            flow_result=dok_gameflow_check(g->items.inventory.health,&g->lives);
            if(flow_result==DOK_FLOW_RETRY||flow_result==DOK_FLOW_GAMEOVER){
                ++g->deaths;
                const dok_world_object_seed *h=&world->initial.objects[world->initial.hero.object_id-1u];
                nds_audio_runtime_hero_death(world->initial.hero,world->effective.objects[h->definition_index].glyph);
                if(!nds_audio_runtime_service())return GAMEPLAY_FAILURE();
                ui->lives=g->lives;ui->message_kind=0;ui->dirty=1;
            }
            if(flow_result==DOK_FLOW_RETRY){
                if(scene->leased&&dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK)return GAMEPLAY_FAILURE();
                if(nds_gameplay_retry(g)!=DOK_STATUS_OK)return GAMEPLAY_FAILURE();
                play.x=g->hero.x;play.y=g->hero.y;play.z=g->hero.z;play.heading=g->hero.heading;
            }else if(flow_result==DOK_FLOW_FINISH||flow_result==DOK_FLOW_GAMEOVER){
                uint32_t transition=flow_result==DOK_FLOW_FINISH?
                    dok_frontend_show_outro(&frontend->state,frontend->state.level)|DOK_FRONTEND_STOP_GAME:
                    dok_frontend_game_over(&frontend->state);
                if(!frontend_actions(transition))return GAMEPLAY_FAILURE();
                /* Publish a non-PLAY descriptor only after its own complete
                 * buffers/palettes exist. PLAY scratch was borrowed by the
                 * renderer; it cannot masquerade as a fresh OUTRO/LEVELS. */
                shown=0;bgHide(0);
                if(!nds_frontend_draw(frontend,bgGetGfxPtr(hud_bg),bgGetGfxPtr(map_bg)))return GAMEPLAY_FAILURE();
                swiWaitForVBlank();
            }
#endif
        }else{
            shown=0;bgHide(0);
            if(!nds_frontend_draw(frontend,bgGetGfxPtr(hud_bg),bgGetGfxPtr(map_bg)))return GAMEPLAY_FAILURE();
            swiWaitForVBlank();
        }
        ++frontend->frames;frontend_game_probe();
    }
}

#ifndef DOK_NDS_FRONTEND_H
#define DOK_NDS_FRONTEND_H
#include <stddef.h>
#include <stdint.h>

enum {
    DOK_FRONTEND_TITLE=1, DOK_FRONTEND_MAIN, DOK_FRONTEND_LEVELS,
    DOK_FRONTEND_LANGUAGE, DOK_FRONTEND_CONTROLS, DOK_FRONTEND_STORY,
    DOK_FRONTEND_INTRO, DOK_FRONTEND_PLAY, DOK_FRONTEND_PAUSE,
    DOK_FRONTEND_OUTRO, DOK_FRONTEND_ENDING, DOK_FRONTEND_EXIT
};
enum {
    DOK_FRONTEND_PAUSE_GAME=1u, DOK_FRONTEND_RESUME_GAME=2u,
    DOK_FRONTEND_LANGUAGE_CHANGED=4u, DOK_FRONTEND_NEW_LEVEL=8u,
    DOK_FRONTEND_STOP_GAME=16u, DOK_FRONTEND_STOP_TITLE=32u
};
enum {
    DOK_FRONTEND_KEY_A=1u, DOK_FRONTEND_KEY_B=2u,
    DOK_FRONTEND_KEY_START=8u, DOK_FRONTEND_KEY_UP=64u,
    DOK_FRONTEND_KEY_DOWN=128u, DOK_FRONTEND_KEY_MASK=4095u,
    DOK_SETTINGS_BYTES=16
};

/* Portable UI controller. World, time, audio, drawing and FAT belong to callers.
 * pages[language-1]: README, BINTRO1, EINTRO1, BINTRO2, EINTRO2,
 * BINTRO3, EINTRO3, ENDTXT. Zero page counts safely become one. */
typedef struct dok_frontend {
    uint16_t pages[2][8];
    uint16_t page,page_count;
    uint32_t previous_keys,revision,transitions;
    uint8_t state,selection,language,level;
    uint8_t return_state,return_selection,blocked,title_pending;
} dok_frontend;

void dok_frontend_init(dok_frontend *,uint8_t language,
                       const uint16_t pages[2][8]);
uint32_t dok_frontend_step(dok_frontend *,uint32_t keys,
                           uint8_t touch_held,uint8_t title_ready);
int dok_frontend_text_index(const dok_frontend *);
int dok_frontend_game_input(const dok_frontend *);
/* N21 explicit text fixtures; campaign FINISH/death remains N22. OUTRO returns
 * to LEVELS and ENDING completes to EXIT, with STOP_GAME. B on ENDING page0
 * cancels to MAIN. PAUSE_GAME is needed only if
 * called while PLAY; paused callers must not pause their clock twice. */
uint32_t dok_frontend_show_outro(dok_frontend *,uint8_t level);
uint32_t dok_frontend_show_ending(dok_frontend *);

/* Exact little-endian 16-byte settings-only record. Decode always writes a
 * safe language (RU=1); its return value says whether the input was valid. */
void dok_settings_encode(uint8_t output[DOK_SETTINGS_BYTES],uint8_t language);
int dok_settings_decode(const void *input,size_t bytes,uint8_t *language);
#endif

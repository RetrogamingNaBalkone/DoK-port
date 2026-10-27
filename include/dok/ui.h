#ifndef DOK_UI_H
#define DOK_UI_H
#include "dok/runtime.h"
#include "dok/image.h"
/* Borrowed ROM images, validated once at UI startup. Panel is original;
 * icons are reduced offline to12x8/8x8/10x12, never resized while drawing. */
typedef struct {dok_image_view panel,health,energy,keys[3],splash,background;
 const uint8_t *splash_palette,*menu_palette,*splash_frame,*menu_frame;} dok_ui_art;
void dok_ui_set_art(const dok_ui_art *);
/* Presentation only. No mutable world pointer, allocator, native far pointer,
 * or dependency on GBA video. CP866 glyphs use8 columns; the message row
 * condenses them horizontally when needed to fit the panel. */
#define DOK_UI_TEXT_LIMIT 240u
enum {DOK_UI_PLAY,DOK_UI_TITLE,DOK_UI_PAUSE,DOK_UI_HELP,DOK_UI_STATUS,DOK_UI_SPLASH,DOK_UI_LEVELS,DOK_UI_MAP};
enum {DOK_UI_RESUME=1,DOK_UI_NEW_GAME,DOK_UI_CREDITS};
typedef struct {uint16_t object,slot;} dok_ui_message;
typedef struct {
 uint32_t state,focus,language,last_keys,blocked,return_state;
 uint32_t action,start_level;
 uint32_t messages,length,page,pages,deadline,active,errors;
 dok_ui_message queue[4];uint32_t read,count;
 uint16_t object,slot;
 uint8_t literal[DOK_UI_TEXT_LIMIT];uint16_t literal_size;
} dok_ui;
void dok_ui_screen(uint8_t *,const uint8_t *,const uint8_t *,unsigned,unsigned,const uint8_t *,size_t,unsigned,const char *,unsigned);
unsigned dok_ui_screen_pages(const uint8_t *,size_t);
void dok_ui_init(dok_ui *);
/* GBA map discovery, using the already reserved visited plane. */
void dok_ui_explore(dok_runtime *);
unsigned dok_ui_frontend(const dok_ui *);
const uint8_t *dok_ui_palette(const dok_ui *);
/* Returns1 only on a requested presentation language change. */
unsigned dok_ui_input(dok_ui *,uint16_t keys);
uint16_t dok_ui_game_keys(dok_ui *,uint16_t keys);
void dok_ui_notify(dok_ui *,uint16_t object,uint16_t slot,uint32_t now);
void dok_ui_literal(dok_ui *,const uint8_t *,size_t,uint32_t now);
void dok_ui_tick(dok_ui *,const dok_runtime_level *,uint32_t now);
/* Resolve selected localization every draw, preserving message identity. */
size_t dok_ui_text(dok_ui *,const dok_runtime_level *,uint8_t out[60]);
/* Draw directly into a 240x160 indexed framebuffer, bounded at every edge.
 * Original FONT8=2048/FONT16=4096 bytes; foreground/background palette IDs. */
void dok_ui_draw(dok_ui *,uint8_t *frame,const dok_runtime *,const uint8_t *font8,
 const uint8_t *font16,unsigned white,unsigned black);
#endif

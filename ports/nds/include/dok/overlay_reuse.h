#ifndef DOK_OVERLAY_REUSE_H
#define DOK_OVERLAY_REUSE_H
#include "dok/weapon_overlay.h"
/* A retained VRAM image, never a borrowed scratch/raw-cache pointer.
 * Source content is immutable for the N04 asset ID within one world serial.
 * Explicit comparison avoids padding and conservatively includes all fields. */
typedef struct dok_overlay_key {
    dok_weapon_view view;
    uint32_t serial,bytes;
    uint16_t pitch;
} dok_overlay_key;
static inline int dok_overlay_key_equal(const dok_overlay_key *a,const dok_overlay_key *b)
{
    return a->serial==b->serial&&a->bytes==b->bytes&&a->pitch==b->pitch&&
        a->view.visible==b->view.visible&&a->view.left==b->view.left&&a->view.top==b->view.top&&
        a->view.frame.image==b->view.frame.image&&a->view.frame.width==b->view.frame.width&&
        a->view.frame.height==b->view.frame.height&&a->view.frame.interval==b->view.frame.interval&&
        a->view.frame.parameter_a==b->view.frame.parameter_a&&a->view.frame.parameter_b==b->view.frame.parameter_b;
}
#endif

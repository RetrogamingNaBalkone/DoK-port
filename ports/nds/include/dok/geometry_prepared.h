#ifndef DOK_GEOMETRY_PREPARED_H
#define DOK_GEOMETRY_PREPARED_H
#include "dok/geometry.h"
/* Internal, transient metadata for one immutable read boundary. Slot identity
 * is definition.objects index, never the object's independently stored glyph. */
typedef struct dok_geometry_field54 {
    int16_t value;
    uint16_t is_word;
} dok_geometry_field54;
_Static_assert(sizeof(dok_geometry_field54)==4u,"one KiB at maximum object count");
static inline void dok_geometry_prepare_fields(const dok_level_definition *level,
    dok_geometry_field54 prepared[DOK_LEVEL_MAX_OBJECTS])
{
    uint32_t i;
    if(level->language!=1u || !level->object_count || level->object_count>DOK_LEVEL_MAX_OBJECTS ||
        level->settings[DOK_LEVEL_WIDTH]<=0 || level->settings[DOK_LEVEL_LENGTH]<=0 ||
        level->settings[DOK_LEVEL_WIDTH]>64 || level->settings[DOK_LEVEL_LENGTH]>64 ||
        level->settings[DOK_LEVEL_CELL_SIDE]<=0 || level->settings[DOK_LEVEL_CELL_HEIGHT]<=0)return;
    for(i=0;i<level->object_count;++i) {
        const dok_level_field *field=dok_level_field_at(&level->objects[i],0x54u);
        uint32_t value=field?(uint32_t)field->value&65535u:0u;
        prepared[i].value=(int16_t)(value<32768u?(int32_t)value:(int32_t)value-65536);
        prepared[i].is_word=(uint16_t)(field && field->kind==DOK_LEVEL_WORD);
    }
}
#endif

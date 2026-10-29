#include "dok/interactions_move.h"
#include <stddef.h>
dok_status dok_interactions_move(void *context,dok_object_ref actor,
    dok_motion_state *motion,int32_t x,int32_t z)
{
    dok_collision_report report;
    if(context==NULL||motion==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    return dok_collision_displace(context,actor,motion,x,z,&report);
}

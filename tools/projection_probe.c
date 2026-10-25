#include "dok/projection.h"
#include <inttypes.h>
#include <stdio.h>

/* Stream interface keeps independent Python vectors outside this C model. */
int main(void)
{
    dok_projection_camera camera;
    int32_t scale,focal,cx,cy,x,y,z;
    while (scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32
        " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32
        " %" SCNd32 " %" SCNd32 " %" SCNd32,
        &camera.x,&camera.y,&camera.z,&camera.sine,&camera.cosine,&camera.distance,
        &scale,&focal,&cx,&cy,&x,&y,&z) == 13) {
        dok_camera_point point;
        dok_screen_point screen={123,456,789};
        int valid;
        if (scale < -32768 || scale > 32767 || focal < -32768 || focal > 32767 ||
            cx < -32768 || cx > 32767 || cy < -32768 || cy > 32767) return 2;
        camera.scale=(int16_t)scale;camera.focal=(int16_t)focal;
        camera.center_x=(int16_t)cx;camera.center_y=(int16_t)cy;
        dok_projection_transform(&camera,x,y,z,&point);
        valid=dok_projection_project(&camera,&point,&screen);
        printf("%" PRId32 " %" PRId32 " %" PRId32 " %d %" PRId32 " %" PRId32 " %" PRId32 "\n",
            point.lateral,point.depth,point.vertical,valid,screen.x,screen.y,screen.factor);
    }
    return ferror(stdin) ? 1 : 0;
}

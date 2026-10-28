#ifndef DOK_CELL_COORDINATE_H
#define DOK_CELL_COORDINATE_H
#include <stdint.h>
#ifdef DOK_RENDERER_PROBE
extern const uint8_t dok_cell_480[1920];
#endif
/* Exact signed division followed by native map-edge clamp. The common GBA
 * map uses floor((n>>4)/30), indexed offline, for nonnegative n<30720. */
static inline int32_t dok_cell_coordinate(int32_t n,int32_t side,int32_t limit){
#ifdef DOK_RENDERER_PROBE
 if(side==480 && limit==64){
  if(n<0)return 0;if(n>=30720)return 63;
  return dok_cell_480[(uint32_t)n>>4];
 }
#endif
 int32_t q=n/side;
 if(q<0)q=0;if(q>=limit)q=limit-1;return q;
}
#endif

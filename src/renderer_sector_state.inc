/* Sector clip pipeline. No per-pixel depth/owner buffer; histories describe
 * opaque ceiling/portal boundaries for the deferred masked pass. */
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
#include "dok/gba_sector_gpu.h"
#endif
typedef struct {uint16_t fixed;uint8_t axis,flags;int16_t child[2];uint16_t first,count,bounds[4];} sector_node;
typedef struct {uint16_t first,count,bounds[4];int16_t ceiling;uint16_t reserved;} sector_leaf;
typedef struct {uint16_t ax,az,bx,bz;int16_t y,height,v0;uint16_t object,cell;uint8_t kind,side;} sector_atom;
typedef struct {const sector_node *nodes;const sector_leaf *leaves;const sector_atom *atoms;const uint16_t *ids,*cells;uint16_t nodes_count,leaves_count;} sector_view;
typedef struct {uint16_t near,far;} sector_history;
typedef struct {int32_t ox,oz,dx,dz,ix,iz;} sector_ray;
typedef struct {
    sector_ray ray[120];
    uint32_t roof[120][4],closed[4];
    uint16_t wall_z[120],ceiling_depth[128];
    uint8_t top[120],counts[120],wall_first[120],wall_last[120],history_top[120][64];
    int dx0,dx1,dq0,dq1,dx,dx_start;
} sector_state;
_Static_assert(sizeof(sector_node)==20 && sizeof(sector_leaf)==16 && sizeof(sector_atom)==20 && sizeof(sector_view)==24,"sector ROM ABI");
_Static_assert(sizeof(sector_state)+3<=sizeof(((dok_renderer_workspace *)0)->covered),"sector state scratch");
_Static_assert(120*64*sizeof(sector_history)<=sizeof(((dok_renderer_workspace *)0)->surfaces),"sector history scratch");
extern const sector_view dok_sector_views[6];
static sector_state *sector;
static sector_history (*sector_history_rows)[64];
static const dok_projection_camera *sector_camera;
static const sector_atom *sector_current_atom;
static unsigned sector_active,sector_emitting,sector_dynamic,sector_ceiling_object;
volatile uint32_t g_sector_work[12]; /* active,nodes,culled,static segments,wall columns,rim columns,roof ranges,history max,dynamic columns,roof pixels,dynamic pixels,capacity */
volatile uint32_t g_sector_ticks[5]; /* optional validation/rays,static visibility+walls,shared ceiling,dynamics,reserved */
static void sector_static_face(dok_renderer_workspace *,const dok_renderer_face *,dok_renderer_stats *);
static unsigned sector_dynamic_column(dok_renderer_workspace *,unsigned,unsigned,unsigned,const uint8_t *,unsigned,unsigned,unsigned);

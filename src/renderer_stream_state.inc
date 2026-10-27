/* Native streaming BSP prototype state. Workspace scratch is shared with
 * legacy geometry only while this renderer is active, never with IRQ code. */
typedef struct {uint16_t fixed;uint8_t axis,reserved;int16_t child[2];uint16_t first,count,bounds[4];} stream_node;
typedef struct {uint16_t first,count,bounds[4];int16_t ceiling;uint16_t reserved;} stream_leaf;
typedef struct {uint16_t ax,az,bx,bz;int16_t y,height,v0;uint16_t object,cell;uint8_t kind,side;} stream_atom;
typedef struct {
    const stream_node *nodes;const stream_leaf *leaves;const stream_atom *atoms;
    const uint16_t *ids,*cells;uint16_t node_count,leaf_count;
} stream_view;
typedef struct {uint16_t face,next;uint8_t first,last;uint16_t reserved;} stream_fragment;
_Static_assert(sizeof(stream_node)==20 && sizeof(stream_leaf)==16 && sizeof(stream_atom)==20,"stream ROM geometry ABI");
_Static_assert(sizeof(stream_view)==24 && sizeof(stream_fragment)==8,"stream view/scratch ABI");
_Static_assert(10240+3<=sizeof(((dok_renderer_workspace *)0)->covered),"stream aligned scratch capacity");
_Static_assert(1024+4096*sizeof(stream_fragment)<=sizeof(((dok_renderer_workspace *)0)->surfaces),"stream fragment capacity");
extern const stream_view dok_stream_views[6];
static unsigned stream_active,stream_emitting,stream_clip_first,stream_clip_last,stream_vertical_clip;
static int stream_ceiling;
static const dok_runtime *stream_runtime;
static const dok_projection_camera *stream_camera;
static int32_t (*stream_xy)[4];
static uint16_t *stream_heads;
static uint8_t *stream_present;
static stream_fragment *stream_fragments;
static unsigned stream_fragment_count;
volatile uint32_t g_stream_work[8]; /* active,visited,culled,atoms,leaves,dynamic fragments,fallbacks,capacity */
volatile uint32_t g_stream_ticks[6]; /* when render_work[0]:validation,partition,box tests,dynamics,static atoms,ceilings */
static void raster(dok_renderer_workspace *,const dok_runtime *,const dok_renderer_face *,const uint8_t *,dok_renderer_stats *);
static void stream_emit_face(dok_renderer_workspace *,const dok_renderer_face *,dok_renderer_stats *);
static void stream_record(unsigned,int,int,int,int,int,int);

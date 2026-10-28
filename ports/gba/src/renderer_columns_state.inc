/* Full column visibility prototype. All large buffers borrow aligned workspace
 * scratch; no gameplay state or memory limits are changed. */
typedef struct {
    dok_renderer_face f;int q0,q1,span;const dok_renderer_image *image;
    unsigned v,step;int u,remainder,u_step,u_carry;
    union {struct {uint8_t first,last;} rect;int16_t height;} range;
    uint8_t row,rectangle;int16_t crop_x,crop_y;
} column_face;
typedef struct {
    const uint8_t *pixels,*lut;unsigned v,step;
    uint16_t depth,stride;uint8_t first,last,masked,reserved;
} column_event;
_Static_assert(sizeof(column_face)==96 && sizeof(column_event)==24,"column scratch ABI");
_Static_assert(256*sizeof(column_face)+384*sizeof(column_event)<=sizeof(((dok_renderer_workspace *)0)->surfaces),"column event capacity");
_Static_assert(2176+256*48+3<=sizeof(((dok_renderer_workspace *)0)->covered),"column ceiling/edge scratch capacity");
_Static_assert(16384+640+384*sizeof(column_event)+3<=sizeof(((dok_renderer_workspace *)0)->covered),"column diagnostic snapshot capacity");
typedef struct {unsigned quotient,remainder,step,carry,divisor;int sign;} column_edge_state;
static column_edge_state (*column_edges)[2];
static unsigned column_active,column_ceiling_object;
static column_face *column_faces;
static column_event *column_events;
static uint32_t (*column_ceiling_mask)[4];
static uint16_t *column_ceiling_depth;
volatile uint32_t g_column_work[8]; /* active,rays,map steps,events,alpha tests,vertical pixels,ceiling pixels,capacity */
volatile uint32_t g_column_ticks[4]; /* when render_work[0]:prepare,visibility,vertical output,ceiling output */
volatile uint32_t g_column_probe[4]; /* request x+1 (0 off),capture count,event count,x */

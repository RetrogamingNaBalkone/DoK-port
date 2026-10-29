#include "dok/collision.h"
#include "collision_routes.h"
#include "collision_probe.h"
_Static_assert(sizeof(dok_collision_routes_workspace)<=DOK_NDS_SCRATCH_BYTES,"N10 routes scratch");
_Static_assert(sizeof(dok_collision_state)<2048u,"N10 bounded runtime table");
_Static_assert(sizeof(NdsCollisionProbe)==344u,"N10 probe ABI v1");
_Static_assert(sizeof(NdsCollisionTraceRecord)==352u,"N10 trace ABI v1");
_Static_assert(sizeof(NdsCollisionSnapshot)==17728u,"N10 snapshot ABI");
int dok_collision_header_compile(void)
{ return (int)(sizeof(dok_collision_plan)+sizeof(dok_collision_context)); }

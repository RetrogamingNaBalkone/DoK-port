#include "dok/geometry.h"
#include "dok/render_packet.h"
#include "dok/render_scene.h"
#include "dok/render_materials.h"
#include "dok/render_fixture.h"

_Static_assert(sizeof(dok_nds_render_camera)==20u,"fixed-width camera");
_Static_assert(sizeof(dok_nds_render_vertex)==20u,"fixed-width local vertex");
_Static_assert(sizeof(dok_nds_render_quad)==92u,"fixed-width diagnostic quad");
_Static_assert(sizeof(dok_render_case)==64u,"fixture ABI record");
_Static_assert(sizeof(dok_render_scene)<=DOK_NDS_SCENE_BYTES,"scene budget");
_Static_assert(sizeof(dok_render_world)<=DOK_NDS_WORLD_BYTES,"world budget");
_Static_assert(sizeof(dok_render_workspace)<=DOK_NDS_SCRATCH_BYTES,"scratch budget");

int dok_render_public_headers_compile(void)
{
    return DOK_RENDER_FIXTURE_HEADER==64u && DOK_RENDER_FIXTURE_LEVEL==10240u;
}

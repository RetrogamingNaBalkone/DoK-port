#include "dok/interactions.h"
#include "dok/interactions_move.h"
#include "dok/door_render.h"
#include "interaction_cases.h"
#include "door_render_cases.h"
_Static_assert(sizeof(dok_interactions_state)<4096u,"bounded interaction runtime");
_Static_assert(sizeof(dok_interaction_phase_view)<2048u,"bounded phase view");
int dok_interactions_compile_check(void) { return (int)sizeof(dok_interaction_door); }

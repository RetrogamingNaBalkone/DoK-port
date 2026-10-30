#ifndef DOK_NDS_HOTCODE_H
#define DOK_NDS_HOTCODE_H
/* Placement only. Other ports/host tools keep their ordinary code sections. */
#if defined(N21P_ITCM) && N21P_ITCM
#include <nds/ndstypes.h>
#define DOK_NDS_HOT_CODE ITCM_CODE
#else
#define DOK_NDS_HOT_CODE
#endif
#endif

#ifndef DOK_NDS_RESOURCE_PATHS_H
#define DOK_NDS_RESOURCE_PATHS_H

/* Immutable cartridge resources; historical diagnostic FAT fixtures remain
 * selectable. Settings always use separate, optional writable storage. */
#ifndef NDS_EMBEDDED_RESOURCES
#define NDS_EMBEDDED_RESOURCES 0
#endif
#if NDS_EMBEDDED_RESOURCES
#define NDS_RESOURCE_ROOT "nitro:/dok/"
#else
#define NDS_RESOURCE_ROOT "fat:/dok/"
#endif

#endif

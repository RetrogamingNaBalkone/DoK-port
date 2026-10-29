#ifndef DOK_NDS_PLATFORM_CLOCK_H
#define DOK_NDS_PLATFORM_CLOCK_H

#include "dok/contracts.h"

/* Start the SDK's shared timer2 counter once, then return N03 services.
 * Repeated calls return the same continuing counter, never restart it.
 * Caller must not reconfigure timer2 or mask IRQs for a full overflow period.
 * The service and its unused NULL context live until program termination. */
dok_status dok_nds_clock_initialize(dok_platform_services *out_services);

#endif

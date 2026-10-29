#ifndef DOK_NDS_PLATFORM_INPUT_H
#define DOK_NDS_PLATFORM_INPUT_H

#include "dok/input.h"

/* Main-thread adapter. Caller initializes the continuing N08 clock first and
 * lends its N03 services/context until the adapter is discarded. This adapter
 * never configures a timer or calls keysDownRepeat/keysCurrent. Poll frequently
 * enough to observe physical taps; transitions occurring entirely between two
 * hardware polls cannot be recovered by software. */
typedef struct dok_nds_input {
    dok_platform_services _platform;
    uint64_t _sequence;
    uint64_t _counter;
    uint32_t _held;
    uint32_t _touch;
    uint32_t _tag;
    uint32_t _has_counter;
} dok_nds_input;

dok_status dok_nds_input_initialize(dok_nds_input *input,
                                    const dok_platform_services *platform);
/* scanKeys snapshots all ARM9/ARM7 keys, then touch is read and counter sampled.
 * Source sequence begins at1; held keys on the first sample are pressed.
 * Success advances adapter; failure leaves adapter/out unchanged. After a
 * successful poll caller must retain the sample until capture succeeds, or
 * stop with an explicit runtime fault; do not poll over an overflowed sample.
 * Only 12 physical buttons and lower-screen touch enter the raw N03 frame. */
dok_status dok_nds_input_poll(dok_nds_input *input, dok_input_sample *out);

#endif

#include "dok/input.h"
#include "input_cases.h"

_Static_assert(DOK_INPUT_SAMPLE_LIMIT == 64, "accepted ordered input capacity");
_Static_assert(sizeof(dok_input_buffer) <= 4096u, "bounded input storage");
_Static_assert(DOK_INPUT_NATIVE_TURN_POS == 4u && DOK_INPUT_NATIVE_TURN_NEG == 8u,
               "source turn directions");
_Static_assert((DOK_INPUT_NATIVE_FORWARD | DOK_INPUT_NATIVE_BACKWARD) == 0x30u,
               "opposite movement bits remain independent");

int dok_input_header_compile(void)
{
    return (int)(sizeof(dok_input_buffer) + sizeof(dok_input_test_report));
}

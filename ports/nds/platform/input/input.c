/* SDK boundary only. Independent buffering/mapping live in core/input.c. */
#include <nds.h>
#include <stddef.h>
#include "input.h"

#define NDS_INPUT_TAG UINT32_C(0x49303950)

_Static_assert((uint32_t)KEY_A == DOK_BUTTON_A && (uint32_t)KEY_B == DOK_BUTTON_B &&
               (uint32_t)KEY_SELECT == DOK_BUTTON_SELECT &&
               (uint32_t)KEY_START == DOK_BUTTON_START &&
               (uint32_t)KEY_RIGHT == DOK_BUTTON_RIGHT &&
               (uint32_t)KEY_LEFT == DOK_BUTTON_LEFT &&
               (uint32_t)KEY_UP == DOK_BUTTON_UP && (uint32_t)KEY_DOWN == DOK_BUTTON_DOWN &&
               (uint32_t)KEY_R == DOK_BUTTON_R && (uint32_t)KEY_L == DOK_BUTTON_L &&
               (uint32_t)KEY_X == DOK_BUTTON_X && (uint32_t)KEY_Y == DOK_BUTTON_Y,
               "Re-audit all NDS keypad bits after SDK change");

dok_status dok_nds_input_initialize(dok_nds_input *input,
                                    const dok_platform_services *platform)
{
    dok_nds_input initialized = {0};
    if (input == NULL || platform == NULL || platform->read_counter == NULL ||
        platform->counter_hz_numerator == 0u ||
        platform->counter_hz_denominator == 0u)
        return DOK_STATUS_INVALID_ARGUMENT;
    initialized._platform = *platform;
    initialized._tag = NDS_INPUT_TAG;
    *input = initialized;
    return DOK_STATUS_OK;
}

dok_status dok_nds_input_poll(dok_nds_input *input, dok_input_sample *out)
{
    dok_input_sample sample = {0};
    touchPosition touch;
    uint32_t keys;
    uint32_t touch_held;
    if (input == NULL || out == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    if (input->_tag != NDS_INPUT_TAG) return DOK_STATUS_INVALID_STATE;
    if (input->_sequence == UINT64_MAX) return DOK_STATUS_LIMIT_REACHED;
    scanKeys();
    keys = keysHeld();
    sample.frame.sequence = input->_sequence + 1u;
    sample.frame.held = keys & DOK_BUTTON_MASK;
    sample.frame.pressed = sample.frame.held & ~input->_held;
    sample.frame.released = input->_held & ~sample.frame.held;
    touch_held = (keys & KEY_TOUCH) != 0u;
    if (touch_held != 0u) {
        touchRead(&touch);
        sample.frame.touch_flags = DOK_TOUCH_HELD;
        if (input->_touch == 0u) sample.frame.touch_flags |= DOK_TOUCH_PRESSED;
        sample.frame.touch_x = (int16_t)touch.px;
        sample.frame.touch_y = (int16_t)touch.py;
    } else if (input->_touch != 0u) {
        sample.frame.touch_flags = DOK_TOUCH_RELEASED;
    }
    sample.counter = input->_platform.read_counter(input->_platform.context);
    if (dok_input_frame_validate(&sample.frame) != DOK_STATUS_OK ||
        (input->_has_counter != 0u && sample.counter < input->_counter))
        return DOK_STATUS_INVALID_STATE;
    input->_sequence = sample.frame.sequence;
    input->_held = sample.frame.held;
    input->_touch = touch_held;
    input->_counter = sample.counter;
    input->_has_counter = 1u;
    *out = sample;
    return DOK_STATUS_OK;
}

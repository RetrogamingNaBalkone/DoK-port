#include "dok/controls.h"
#include <stddef.h>
#include <string.h>

#define CONTROLS_TAG UINT32_C(0x4e30394c)

dok_status dok_controls_initialize(dok_control_latch *latch)
{
    if (latch == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    memset(latch, 0, sizeof(*latch));
    latch->_tag = CONTROLS_TAG;
    return DOK_STATUS_OK;
}

dok_status dok_controls_feed(dok_control_latch *latch,
                              const dok_input_frame *frame)
{
    uint32_t i;
    if (latch == NULL || dok_input_frame_validate(frame) != DOK_STATUS_OK || frame->sequence == 0)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (latch->_tag != CONTROLS_TAG || frame->sequence <= latch->_last_sequence)
        return DOK_STATUS_INVALID_STATE;
    for (i = 0; i < 12; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if (((frame->pressed & bit) && latch->_presses[i] == UINT32_MAX) ||
            ((frame->released & bit) && latch->_releases[i] == UINT32_MAX))
            return DOK_STATUS_LIMIT_REACHED;
    }
    if ((frame->released & (DOK_BUTTON_X | DOK_BUTTON_B)) && latch->_uses == UINT32_MAX)
        return DOK_STATUS_LIMIT_REACHED;
    for (i = 0; i < 12; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if (frame->pressed & bit) ++latch->_presses[i];
        if (frame->released & bit) ++latch->_releases[i];
    }
    if (frame->released & (DOK_BUTTON_X | DOK_BUTTON_B)) ++latch->_uses;
    latch->_held = frame->held;
    latch->_last_sequence = frame->sequence;
    return DOK_STATUS_OK;
}

dok_status dok_controls_take(dok_control_latch *latch,
                              dok_control_commands *out)
{
    dok_control_commands commands = {0};
    dok_input_frame mapped_frame = {0};
    dok_input_commands mapped;
    uint32_t i;
    if (latch == NULL || out == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    if (latch->_tag != CONTROLS_TAG) return DOK_STATUS_INVALID_STATE;
    commands.physical_held = latch->_held;
    for (i = 0; i < 12; ++i) {
        if (latch->_presses[i]) commands.physical_pressed |= UINT32_C(1) << i;
        if (latch->_releases[i]) commands.physical_released |= UINT32_C(1) << i;
    }
    mapped_frame.held = commands.physical_held | commands.physical_pressed;
    mapped_frame.pressed = commands.physical_pressed;
    if (dok_input_map(&mapped_frame, &mapped) != DOK_STATUS_OK)
        return DOK_STATUS_INVALID_STATE;
    /* Native Use reads the unconsumed break/release state, not make/held. */
    commands.native_commands = mapped.native_held & ~(uint32_t)DOK_INPUT_NATIVE_USE;
    commands.next_weapon_pressed = mapped.next_weapon_pressed;
    commands.pause_pressed = mapped.pause_pressed;
    commands.use_triggered = latch->_uses != 0;
    if (commands.use_triggered) commands.native_commands |= DOK_INPUT_NATIVE_USE;
    for (i = 0; i < 12; ++i) {
        if (latch->_presses[i]) --latch->_presses[i];
        if (latch->_releases[i]) --latch->_releases[i];
    }
    if (latch->_uses) --latch->_uses;
    *out = commands;
    return DOK_STATUS_OK;
}

dok_status dok_controls_ui_boundary(dok_control_latch *latch)
{
    if (latch == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    if (latch->_tag != CONTROLS_TAG) return DOK_STATUS_INVALID_STATE;
    memset(latch->_presses, 0, sizeof(latch->_presses));
    memset(latch->_releases, 0, sizeof(latch->_releases));
    latch->_held = 0; latch->_uses = 0;
    return DOK_STATUS_OK;
}

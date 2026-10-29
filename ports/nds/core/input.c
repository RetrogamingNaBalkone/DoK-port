#include "dok/input.h"

#include <stddef.h>
#include <string.h>

#define INPUT_TAG UINT32_C(0x49303942)

static uint32_t native_mask(uint32_t buttons)
{
    uint32_t mask = 0u;
    if ((buttons & DOK_BUTTON_Y) != 0u) mask |= DOK_INPUT_NATIVE_TURN_POS;
    if ((buttons & DOK_BUTTON_A) != 0u) mask |= DOK_INPUT_NATIVE_TURN_NEG;
    if ((buttons & DOK_BUTTON_UP) != 0u) mask |= DOK_INPUT_NATIVE_FORWARD;
    if ((buttons & DOK_BUTTON_DOWN) != 0u) mask |= DOK_INPUT_NATIVE_BACKWARD;
    if ((buttons & DOK_BUTTON_LEFT) != 0u) mask |= DOK_INPUT_NATIVE_STRAFE_LEFT;
    if ((buttons & DOK_BUTTON_RIGHT) != 0u) mask |= DOK_INPUT_NATIVE_STRAFE_RIGHT;
    if ((buttons & DOK_BUTTON_R) != 0u) mask |= DOK_INPUT_NATIVE_FIRE;
    if ((buttons & (DOK_BUTTON_X | DOK_BUTTON_B)) != 0u)
        mask |= DOK_INPUT_NATIVE_USE;
    if ((buttons & DOK_BUTTON_L) != 0u) mask |= DOK_INPUT_NATIVE_RUN;
    return mask;
}

dok_status dok_input_buffer_initialize(dok_input_buffer *buffer)
{
    if (buffer == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    memset(buffer, 0, sizeof(*buffer));
    buffer->_tag = INPUT_TAG;
    return DOK_STATUS_OK;
}

dok_status dok_input_capture(dok_input_buffer *buffer,
                             const dok_input_frame *frame, uint64_t counter)
{
    dok_input_sample sample;
    uint32_t tail;
    if (buffer == NULL || dok_input_frame_validate(frame) != DOK_STATUS_OK ||
        frame->sequence == 0u)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (buffer->_tag != INPUT_TAG) return DOK_STATUS_INVALID_STATE;
    if ((buffer->_has_capture != 0u &&
         (frame->sequence <= buffer->_last_sequence ||
          counter < buffer->_last_counter)) ||
        (buffer->_has_cutoff != 0u && counter < buffer->_last_cutoff))
        return DOK_STATUS_INVALID_STATE;
    if (buffer->_count == DOK_INPUT_SAMPLE_LIMIT) return DOK_STATUS_NO_SPACE;

    sample.frame = *frame;
    sample.counter = counter;
    sample.frame.held &= ~buffer->_blocked;
    sample.frame.pressed &= ~buffer->_blocked;
    sample.frame.released &= ~buffer->_blocked;
    if (buffer->_touch_blocked != 0u) {
        sample.frame.touch_flags = 0u;
        sample.frame.touch_x = 0;
        sample.frame.touch_y = 0;
    }
    tail = (buffer->_head + buffer->_count) % DOK_INPUT_SAMPLE_LIMIT;
    buffer->_samples[tail] = sample;
    ++buffer->_count;
    buffer->_last_sequence = frame->sequence;
    buffer->_last_counter = counter;
    buffer->_has_capture = 1u;
    buffer->_raw_held = frame->held;
    buffer->_raw_touch = frame->touch_flags & DOK_TOUCH_HELD;
    buffer->_blocked &= frame->held;
    if ((frame->touch_flags & DOK_TOUCH_HELD) == 0u)
        buffer->_touch_blocked = 0u;
    return DOK_STATUS_OK;
}

dok_status dok_input_pop_through(dok_input_buffer *buffer, uint64_t cutoff,
                                 dok_input_sample *out, uint32_t *available)
{
    if (buffer == NULL || out == NULL || available == NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (buffer->_tag != INPUT_TAG ||
        (buffer->_has_cutoff != 0u && cutoff < buffer->_last_cutoff))
        return DOK_STATUS_INVALID_STATE;
    buffer->_last_cutoff = cutoff;
    buffer->_has_cutoff = 1u;
    *available = 0u;
    if (buffer->_count == 0u || buffer->_samples[buffer->_head].counter > cutoff)
        return DOK_STATUS_OK;
    *out = buffer->_samples[buffer->_head];
    buffer->_head = (buffer->_head + 1u) % DOK_INPUT_SAMPLE_LIMIT;
    --buffer->_count;
    *available = 1u;
    return DOK_STATUS_OK;
}

dok_status dok_input_ui_boundary(dok_input_buffer *buffer)
{
    if (buffer == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    if (buffer->_tag != INPUT_TAG) return DOK_STATUS_INVALID_STATE;
    buffer->_head = 0u;
    buffer->_count = 0u;
    buffer->_blocked |= buffer->_raw_held;
    buffer->_touch_blocked |= buffer->_raw_touch;
    return DOK_STATUS_OK;
}

dok_status dok_input_map(const dok_input_frame *frame,
                         dok_input_commands *out)
{
    dok_input_commands commands;
    if (out == NULL || dok_input_frame_validate(frame) != DOK_STATUS_OK)
        return DOK_STATUS_INVALID_ARGUMENT;
    commands.native_held = native_mask(frame->held);
    commands.native_pressed = native_mask(frame->pressed);
    commands.native_released = native_mask(frame->released);
    commands.next_weapon_pressed =
        (frame->pressed & DOK_BUTTON_SELECT) != 0u;
    commands.pause_pressed = (frame->pressed & DOK_BUTTON_START) != 0u;
    *out = commands;
    return DOK_STATUS_OK;
}
